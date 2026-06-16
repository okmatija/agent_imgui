#ifndef AGENT_IMGUI_LLM_PROVIDER_H_
#define AGENT_IMGUI_LLM_PROVIDER_H_

#include <functional>
#include <string>
#include <vector>

namespace agent_imgui {

// One turn in the conversation. `role` is "user" or "assistant".
struct LlmMessage {
  std::string role;
  std::string text;
};

// A tool the model may call. `input_schema` is a JSON-Schema object (as a JSON
// string) describing the arguments.
struct ToolDef {
  std::string name;
  std::string description;
  std::string input_schema;
};

// An image a tool hands back to a multimodal model (e.g. a screenshot).
struct ToolImage {
  std::string media_type;   // e.g. "image/png"
  std::string data_base64;  // base64-encoded image bytes
};

// What a tool call returns to the model: a short human-readable text result and,
// optionally, images (only used by multimodal providers; ignored otherwise). It
// implicitly constructs from a string, so a text-only tool can just
// `return "...";`.
struct ToolResult {
  std::string text;
  std::vector<ToolImage> images;

  ToolResult() = default;
  ToolResult(std::string t) : text(std::move(t)) {}  // NOLINT(runtime/explicit)
  ToolResult(const char* t) : text(t) {}             // NOLINT(runtime/explicit)
};

// Executes a tool call and returns the result fed back to the model. `json_args`
// is the raw JSON arguments object the model produced. The provider runs the
// tool-use loop internally and invokes this for each call; implementations
// should be quick and side-effecting (e.g. open a window) -- this is the "as if
// clicking the button" seam.
using ToolExecutor =
    std::function<ToolResult(const std::string& name,
                             const std::string& json_args)>;

// Optional progress callback, invoked on the worker thread as extended-thinking
// accumulates across the tool-use loop. Lets a UI show partial reasoning (e.g.
// the thinking-so-far when the user cancels). `thinking_so_far` is cumulative.
using ProgressCallback = std::function<void(const std::string& thinking_so_far)>;

// The outcome of a request (after any tool-use round trips).
struct LlmResult {
  bool ok = false;
  std::string text;      // final assistant reply, when ok.
  std::string thinking;  // extended-thinking text, when the model produced any.
  std::string error;     // human-readable message, when !ok.
};

// Provider-agnostic seam for talking to an LLM. Implementations are called on a
// worker thread (or inline in synchronous capture), so Send() may block on
// network I/O and on `exec`.
class LlmProvider {
 public:
  virtual ~LlmProvider() = default;

  // `system` is the system prompt; `messages` is the running conversation,
  // oldest first, ending with the latest user turn. `tools` are offered to the
  // model; when it calls one, `exec` runs it and the loop continues until the
  // model returns a plain-text answer.
  virtual LlmResult Send(const std::string& system,
                         const std::vector<LlmMessage>& messages,
                         const std::vector<ToolDef>& tools,
                         const ToolExecutor& exec,
                         const ProgressCallback& on_thinking = {}) = 0;

  // Short name for the status line (e.g. "Claude", "mock").
  virtual const char* name() const = 0;

  // Switch the active model. `id_or_alias` is a friendly alias ("opus",
  // "sonnet", "haiku") or a full model id. Returns the resolved model id, or ""
  // if unrecognized/unsupported. Default: no-op.
  virtual std::string SetModel(const std::string& /*id_or_alias*/) {
    return "";
  }

  // The current model id (e.g. "claude-opus-4-8"), or "" if not applicable.
  virtual std::string Model() const { return ""; }
};

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_LLM_PROVIDER_H_
