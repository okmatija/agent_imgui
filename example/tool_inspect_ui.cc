#include "tool_inspect_ui.h"

namespace example {

agent_imgui::ToolDef InspectUiToolDef() {
  return {"tool_inspect_ui",
          "Lists the widgets currently visible on screen, grouped by window, "
          "with their labels and ids. Call it to find exact item paths. No "
          "arguments.",
          "{\"type\":\"object\",\"properties\":{},\"required\":[]}"};
}

agent_imgui::ToolResult RunInspectUiTool(agent_imgui::TestRunner& runner,
                                         const std::string& /*json_args*/) {
  return runner.Inspect();
}

}  // namespace example
