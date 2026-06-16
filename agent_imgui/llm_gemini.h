#ifndef AGENT_IMGUI_LLM_GEMINI_H_
#define AGENT_IMGUI_LLM_GEMINI_H_

#include <string>
#include <utility>
#include <vector>

#include "agent_imgui/llm_provider.h"

namespace agent_imgui {

// Talks to the Google Gemini API (generativelanguage.googleapis.com,
// :generateContent) with function calling, mirroring ClaudeProvider's own
// hand-rolled tool-use loop. Defaults to gemini-2.5-flash; switchable via
// SetModel / the "/model gemini-..." command. The API key is read from
// GEMINI_API_KEY (or GOOGLE_API_KEY). Transport is WinHTTP on Windows; other
// platforms return an error result for now (no SDK in-tree).
class GeminiProvider : public LlmProvider {
 public:
  explicit GeminiProvider(std::string api_key);

  // Returns GEMINI_API_KEY (or GOOGLE_API_KEY), or "" if unset/empty.
  static std::string KeyFromEnv();

  // The {alias, full id} pairs this provider understands, for the "/model"
  // listing (e.g. {"flash", "gemini-2.5-flash"}).
  static std::vector<std::pair<std::string, std::string>> Models();

  LlmResult Send(const std::string& system,
                 const std::vector<LlmMessage>& messages,
                 const std::vector<ToolDef>& tools,
                 const ToolExecutor& exec,
                 const ProgressCallback& on_thinking = {}) override;

  const char* name() const override { return "Gemini"; }

  // Accepts "flash"/"pro", "gemini" (-> default flash), or a full "gemini-..."
  // id. Returns the resolved id, or "".
  std::string SetModel(const std::string& id_or_alias) override;
  std::string Model() const override { return model_; }

 private:
  std::string api_key_;
  std::string model_ = "gemini-2.5-flash";
  int max_tokens_ = 8192;
};

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_LLM_GEMINI_H_
