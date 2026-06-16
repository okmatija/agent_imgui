#include "agent_imgui/llm_gemini.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "agent_imgui/http_transport.h"
#include "agent_imgui/llm_provider.h"

namespace agent_imgui {
namespace {

constexpr int kMaxToolIterations = 20;

// --- Minimal JSON helpers (same approach as llm_claude.cc: build by hand, pull
// out the pieces we need; no general-purpose parser). ------------------------

std::string JsonString(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o += static_cast<char>(c);
        }
    }
  }
  o += "\"";
  return o;
}

void AppendUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

unsigned Hex4(const std::string& s, size_t i) {
  unsigned v = 0;
  for (size_t k = 0; k < 4 && i + k < s.size(); ++k) {
    char c = s[i + k];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= (c - '0');
    else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
  }
  return v;
}

size_t ParseJsonStringAt(const std::string& s, size_t pos, std::string& out) {
  if (pos >= s.size() || s[pos] != '"') return std::string::npos;
  size_t i = pos + 1;
  while (i < s.size()) {
    char c = s[i];
    if (c == '"') return i + 1;
    if (c == '\\') {
      if (i + 1 >= s.size()) return std::string::npos;
      char e = s[i + 1];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': AppendUtf8(out, Hex4(s, i + 2)); i += 4; break;
        default: out += e; break;
      }
      i += 2;
    } else {
      out += c;
      ++i;
    }
  }
  return std::string::npos;
}

// Given `pos` at a '{' or '[', returns the index just past the matching close,
// respecting strings/escapes. npos on malformed input.
size_t SkipJsonValue(const std::string& s, size_t pos) {
  if (pos >= s.size() || (s[pos] != '{' && s[pos] != '[')) {
    return std::string::npos;
  }
  const char open = s[pos];
  const char close = (open == '{') ? '}' : ']';
  int depth = 0;
  bool in_str = false;
  for (size_t i = pos; i < s.size(); ++i) {
    char c = s[i];
    if (in_str) {
      if (c == '\\') { ++i; continue; }
      if (c == '"') in_str = false;
    } else if (c == '"') {
      in_str = true;
    } else if (c == open) {
      ++depth;
    } else if (c == close) {
      if (--depth == 0) return i + 1;
    }
  }
  return std::string::npos;
}

// Reads the JSON string value that follows `key` (e.g. "\"name\":") in `s`.
std::string ReadStringField(const std::string& s, const std::string& key) {
  size_t k = s.find(key);
  if (k == std::string::npos) return "";
  size_t q = s.find('"', k + key.size());
  if (q == std::string::npos) return "";
  std::string out;
  ParseJsonStringAt(s, q, out);
  return out;
}

// Invokes `fn` on each object element of the JSON array string `arr` ("[...]").
template <class Fn>
void ForEachArrayObject(const std::string& arr, Fn fn) {
  size_t i = arr.find('[');
  if (i == std::string::npos) return;
  ++i;
  while (i < arr.size()) {
    while (i < arr.size() &&
           (arr[i] == ' ' || arr[i] == '\n' || arr[i] == '\t' ||
            arr[i] == '\r' || arr[i] == ',')) {
      ++i;
    }
    if (i >= arr.size() || arr[i] == ']') break;
    if (arr[i] != '{') { ++i; continue; }
    size_t end = SkipJsonValue(arr, i);
    if (end == std::string::npos) break;
    fn(arr.substr(i, end - i));
    i = end;
  }
}

// --- Gemini request building ------------------------------------------------

// Serializes the conversation as the inner of a "contents" array (no brackets).
// role "assistant" -> "model"; everything else -> "user".
std::string SerializeContents(const std::vector<LlmMessage>& messages) {
  std::string inner;
  for (size_t i = 0; i < messages.size(); ++i) {
    if (i) inner += ",";
    const char* role = (messages[i].role == "assistant") ? "model" : "user";
    inner += "{\"role\":\"" + std::string(role) +
             "\",\"parts\":[{\"text\":" + JsonString(messages[i].text) + "}]}";
  }
  return inner;
}

