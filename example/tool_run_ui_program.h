#ifndef AGENT_IMGUI_EXAMPLE_TOOL_RUN_UI_PROGRAM_H_
#define AGENT_IMGUI_EXAMPLE_TOOL_RUN_UI_PROGRAM_H_

#include <string>

#include "agent_imgui/llm_provider.h"
#include "agent_imgui/test_runner.h"

namespace example {

// The "tool_run_ui_program" tool: the actuator. Runs a JSON op-program of Dear
// ImGui Test Engine operations against the real widgets.
agent_imgui::ToolDef RunUiProgramToolDef();
agent_imgui::ToolResult RunUiProgramTool(agent_imgui::TestRunner& runner,
                                         const std::string& json_args);

}  // namespace example

#endif  // AGENT_IMGUI_EXAMPLE_TOOL_RUN_UI_PROGRAM_H_
