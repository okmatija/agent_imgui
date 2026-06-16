#ifndef AGENT_IMGUI_HTTP_TRANSPORT_H_
#define AGENT_IMGUI_HTTP_TRANSPORT_H_

#include <memory>
#include <string>
#include <vector>

namespace agent_imgui {

// =============================================================================
// Transport seam.
//
// This is the ONLY networking surface in agent_imgui. It is deliberately tiny
// and has nothing to do with LLMs: an LlmProvider (see llm_provider.h) turns a
// conversation into an HTTPS request, hands it to an HttpTransport, and parses
// the reply. The two seams never mix.
//
// To run agent_imgui somewhere the bundled backends don't cover -- e.g. a Dear
// ImGui app compiled to WebAssembly -- implement this one interface (using
// emscripten fetch, a host callback, etc.) and pass it to a provider:
//
//     auto transport = std::make_shared<MyWasmTransport>();
//     agent.set_provider(std::make_unique<ClaudeProvider>(key, transport));
//
// The built-in DefaultHttpTransport() covers desktop (WinHTTP / libcurl).
// =============================================================================

// The result of one HTTP request.
struct HttpResponse {
  bool ok = false;     // true if a response was received (any HTTP status)
  int status = 0;      // HTTP status code (e.g. 200, 429), valid when ok
  std::string body;    // response body, valid when ok
  std::string error;   // human-readable transport error, set when !ok
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;

  // POST `body` to `url` (an "https://host/path" URL) with the given request
  // headers (each "Name: value", ASCII). Blocking. Must be safe to call from a
  // worker thread. Implementations should not throw.
  virtual HttpResponse Post(const std::string& url,
                            const std::vector<std::string>& headers,
                            const std::string& body) = 0;
};

// The built-in transport for the current platform: WinHTTP on Windows, libcurl
// elsewhere (when built with AGENT_IMGUI_HAVE_CURL). Never returns null; if no
// backend was compiled in, the returned transport reports an error from Post().
std::shared_ptr<HttpTransport> DefaultHttpTransport();

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_HTTP_TRANSPORT_H_
