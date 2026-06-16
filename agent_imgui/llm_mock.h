#ifndef AGENT_IMGUI_LLM_MOCK_H_
#define AGENT_IMGUI_LLM_MOCK_H_

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "agent_imgui/llm_provider.h"

namespace mujoco::studio {

// A deterministic, offline provider. Used for headless GIF capture and when no
// ANTHROPIC_API_KEY is set, so the UI plumbing (including the open_tool_window
// tool) can be exercised without a network call. It "decides" to open a panel
// by scanning the prompt for a tool's panel name.
class MockProvider : public LlmProvider {
 public:
  LlmResult Send(const std::string& /*system*/,
                 const std::vector<LlmMessage>& messages,
                 const std::vector<ToolDef>& /*tools*/,
                 const ToolExecutor& exec,
                 const ProgressCallback& /*on_thinking*/ = {}) override {
    LlmResult r;
    r.ok = true;
    std::string q = messages.empty() ? "" : messages.back().text;
    std::string lower = q;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Crudely mirror Claude: if the prompt names a known panel, "call" the tool.
    static const char* kPanels[] = {"physics",   "rendering", "visualization",
                                    "joints",    "controls",  "sensor",
                                    "watch",     "state",     "explorer",
                                    "editor"};
    for (const char* p : kPanels) {
      if (lower.find(p) != std::string::npos) {
        std::string title(1, static_cast<char>(std::toupper(p[0])));
        title += (p + 1);
        if (exec) {
          exec("open_tool_window", "{\"name\":\"" + title + "\"}");
        }
        r.text = "Opened the " + title + " panel.";
        return r;
      }
    }
    r.text =
        "Connected. Ask me to open a panel (e.g. \"open the physics menu\").";
    return r;
  }

  const char* name() const override { return "mock"; }
};

}  // namespace mujoco::studio

#endif  // AGENT_IMGUI_LLM_MOCK_H_
