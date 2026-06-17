#ifndef AGENT_IMGUI_EXAMPLE_TOOL_INSPECT_UI_H_
#define AGENT_IMGUI_EXAMPLE_TOOL_INSPECT_UI_H_

#include <string>

#include "agent_imgui/llm_provider.h"
#include "agent_imgui/test_runner.h"

namespace example {

// The "tool_inspect_ui" tool: a textual listing of the on-screen widgets.
agent_imgui::ToolDef InspectUiToolDef();
agent_imgui::ToolResult RunInspectUiTool(agent_imgui::TestRunner& runner,
                                         const std::string& json_args);

}  // namespace example

#endif  // AGENT_IMGUI_EXAMPLE_TOOL_INSPECT_UI_H_
