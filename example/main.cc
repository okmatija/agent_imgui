// agent_imgui example: a portable SDL3 + SDL_GPU host for the LLM agent.
//
//   agent_imgui_example <prompt-file> [options]
//     --model ID         model alias/id (opus|sonnet|haiku or claude-...)
//     --key-file PATH    file with the API key (else ANTHROPIC_API_KEY)
//     --timeout SECONDS  max wait for the agent (default 120)
//     --headless         text-only: run the prompt, print the reply, exit
//     --screenshot PATH  offscreen: run the prompt, save a final PNG, exit
//     --window           interactive window (default)
//
// In windowed / screenshot mode the agent can act on the Dear ImGui demo window
// through the ImGui Test Engine (run_ui_program), inspect it (inspect_ui), and
// grab a screenshot for itself (the multimodal `screenshot` tool).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "agent_imgui/llm_claude.h"
#include "agent_imgui/llm_panel.h"
#include "agent_imgui/test_runner.h"
#include "agent_imgui/ui_agent.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // for SDL_SetMainReady (SDL.h does not include this)

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

std::string ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool WriteFile(const std::string& path, const std::string& bytes) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return f.good();
}

std::string Trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string Base64(const std::string& in) {
  static const char* t =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  auto uc = [](char c) { return static_cast<unsigned>(static_cast<unsigned char>(c)); };
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

void SetEnvIfUnset(const char* key, const std::string& value) {
#if defined(_WIN32)
  if (!std::getenv(key)) _putenv_s(key, value.c_str());
#else
  setenv(key, value.c_str(), /*overwrite=*/0);
#endif
}

// A demo-appropriate system prompt, installed (unless the user already set
// AGENT_IMGUI_SYSTEM_PROMPT) so the agent knows it is driving the demo window.
const char* kDemoSystemPrompt =
    "You are an agent embedded in a Dear ImGui application. The main window on "
    "screen is the \"Dear ImGui Demo\" window, which showcases many widgets.\n\n"
    "You act on the UI ONLY by calling run_ui_program: a JSON program of Dear "
    "ImGui Test Engine operations that click/drag/type the real widgets. "
    "Reference items by their ImGui path, e.g. \"//Dear ImGui Demo/Widgets\" or "
    "\"//Dear ImGui Demo/Configuration\". A tree node or header must be opened "
    "(op item_open, or item_click) before its children are reachable.\n\n"
    "Tools:\n"
    "- inspect_ui: lists the items currently on screen with labels/ids. Use it "
    "to discover exact item paths before referencing them.\n"
    "- run_ui_program: performs the actions.\n"
    "- screenshot: returns an image of the current UI. Use it SPARINGLY, only "
    "when you must visually confirm a result you cannot verify from inspect_ui.\n\n"
    "Work in small steps: open the relevant section, inspect_ui to find the "
    "exact item, then run_ui_program. Keep replies to one short sentence.";

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

struct Args {
  std::string prompt_file;
  std::string model;
  std::string key_file;
  std::string screenshot_path;  // non-empty => offscreen eval mode
  std::string record_file;      // non-empty => write the recorded ops here
  std::string replay_file;      // non-empty => replay these ops (no agent)
  double timeout_s = 120.0;
  bool headless = false;
  bool help = false;
};

void Usage(const char* argv0) {
  std::fprintf(stderr,
      "Usage: %s <prompt-file> [options]\n"
      "  <prompt-file>        file whose contents are sent as the prompt\n"
      "  --model ID           model alias/id (opus|sonnet|haiku or claude-...)\n"
      "  --key-file PATH      file containing the API key (else ANTHROPIC_API_KEY)\n"
      "  --timeout SECONDS    max wait for the agent (default 120)\n"
      "  --headless           text-only: run prompt, print reply, exit (no GPU)\n"
      "  --screenshot PATH    offscreen: run prompt, save final PNG to PATH, exit\n"
      "  --record PATH        save the agent's recorded ops (a replayable program)\n"
      "  --replay FILE        replay an ops file in a window at human speed (no agent)\n"
      "  --window             interactive window (default)\n",
      argv0);
}

bool ParseArgs(int argc, char** argv, Args& a, std::string& err) {
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    auto value = [&](std::string& out) -> bool {
      if (i + 1 >= argc) { err = "missing value for " + s; return false; }
      out = argv[++i];
      return true;
    };
    if (s == "-h" || s == "--help") a.help = true;
    else if (s == "--headless") a.headless = true;
    else if (s == "--window") a.headless = false;
    else if (s == "--model") { if (!value(a.model)) return false; }
    else if (s == "--key-file") { if (!value(a.key_file)) return false; }
    else if (s == "--screenshot") { if (!value(a.screenshot_path)) return false; }
    else if (s == "--record") { if (!value(a.record_file)) return false; }
    else if (s == "--replay") { if (!value(a.replay_file)) return false; }
    else if (s == "--timeout") {
      std::string t;
      if (!value(t)) return false;
      a.timeout_s = std::atof(t.c_str());
    } else if (!s.empty() && s[0] == '-') {
      err = "unknown option " + s;
      return false;
    } else if (a.prompt_file.empty()) {
      a.prompt_file = s;
    } else {
      err = "unexpected argument " + s;
      return false;
    }
  }
  return true;
}

