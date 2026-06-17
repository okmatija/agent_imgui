#ifndef AGENT_IMGUI_EXAMPLE_TOOL_SCREENSHOT_H_
#define AGENT_IMGUI_EXAMPLE_TOOL_SCREENSHOT_H_

#include <condition_variable>
#include <mutex>
#include <string>

#include "agent_imgui/llm_provider.h"

namespace example {

// Hand-off so the worker-thread "tool_screenshot" tool gets a frame captured by
// the render thread (capture must happen on the thread that owns the GPU
// device). The render loop fills this in via the renderer; see main.cc.
struct CaptureBridge {
  std::mutex mu;
  std::condition_variable cv;
  bool requested = false;
  bool done = false;
  std::string png;  // PNG bytes when done
};

// The "tool_screenshot" tool: hands the model a PNG of the current UI.
agent_imgui::ToolDef ScreenshotToolDef();
agent_imgui::ToolResult RunScreenshotTool(CaptureBridge& bridge,
                                          const std::string& json_args);

}  // namespace example

#endif  // AGENT_IMGUI_EXAMPLE_TOOL_SCREENSHOT_H_
