#ifndef AGENT_IMGUI_LLM_PANEL_H_
#define AGENT_IMGUI_LLM_PANEL_H_

namespace mujoco::studio {

class UiAgent;

// Renders the LLM conversation inside the current ImGui window (the command
// palette), so an "ask" exchange happens right in the Ctrl+P box. Keeps a small
// amount of presentation state: a typewriter reveal of the latest reply so
// demos/GIFs read naturally.
class LlmPanel {
 public:
  void Render(UiAgent& agent);

 private:
  int revealing_index_ = -1;  // history index of the reply being revealed.
  int reveal_chars_ = 0;
  int last_turn_count_ = 0;   // history size last frame; detects new/cleared.
  bool scroll_to_bottom_ = false;  // one-shot jump after a new turn arrives.
};

}  // namespace mujoco::studio

#endif  // AGENT_IMGUI_LLM_PANEL_H_
