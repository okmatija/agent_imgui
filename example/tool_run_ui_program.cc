#include "tool_run_ui_program.h"

namespace example {
namespace {

const char* kSchema =
    "{\"type\":\"object\",\"properties\":{\"ops\":{\"type\":\"array\",\"items\":"
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"enum\":[\"item_click\",\"click_id\","
    "\"right_click\",\"double_click\",\"item_check\",\"item_uncheck\","
    "\"item_open\",\"item_close\",\"set_float\",\"set_int\",\"set_text\","
    "\"combo_select\",\"menu_click\",\"scroll\",\"hover\",\"key_chars\","
    "\"key_press\",\"wait\"]},"
    "\"ref\":{\"type\":\"string\",\"description\":\"ImGui item path, e.g. "
    "//Dear ImGui Demo/Widgets\"},"
    "\"id\":{\"type\":\"number\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"menu path for menu_click\"},"
    "\"value\":{\"description\":\"number for set_float/set_int, or option text "
    "for combo_select\"},"
    "\"text\":{\"type\":\"string\"},\"key\":{\"type\":\"string\"},"
    "\"to\":{\"type\":\"string\"},\"seconds\":{\"type\":\"number\"}},"
    "\"required\":[\"op\"]}}},\"required\":[\"ops\"]}";

}  // namespace

agent_imgui::ToolDef RunUiProgramToolDef() {
  return {"tool_run_ui_program",
          "Drive the UI by running a short program of Dear ImGui Test Engine "
          "operations -- this clicks/drags/types the real widgets. Reference "
          "items by their ImGui path (e.g. //Dear ImGui Demo/Widgets). Open a "
          "tree node/header before touching its children.",
          kSchema};
}

agent_imgui::ToolResult RunUiProgramTool(agent_imgui::TestRunner& runner,
                                         const std::string& json_args) {
  const int n = runner.Run(json_args);
  return "Queued a " + std::to_string(n) + "-op UI program.";
}

}  // namespace example