std::string ResolveKey(const Args& a, std::string& err) {
  if (!a.key_file.empty()) {
    const std::string k = Trim(ReadFile(a.key_file));
    if (k.empty()) err = "could not read API key from " + a.key_file;
    return k;
  }
  return agent_imgui::ClaudeProvider::KeyFromEnv();
}

void ApplyProvider(agent_imgui::UiAgent& agent, const Args& a,
                   const std::string& key) {
  if (key.empty()) return;
  auto provider = std::make_unique<agent_imgui::ClaudeProvider>(key);
  if (!a.model.empty() && provider->SetModel(a.model).empty()) {
    std::fprintf(stderr, "warning: unknown model '%s'; using %s\n",
                 a.model.c_str(), provider->Model().c_str());
  }
  agent.set_provider(std::move(provider));
}

// ---------------------------------------------------------------------------
// SDL_GPU rendering + screenshot capture.
//
// ImGui is rendered into an offscreen RGBA texture which is then blitted to the
// swapchain for display. The offscreen texture is the single source for both the
// on-screen image and screenshot read-back, so capture works the same whether
// the window is visible, hidden, or never presented.
// ---------------------------------------------------------------------------

struct Renderer {
  SDL_GPUDevice* gpu = nullptr;
  SDL_Window* window = nullptr;
  SDL_GPUTexture* color = nullptr;  // offscreen color target (RGBA8)
  int cw = 0, ch = 0;               // color target size

  static constexpr SDL_GPUTextureFormat kFormat =
      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  bool Init(const char* title, int w, int h, bool hidden) {
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (hidden) flags |= SDL_WINDOW_HIDDEN;
    window = SDL_CreateWindow(title, w, h, flags);
    if (!window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                              SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
    if (!gpu) { std::fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError()); return false; }
    if (!SDL_ClaimWindowForGPUDevice(gpu, window)) {
      std::fprintf(stderr, "SDL_ClaimWindowForGPUDevice: %s\n", SDL_GetError());
      return false;
    }
    if (!hidden) SDL_ShowWindow(window);
    return true;
  }

  void EnsureColor(int w, int h) {
    if (color && cw == w && ch == h) return;
    if (color) SDL_ReleaseGPUTexture(gpu, color);
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = kFormat;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = static_cast<Uint32>(w);
    ci.height = static_cast<Uint32>(h);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    color = SDL_CreateGPUTexture(gpu, &ci);
    cw = w;
    ch = h;
  }