// Serializes our ToolDefs as Gemini's tools/function_declarations. Our
// input_schema is already a JSON-Schema object (OpenAPI-compatible enough).
std::string SerializeTools(const std::vector<ToolDef>& tools) {
  if (tools.empty()) return "";
  std::string decls = "[";
  for (size_t i = 0; i < tools.size(); ++i) {
    if (i) decls += ",";
    decls += "{\"name\":" + JsonString(tools[i].name) +
             ",\"description\":" + JsonString(tools[i].description) +
             ",\"parameters\":" + tools[i].input_schema + "}";
  }
  decls += "]";
  return "[{\"function_declarations\":" + decls + "}]";
}

std::string BuildRequestBody(const std::string& system,
                             const std::string& contents_inner,
                             const std::string& tools_inner, int max_tokens) {
  std::string body = "{";
  if (!system.empty()) {
    body += "\"system_instruction\":{\"parts\":[{\"text\":" + JsonString(system) +
            "}]},";
  }
  if (!tools_inner.empty()) body += "\"tools\":" + tools_inner + ",";
  body += "\"generationConfig\":{\"maxOutputTokens\":" +
          std::to_string(max_tokens) + "},";
  body += "\"contents\":[" + contents_inner + "]}";
  return body;
}

// --- Gemini response parsing ------------------------------------------------

struct FnCall {
  std::string name;
  std::string args;  // raw JSON object string
};

// The first candidate's content object ({"role":"model","parts":[...]}), echoed
// back into the conversation. "" if absent.
std::string ExtractContentObject(const std::string& json) {
  size_t c = json.find("\"candidates\"");
  size_t k = json.find("\"content\"", c == std::string::npos ? 0 : c);
  if (k == std::string::npos) return "";
  size_t brace = json.find('{', k);
  if (brace == std::string::npos) return "";
  size_t end = SkipJsonValue(json, brace);
  return end == std::string::npos ? "" : json.substr(brace, end - brace);
}

std::string ExtractPartsArray(const std::string& content) {
  size_t k = content.find("\"parts\"");
  if (k == std::string::npos) return "";
  size_t br = content.find('[', k);
  if (br == std::string::npos) return "";
  size_t end = SkipJsonValue(content, br);
  return end == std::string::npos ? "" : content.substr(br, end - br);
}

std::string ExtractErrorMessage(const std::string& json) {
  return ReadStringField(json, "\"message\"");
}

}  // namespace

std::string GeminiProvider::KeyFromEnv() {
  const char* k = std::getenv("GEMINI_API_KEY");
  if (!k || !*k) k = std::getenv("GOOGLE_API_KEY");
  return (k && *k) ? std::string(k) : std::string();
}

GeminiProvider::GeminiProvider(std::string api_key,
                               std::shared_ptr<HttpTransport> transport)
    : transport_(transport ? std::move(transport) : DefaultHttpTransport()),
      api_key_(std::move(api_key)) {}

std::vector<std::pair<std::string, std::string>> GeminiProvider::Models() {
  return {{"flash", "gemini-2.5-flash"}, {"pro", "gemini-2.5-pro"}};
}

std::string GeminiProvider::SetModel(const std::string& id_or_alias) {
  std::string a;
  for (char c : id_or_alias) {
    if (c == ' ' || c == '\t') continue;
    a += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  std::string id;
  if (a == "flash") {
    id = "gemini-2.5-flash";
  } else if (a == "pro") {
    id = "gemini-2.5-pro";
  } else if (a == "gemini") {
    id = "gemini-2.5-flash";  // default
  } else if (a.rfind("gemini-", 0) == 0) {
    id = a;  // a full model id, passed through
  } else {
    return "";
  }
  model_ = id;
  return id;
}

}  // namespace agent_imgui

// ---------------------------------------------------------------------------
// The tool-use loop. Platform-independent: HTTP goes through the injected
// HttpTransport (see agent_imgui/http_transport.h).
// ---------------------------------------------------------------------------

