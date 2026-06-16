# agent_imgui

An LLM **"agent in your [Dear ImGui](https://github.com/ocornut/imgui) app"**
library. It gives a desktop ImGui application a conversational agent that can
*act on the UI*: it talks to an LLM provider, keeps the running conversation, and
drives the real widgets through the [ImGui Test
Engine](https://github.com/ocornut/imgui_test_engine).

It was extracted from [MuJoCo Studio](https://github.com/okmatija/mujoco)
(`src/experimental/studio/llm`), which remains the reference consumer.

## What's in here

| Unit | Purpose |
|------|---------|
| `llm_provider.h` | Provider seam: `LlmProvider`, `LlmResult`, `ToolDef`, `ToolExecutor`. |
| `llm_claude.{h,cc}` | Anthropic Claude provider (WinHTTP transport, Windows-only). |
| `llm_gemini.{h,cc}` | Google Gemini provider (WinHTTP transport, Windows-only). |
| `llm_mock.h` | Deterministic mock provider for tests / headless capture. |
| `ui_agent.{h,cc}` | `UiAgent`: routes text to a provider on a worker thread, owns history, slash-commands, tool wiring. |
| `llm_panel.{h,cc}` | `LlmPanel`: renders the conversation inside a host ImGui window. |
| `test_runner.{h,cc}` | Runs the model's UI program through the ImGui Test Engine. |
| `source_search.{h,cc}` | `GrepSource`: the `grep_source` tool, a substring grep over the host app's sources + model dir. |
| `system_prompt.md` | The agent's system prompt (editable on disk, hot-reloaded each turn). |

## Dependencies

agent_imgui plugs into the **host application's** Dear ImGui build rather than
vendoring its own. The host project must define these CMake targets *before*
pulling agent_imgui in:

- `dear_imgui` — Dear ImGui (the panel renders with it).
- `imgui_test_engine` — Dear ImGui Test Engine (drives the widgets).

This keeps a single copy of ImGui in the final binary. On non-Windows platforms
the network providers compile but return an error at call time (only the WinHTTP
transport is implemented today); `MockProvider` works everywhere.

## Consuming it (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
    agent_imgui
    GIT_REPOSITORY https://github.com/okmatija/agent_imgui.git
    GIT_TAG        <commit-sha>
)
FetchContent_MakeAvailable(agent_imgui)   # after dear_imgui / imgui_test_engine

target_link_libraries(my_app PRIVATE agent_imgui)

# Let the grep_source tool search your app's sources (optional but recommended):
target_compile_definitions(agent_imgui
    PRIVATE AGENT_IMGUI_HOST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
```

Then, from the host code:

```cpp
#include "agent_imgui/ui_agent.h"
#include "agent_imgui/llm_panel.h"

mujoco::studio::UiAgent agent;     // ClaudeProvider if ANTHROPIC_API_KEY is set,
mujoco::studio::LlmPanel panel;    // otherwise MockProvider.
// each frame: agent.Poll();  panel.Render(agent);
// on submit:  agent.Ask(user_text);
```

> Note: the public symbols are still in the `mujoco::studio` namespace from the
> original extraction.

## Configuration

Compile-time (CMake `target_compile_definitions` on the `agent_imgui` target):

- `AGENT_IMGUI_SOURCE_DIR` — set automatically to this repo's source dir so the
  agent can load `system_prompt.md`.
- `AGENT_IMGUI_HOST_SOURCE_DIR` — set by the consumer to the source tree the
  `grep_source` tool should search (defaults to empty = disabled).

Runtime (environment):

- `ANTHROPIC_API_KEY` / `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) — provider keys.
- `AGENT_IMGUI_SYSTEM_PROMPT` — path to a system prompt file that overrides the
  built-in `system_prompt.md`.
- `AGENT_IMGUI_LLM_VERBOSE` — if set, providers log the raw request/response.

See [`agent_imgui/LLM_INTEGRATION_DESIGN.md`](agent_imgui/LLM_INTEGRATION_DESIGN.md)
for the architecture and [`agent_imgui/WIDGET_GUIDELINES.md`](agent_imgui/WIDGET_GUIDELINES.md)
for how to write agent-friendly widgets.
