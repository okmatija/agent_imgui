#include "tool_screenshot.h"

#include <chrono>

namespace example {
namespace {

std::string Base64(const std::string& in) {
  static const char* t =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  auto uc = [](char c) {
    return static_cast<unsigned>(static_cast<unsigned char>(c));
  };
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const unsigned v = (uc(in[i]) << 16) | (uc(in[i + 1]) << 8) | uc(in[i + 2]);
    out += t[(v >> 18) & 63];
    out += t[(v >> 12) & 63];
    out += t[(v >> 6) & 63];
    out += t[v & 63];
  }
  if (i < in.size()) {
    const bool two = (i + 1 < in.size());
    unsigned v = uc(in[i]) << 16;
    if (two) v |= uc(in[i + 1]) << 8;
    out += t[(v >> 18) & 63];
    out += t[(v >> 12) & 63];
    out += two ? t[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

}  // namespace

agent_imgui::ToolDef ScreenshotToolDef() {
  return {
      "tool_screenshot",
      "Returns a PNG screenshot of the current UI for you to look at. You MUST "
      "call this whenever the user's request needs information that is on screen "
      "but does NOT appear in the tool_inspect_ui result -- e.g. colours, a "
      "plotted curve, an image, rendered shapes, or the relative position / "
      "layout of things. tool_inspect_ui only lists widget labels and ids, so "
      "anything purely visual is invisible to it. Do NOT take screenshots "
      "routinely: use tool_inspect_ui first, and fall back to a screenshot only "
      "for genuinely visual information it cannot give you. No arguments.",
      "{\"type\":\"object\",\"properties\":{},\"required\":[]}"};
}

agent_imgui::ToolResult RunScreenshotTool(CaptureBridge& bridge,
                                          const std::string& /*json_args*/) {
  std::string png;
  {
    std::unique_lock<std::mutex> lk(bridge.mu);
    bridge.requested = true;
    bridge.done = false;
    bridge.png.clear();
    if (!bridge.cv.wait_for(lk, std::chrono::seconds(20),
                            [&] { return bridge.done; })) {
      bridge.requested = false;
      return "Screenshot timed out.";
    }
    png = std::move(bridge.png);
  }
  if (png.empty()) return "Screenshot failed.";
  agent_imgui::ToolResult r;
  r.text = "Screenshot of the current UI (PNG attached).";
  r.images.push_back(agent_imgui::ToolImage{"image/png", Base64(png)});
  return r;
}

}  // namespace example
