#ifndef AGENT_IMGUI_LLM_CLAUDE_H_
#define AGENT_IMGUI_LLM_CLAUDE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent_imgui/llm_provider.h"

namespace agent_imgui {

class HttpTransport;

// Talks to the Anthropic Messages API (POST /v1/messages). Defaults to model
// claude-sonnet-4-6 (switchable at runtime via SetModel / the "/model" command).
// The API key is read from the
// ANTHROPIC_API_KEY environment variable (see KeyFromEnv). Transport is WinHTTP
// on Windows and libcurl elsewhere (see agent_imgui/http.h).
//
// The MVP uses a single, non-streaming request: the conversation is short and
// the reply (a few sentences) fits comfortably under max_tokens. Streaming is a
// later refinement noted in the design doc.
class ClaudeProvider : public LlmProvider {
 public:
  // `transport` carries the HTTPS requests; defaults to DefaultHttpTransport().
  // Inject your own (e.g. a WebAssembly transport) to network differently.
  explicit ClaudeProvider(std::string api_key,
                          std::shared_ptr<HttpTransport> transport = nullptr);

  // Returns the ANTHROPIC_API_KEY value, or "" if unset/empty.
  static std::string KeyFromEnv();

  // The {alias, full id} pairs this provider understands, for the "/model"
  // listing (e.g. {"opus", "claude-opus-4-8"}).
  static std::vector<std::pair<std::string, std::string>> Models();

  LlmResult Send(const std::string& system,
                 const std::vector<LlmMessage>& messages,
                 const std::vector<ToolDef>& tools,
                 const ToolExecutor& exec,
                 const ProgressCallback& on_thinking = {}) override;

  const char* name() const override { return "Claude"; }

  // Accepts "opus"/"sonnet"/"haiku" or a full "claude-..." id; updates the
  // thinking mode to suit (adaptive for the opus/sonnet 4.6+ family, off for
  // Haiku, which doesn't accept it). Returns the resolved id, or "".
  std::string SetModel(const std::string& id_or_alias) override;
  std::string Model() const override { return model_; }

 private:
  std::shared_ptr<HttpTransport> transport_;
  std::string api_key_;
  std::string model_ = "claude-sonnet-4-6";
  bool adaptive_thinking_ = true;  // on for the opus/sonnet 4.6+ family
  int max_tokens_ = 8192;
};

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_LLM_CLAUDE_H_