  // Renders `draw_data` to the offscreen texture and blits it to the swapchain.
  void RenderFrame(ImDrawData* draw_data) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) { w = 1; h = 1; }
    EnsureColor(w, h);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    if (!cmd) return;
    if (draw_data) ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);

    // Pass 1: ImGui -> offscreen color texture.
    SDL_GPUColorTargetInfo target{};
    target.texture = color;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.clear_color = SDL_FColor{0.10f, 0.10f, 0.12f, 1.0f};
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    if (draw_data) ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
    SDL_EndGPURenderPass(pass);

    // Pass 2: blit the offscreen texture to the swapchain for display.
    Uint32 sw = 0, sh = 0;
    SDL_GPUTexture* swap = nullptr;
    if (SDL_AcquireGPUSwapchainTexture(cmd, window, &swap, &sw, &sh) && swap) {
      SDL_GPUBlitInfo blit{};
      blit.source.texture = color;
      blit.source.w = static_cast<Uint32>(cw);
      blit.source.h = static_cast<Uint32>(ch);
      blit.destination.texture = swap;
      blit.destination.w = sw;
      blit.destination.h = sh;
      blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
      blit.filter = SDL_GPU_FILTER_LINEAR;
      SDL_BlitGPUTexture(cmd, &blit);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
  }

  // Reads back the offscreen texture and PNG-encodes it. UI/render thread only.
  bool CapturePng(std::string& png) {
    if (!color || cw <= 0 || ch <= 0) return false;
    const Uint32 bytes = static_cast<Uint32>(cw) * static_cast<Uint32>(ch) * 4;
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tci);
    if (!tb) return false;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{};
    region.texture = color;
    region.w = static_cast<Uint32>(cw);
    region.h = static_cast<Uint32>(ch);
    region.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    dst.pixels_per_row = static_cast<Uint32>(cw);
    dst.rows_per_layer = static_cast<Uint32>(ch);
    SDL_DownloadFromGPUTexture(cp, &region, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(gpu, true, &fence, 1);
    SDL_ReleaseGPUFence(gpu, fence);

    bool ok = false;
    if (void* mapped = SDL_MapGPUTransferBuffer(gpu, tb, false)) {
      png.clear();
      ok = stbi_write_png_to_func(
               [](void* ctx, void* data, int size) {
                 static_cast<std::string*>(ctx)->append(
                     static_cast<char*>(data), static_cast<size_t>(size));
               },
               &png, cw, ch, 4, mapped, cw * 4) != 0;
      SDL_UnmapGPUTransferBuffer(gpu, tb);
    }
    SDL_ReleaseGPUTransferBuffer(gpu, tb);
    return ok;
  }

  void Shutdown() {
    if (gpu) SDL_WaitForGPUIdle(gpu);
    if (color) SDL_ReleaseGPUTexture(gpu, color);
    if (gpu && window) SDL_ReleaseWindowFromGPUDevice(gpu, window);
    if (gpu) SDL_DestroyGPUDevice(gpu);
    if (window) SDL_DestroyWindow(window);
  }
};

// Hand-off so the worker-thread `screenshot` tool gets a frame captured by the
// render thread (capture must happen on the thread that owns the GPU device).
struct CaptureBridge {
  std::mutex mu;
  std::condition_variable cv;
  bool requested = false;
  bool done = false;
  std::string png;
};

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

const char* kRunProgramSchema =
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

void RegisterTools(agent_imgui::UiAgent& agent, agent_imgui::TestRunner& runner,
                   CaptureBridge& bridge) {
  using agent_imgui::ToolDef;
  using agent_imgui::ToolImage;
  using agent_imgui::ToolResult;

  ToolDef inspect_ui{
      "inspect_ui",
      "Lists the widgets currently visible on screen, grouped by window, with "
      "their labels and ids. Call it to find exact item paths. No arguments.",
      "{\"type\":\"object\",\"properties\":{},\"required\":[]}"};

  ToolDef run_program{
      "run_ui_program",
      "Drive the UI by running a short program of Dear ImGui Test Engine "
      "operations -- this clicks/drags/types the real widgets. Reference items "
      "by their ImGui path (e.g. //Dear ImGui Demo/Widgets). Open a tree "
      "node/header before touching its children.",
      kRunProgramSchema};

  ToolDef screenshot{
      "screenshot",
      "Returns a PNG screenshot of the current UI for you to look at. Use "
      "SPARINGLY -- only when you must visually confirm something you cannot "
      "verify with inspect_ui. No arguments.",
      "{\"type\":\"object\",\"properties\":{},\"required\":[]}"};

  auto exec = [&runner, &bridge](const std::string& name,
                                 const std::string& json_args) -> ToolResult {
    if (name == "inspect_ui") return runner.Inspect();
    if (name == "run_ui_program") {
      const int n = runner.Run(json_args);
      return "Queued a " + std::to_string(n) + "-op UI program.";
    }
    if (name == "screenshot") {
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
      ToolResult r;
      r.text = "Screenshot of the current UI (PNG attached).";
      r.images.push_back(ToolImage{"image/png", Base64(png)});
      return r;
    }
    return "Unknown tool: " + name;
  };

  agent.set_tools({inspect_ui, run_program, screenshot}, exec);
}

