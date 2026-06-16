// agent_imgui example: a portable SDL3 + SDL_GPU host for the LLM agent.
//
//   agent_imgui_example <prompt-file> [--model ID] [--key-file PATH]
//                       [--timeout SECONDS] [--headless | --window]
//
// Windowed (default): opens a window, shows a full-screen Dear ImGui demo window
// and a small "Agent" panel you can chat in. Headless (--headless): runs the
// prompt once, prints the reply, and exits -- no window or GPU needed.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "agent_imgui/llm_claude.h"
#include "agent_imgui/llm_panel.h"
#include "agent_imgui/ui_agent.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // for SDL_SetMainReady (SDL.h does not include this)

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string Trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

struct Args {
  std::string prompt_file;
  std::string model;
  std::string key_file;
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
      "  --timeout SECONDS    max wait for a reply (default 120)\n"
      "  --headless           run the prompt, print the reply, exit (no window)\n"
      "  --window             force windowed mode (default)\n",
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
    if (s == "-h" || s == "--help") {
      a.help = true;
    } else if (s == "--headless") {
      a.headless = true;
    } else if (s == "--window") {
      a.headless = false;
    } else if (s == "--model") {
      if (!value(a.model)) return false;
    } else if (s == "--key-file") {
      if (!value(a.key_file)) return false;
    } else if (s == "--timeout") {
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

// The API key, from --key-file or else ANTHROPIC_API_KEY. Sets `err` only when a
// key file was given but couldn't be read.
std::string ResolveKey(const Args& a, std::string& err) {
  if (!a.key_file.empty()) {
    const std::string k = Trim(ReadFile(a.key_file));
    if (k.empty()) err = "could not read API key from " + a.key_file;
    return k;
  }
  return agent_imgui::ClaudeProvider::KeyFromEnv();
}

// Point the agent at Claude with the given key/model. With no key, the agent
// keeps its default provider (mock unless ANTHROPIC_API_KEY is set).
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
    if (!it->thinking.empty()) {
      std::fprintf(stderr, "[thinking]\n%s\n\n", it->thinking.c_str());
    }
    const bool is_error = it->text.rfind("[error]", 0) == 0;
    std::fprintf(is_error ? stderr : stdout, "%s\n", it->text.c_str());
    return is_error ? 1 : 0;
  }
  std::fprintf(stderr, "error: no reply\n");
  return 1;
}

int RunWindowed(const Args& a, const std::string& prompt) {
  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 4;
  }
  SDL_Window* window = SDL_CreateWindow(
      "agent_imgui example", 1280, 800,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 4;
  }
  SDL_GPUDevice* gpu = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
          SDL_GPU_SHADERFORMAT_METALLIB,
      /*debug_mode=*/false, /*name=*/nullptr);
  if (!gpu) {
    std::fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }
  if (!SDL_ClaimWindowForGPUDevice(gpu, window)) {
    std::fprintf(stderr, "SDL_ClaimWindowForGPUDevice failed: %s\n",
                 SDL_GetError());
    SDL_DestroyGPUDevice(gpu);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForSDLGPU(window);
  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = gpu;
  init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu, window);
  init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init(&init_info);

  agent_imgui::UiAgent agent;
  agent_imgui::LlmPanel panel;
  std::string key_err;
  ApplyProvider(agent, a, ResolveKey(a, key_err));

  static char input[1 << 14];
  if (!prompt.empty()) {
    std::snprintf(input, sizeof(input), "%s", prompt.c_str());
    agent.Ask(prompt);  // auto-run the prompt that was passed on the command line
  }

  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      }
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    agent.Poll();

    // Full-screen Dear ImGui demo window (starts covering the viewport).
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_FirstUseEver);
    ImGui::ShowDemoWindow(nullptr);

    // A small panel to chat with the agent.
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
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        send = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Send", ImVec2(60, 0))) send = true;
      ImGui::EndDisabled();
      if (busy) {
        ImGui::SameLine();
        ImGui::TextUnformatted("thinking...");
      }
      if (send && input[0] != '\0') {
        agent.Ask(input);
        input[0] = '\0';
      }
      ImGui::Separator();
      panel.Render(agent);
    }
    ImGui::End();

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool minimized =
        draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUTexture* swapchain = nullptr;
    SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, nullptr, nullptr);
    if (swapchain != nullptr && !minimized) {
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
      SDL_GPUColorTargetInfo target = {};
      target.texture = swapchain;
      target.load_op = SDL_GPU_LOADOP_CLEAR;
      target.store_op = SDL_GPU_STOREOP_STORE;
      target.clear_color = SDL_FColor{0.10f, 0.10f, 0.12f, 1.0f};
      SDL_GPURenderPass* pass =
          SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
      SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
  }

  SDL_WaitForGPUIdle(gpu);
  if (agent.busy()) agent.Cancel();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_ReleaseWindowFromGPUDevice(gpu, window);
  SDL_DestroyGPUDevice(gpu);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
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
  if (args.help) {
    Usage(argv[0]);
    return 0;
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
  return RunWindowed(args, prompt);
}