namespace agent_imgui {

LlmResult GeminiProvider::Send(const std::string& system,
                               const std::vector<LlmMessage>& messages,
                               const std::vector<ToolDef>& tools,
                               const ToolExecutor& exec,
                               const ProgressCallback& /*on_thinking*/) {
  LlmResult r;
  if (api_key_.empty()) {
    r.error = "GEMINI_API_KEY is not set.";
    return r;
  }

  const std::string url = "https://generativelanguage.googleapis.com/v1beta/"
                          "models/" +
                          model_ + ":generateContent";
  const std::vector<std::string> headers = {"x-goog-api-key: " + api_key_,
                                            "content-type: application/json"};
  const std::string contents = SerializeContents(messages);
  const std::string tools_inner = SerializeTools(tools);
  std::string extra;  // model/function turns appended across the loop.

  const bool verbose = std::getenv("AGENT_IMGUI_LLM_VERBOSE") != nullptr;
  auto vtrunc = [](const std::string& s, size_t n) {
    return s.size() > n ? s.substr(0, n) + " ...(truncated)" : s;
  };

  for (int iter = 0; iter < kMaxToolIterations; ++iter) {
    const std::string body =
        BuildRequestBody(system, contents + extra, tools_inner, max_tokens_);

    const HttpResponse http = transport_->Post(url, headers, body);
    if (!http.ok) {
      r.error = http.error;
      return r;
    }
    const int status = http.status;
    const std::string& response = http.body;
    if (status != 200) {
      std::string msg = ExtractErrorMessage(response);
      r.error = "HTTP " + std::to_string(status) +
                (msg.empty() ? (": " + response) : (": " + msg));
      return r;
    }

    const std::string content = ExtractContentObject(response);
    const std::string parts = ExtractPartsArray(content);

    std::vector<FnCall> calls;
    std::string text;
    ForEachArrayObject(parts, [&](const std::string& part) {
      if (part.find("\"functionCall\"") != std::string::npos) {
        FnCall fc;
        fc.name = ReadStringField(part, "\"name\"");
        size_t a = part.find("\"args\"");
        if (a != std::string::npos) {
          size_t br = part.find('{', a);
          if (br != std::string::npos) {
            size_t end = SkipJsonValue(part, br);
            if (end != std::string::npos) fc.args = part.substr(br, end - br);
          }
        }
        if (fc.args.empty()) fc.args = "{}";
        calls.push_back(std::move(fc));
      } else if (part.find("\"text\"") != std::string::npos) {
        text += ReadStringField(part, "\"text\"");
      }
    });

    if (verbose && !text.empty()) {
      std::fprintf(stderr, "\n===== assistant (turn %d) =====\n%s\n", iter,
                   text.c_str());
    }

    if (calls.empty()) {
      r.text = text;
      if (r.text.empty()) {
        r.error = "Empty response from Gemini.";
        return r;
      }
      if (verbose) {
        std::fprintf(stderr, "\n===== final assistant reply =====\n%s\n",
                     r.text.c_str());
      }
      r.ok = true;
      return r;
    }

    // Echo the model's content (its functionCall parts) back, then answer each
    // call with a functionResponse and loop.
    if (content.empty()) {
      r.error = "Could not parse Gemini content.";
      return r;
    }
    extra += "," + content;

    std::string fr = "[";
    for (size_t k = 0; k < calls.size(); ++k) {
      if (verbose) {
        std::fprintf(stderr, "\n----- tool_use (turn %d): %s -----\n%s\n", iter,
                     calls[k].name.c_str(), vtrunc(calls[k].args, 2000).c_str());
      }
      ToolResult res =
          exec ? exec(calls[k].name, calls[k].args) : ToolResult("(no executor)");
      const std::string& out = res.text;  // Gemini tool results are text-only
      if (verbose) {
        std::fprintf(stderr, "----- tool_result: %s -----\n%s\n",
                     calls[k].name.c_str(), vtrunc(out, 2000).c_str());
      }
      if (k) fr += ",";
      fr += "{\"functionResponse\":{\"name\":" + JsonString(calls[k].name) +
            ",\"response\":{\"result\":" + JsonString(out) + "}}}";
    }
    fr += "]";
    extra += ",{\"role\":\"user\",\"parts\":" + fr + "}";
  }

  r.error = "Tool-use loop did not converge.";
  return r;
}

}  // namespace agent_imgui