// Services a pending screenshot-tool request once the UI has settled.
void ServiceCaptureRequest(Renderer& r, agent_imgui::TestRunner& runner,
                           CaptureBridge& bridge) {
  std::string png;
  bool capture = false;
  {
    std::lock_guard<std::mutex> lk(bridge.mu);
    capture = bridge.requested && !bridge.done && runner.idle();
  }
  if (!capture) return;
  const bool ok = r.CapturePng(png);
  std::lock_guard<std::mutex> lk(bridge.mu);
  bridge.png = ok ? std::move(png) : std::string();
  bridge.done = true;
  bridge.requested = false;
  bridge.cv.notify_all();
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

int RunHeadless(const Args& a, const std::string& prompt) {
  std::string err;
  const std::string key = ResolveKey(a, err);
  if (!err.empty()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
  if (key.empty()) {
    std::fprintf(stderr,
                 "error: no API key (pass --key-file or set ANTHROPIC_API_KEY)\n");
    return 2;
  }

  agent_imgui::UiAgent agent;
  ApplyProvider(agent, a, key);
  agent.Ask(prompt);

  const auto start = std::chrono::steady_clock::now();
  while (agent.busy()) {
    agent.Poll();
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    if (elapsed > a.timeout_s) {
      agent.Cancel();
      std::fprintf(stderr, "error: timed out after %.0fs\n", a.timeout_s);
      return 3;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  agent.Poll();
  for (auto it = agent.history().rbegin(); it != agent.history().rend(); ++it) {
    if (it->role != "assistant") continue;
    if (!it->thinking.empty())
      std::fprintf(stderr, "[thinking]\n%s\n\n", it->thinking.c_str());
    const bool is_error = it->text.rfind("[error]", 0) == 0;
    std::fprintf(is_error ? stderr : stdout, "%s\n", it->text.c_str());
    return is_error ? 1 : 0;
  }
  std::fprintf(stderr, "error: no reply\n");
  return 1;
}

int RunGui(const Args& a, const std::string& prompt) {
  const bool replay = !a.replay_file.empty();          // play an ops file, no agent
  const bool eval = !a.screenshot_path.empty() && !replay;  // offscreen one-shot

  std::string replay_ops;
  if (replay) {
    replay_ops = Trim(ReadFile(a.replay_file));
    if (replay_ops.empty()) {
      std::fprintf(stderr, "error: empty or unreadable ops file '%s'\n",
                   a.replay_file.c_str());
      return 2;
    }
  }

  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 4;
  }
  Renderer r;
  if (!r.Init("agent_imgui example", 1280, 800, /*hidden=*/eval)) {
    r.Shutdown();
    SDL_Quit();
    return 4;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::GetIO().IniFilename = nullptr;  // no persisted layout: deterministic runs
  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForSDLGPU(r.window);
  ImGui_ImplSDLGPU3_InitInfo init_info{};
  init_info.Device = r.gpu;
  init_info.ColorTargetFormat = Renderer::kFormat;  // we render to our RGBA target
  init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init(&init_info);

  agent_imgui::TestRunner runner;
  runner.Start();
  agent_imgui::UiAgent agent;
  agent_imgui::LlmPanel panel;
  CaptureBridge bridge;
  RegisterTools(agent, runner, bridge);

  std::string key_err;
  ApplyProvider(agent, a, ResolveKey(a, key_err));

  static char input[1 << 14];
  if (replay) {
    // No agent: just play the recorded ops at human speed for the user to watch.
    runner.Replay(replay_ops, agent_imgui::TestRunner::Speed::kNormal);
  } else if (!prompt.empty()) {
    std::snprintf(input, sizeof(input), "%s", prompt.c_str());
    agent.Ask(prompt);
  }

  const auto start = std::chrono::steady_clock::now();
  int settled = 0;
  bool done = false;
  int exit_code = 0;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(r.window)) {
        done = true;
      }
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    agent.Poll();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::ShowDemoWindow(nullptr);
    // Keep the demo window full-screen (ShowDemoWindow sets its own default
    // pos/size internally, so pin ours by name every frame).
    ImGui::SetWindowPos("Dear ImGui Demo", vp->WorkPos, ImGuiCond_Always);
    ImGui::SetWindowSize("Dear ImGui Demo", vp->WorkSize, ImGuiCond_Always);

    if (!eval && !replay) {  // interactive chat panel
      ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Agent")) {
        ImGui::TextDisabled("provider: %s   model: %s",
                            agent.provider_name().c_str(),
                            agent.provider_model().c_str());
        const bool busy = agent.busy();
        bool send = false;
        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(-70.0f);
        if (ImGui::InputText("##input", input, sizeof(input),
                             ImGuiInputTextFlags_EnterReturnsTrue))
          send = true;
        ImGui::SameLine();
        if (ImGui::Button("Send", ImVec2(60, 0))) send = true;
        ImGui::EndDisabled();
        if (busy) { ImGui::SameLine(); ImGui::TextUnformatted("thinking..."); }
        if (send && input[0] != '\0') { agent.Ask(input); input[0] = '\0'; }
        ImGui::Separator();
        panel.Render(agent);
      }
      ImGui::End();
    }

    ImGui::Render();
    r.RenderFrame(ImGui::GetDrawData());
    runner.PostSwap();
    ServiceCaptureRequest(r, runner, bridge);

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();

    if (eval) {
      const bool idle = !agent.busy() && runner.idle() && !prompt.empty();
      settled = idle ? settled + 1 : 0;
      if (settled > 30 || elapsed > a.timeout_s) {
        std::string png;
        if (r.CapturePng(png) && WriteFile(a.screenshot_path, png)) {
          std::fprintf(stderr, "wrote %s\n", a.screenshot_path.c_str());
        } else {
          std::fprintf(stderr, "error: failed to write %s\n",
                       a.screenshot_path.c_str());
          exit_code = 5;
        }
        if (elapsed > a.timeout_s && agent.busy()) {
          std::fprintf(stderr, "warning: timed out after %.0fs\n", a.timeout_s);
        }
        done = true;
      }
    }
  }

  if (agent.busy()) agent.Cancel();

  // Save the agent's recorded ops (a replayable program) if requested.
  if (!a.record_file.empty()) {
    const std::string ops = runner.GetRecording();
    if (ops.empty()) {
      std::fprintf(stderr, "note: no ops were recorded\n");
    } else if (WriteFile(a.record_file, ops)) {
      std::fprintf(stderr, "wrote ops to %s\n", a.record_file.c_str());
    } else {
      std::fprintf(stderr, "error: failed to write %s\n", a.record_file.c_str());
      exit_code = 5;
    }
  }

  SDL_WaitForGPUIdle(r.gpu);
  runner.Stop();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  runner.Destroy();
  r.Shutdown();
  SDL_Quit();
  return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  std::string err;
  if (!ParseArgs(argc, argv, args, err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    Usage(argv[0]);
    return 2;
  }
  if (args.help) { Usage(argv[0]); return 0; }

  // Replay mode needs no prompt, key, or agent: just play an ops file in a window.
  if (!args.replay_file.empty()) {
    if (args.headless) {
      std::fprintf(stderr, "error: --replay needs a window (not --headless)\n");
      return 2;
    }
    return RunGui(args, /*prompt=*/"");
  }

  std::string prompt;
  if (!args.prompt_file.empty()) {
    prompt = Trim(ReadFile(args.prompt_file));
    if (prompt.empty()) {
      std::fprintf(stderr, "error: empty or unreadable prompt file '%s'\n",
                   args.prompt_file.c_str());
      return 2;
    }
  }

  if (args.headless) {
    if (prompt.empty()) {
      std::fprintf(stderr, "error: --headless needs a prompt file\n");
      Usage(argv[0]);
      return 2;
    }
    return RunHeadless(args, prompt);
  }

  if (!args.screenshot_path.empty() && prompt.empty()) {
    std::fprintf(stderr, "error: --screenshot needs a prompt file\n");
    Usage(argv[0]);
    return 2;
  }

  // Install a demo-appropriate system prompt (unless the user set one), so the
  // agent knows it is driving the Dear ImGui demo window.
  {
    const auto p = std::filesystem::temp_directory_path() /
                   "agent_imgui_example_system_prompt.md";
    if (WriteFile(p.string(), kDemoSystemPrompt))
      SetEnvIfUnset("AGENT_IMGUI_SYSTEM_PROMPT", p.string());
  }

  return RunGui(args, prompt);
}
