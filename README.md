# agent_imgui

An LLM **"agent in your [Dear ImGui](https://github.com/ocornut/imgui) app"**
library. It gives a desktop ImGui application a conversational agent that can
*act on the UI*: it talks to an LLM provider, keeps the running conversation, and
drives the real widgets through the [ImGui Test
Engine](https://github.com/ocornut/imgui_test_engine).

A small, portable reference host lives in [`example/`](example/) (SDL3 + SDL_GPU).

## What's in here

| Unit | Purpose |
|------|---------|
| `llm_provider.h` | **Provider seam** — implement this to add an LLM: `LlmProvider`, plus `LlmMessage` / `LlmResult` / `ToolDef` / `ToolExecutor` / `ToolResult`. |
| `http_transport.h` | **Transport seam** — `HttpTransport` + `DefaultHttpTransport()`. The only networking surface; implement it for new platforms (e.g. WASM). |
| `http_transport.cc` | Built-in transports: WinHTTP (Windows) / libcurl (elsewhere). |
| `llm_claude.{h,cc}` | Anthropic Claude provider. |
| `llm_gemini.{h,cc}` | Google Gemini provider. |
| `ui_agent.{h,cc}` | `UiAgent`: routes text to a provider on a worker thread, owns history, slash-commands, tool wiring. |
| `llm_panel.{h,cc}` | `LlmPanel`: renders the conversation inside a host ImGui window. |
| `test_runner.{h,cc}` | Runs the model's UI program through the ImGui Test Engine (the `run_ui_program` actuator). |
| `source_search.{h,cc}` | `GrepSource`: the `grep_source` tool, a substring grep over the host app's sources + model dir. |
| `system_prompt.md` | A default system prompt (editable on disk, hot-reloaded each turn). |

## Dependencies

agent_imgui plugs into the **host application's** Dear ImGui build rather than
vendoring its own. The host project must define these CMake targets *before*
pulling agent_imgui in:

- `dear_imgui` — Dear ImGui (the panel renders with it).
- `imgui_test_engine` — Dear ImGui Test Engine (drives the widgets).

This keeps a single copy of ImGui in the final binary. (When built standalone —
this repo as the top-level CMake project — it fetches matching versions of both
itself; see `cmake/dependencies.cmake`.)

The LLM providers work on **Windows, Linux and macOS**: HTTPS uses WinHTTP on
Windows and **libcurl** elsewhere. libcurl ships with macOS; on Linux install
its development headers (e.g. `libcurl4-openssl-dev`). If libcurl isn't found the
library still builds, but the network providers return an error at call time.

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

agent_imgui::UiAgent agent;     // defaults to Claude (uses ANTHROPIC_API_KEY)
agent_imgui::LlmPanel panel;
// each frame: agent.Poll();  panel.Render(agent);
// on submit:  agent.Ask(user_text);
```

## Extending agent_imgui

Two small, independent seams — you usually touch one and ignore the other.

### Add an LLM — the provider seam (`llm_provider.h`)

Subclass `LlmProvider` and implement one method:

```cpp
class MyProvider : public agent_imgui::LlmProvider {
 public:
  agent_imgui::LlmResult Send(
      const std::string& system,
      const std::vector<agent_imgui::LlmMessage>& messages,
      const std::vector<agent_imgui::ToolDef>& tools,
      const agent_imgui::ToolExecutor& exec,
      const agent_imgui::ProgressCallback& on_thinking) override {
    // 1. turn (system, messages, tools) into a request
    // 2. send it (use an HttpTransport, below)
    // 3. while the model calls tools: run exec(name, json_args), feed results back
    // 4. return {ok=true, text=...}  or  {ok=false, error=...}
  }
  const char* name() const override { return "mine"; }
};
agent.set_provider(std::make_unique<MyProvider>(/* ... */));
```

Start minimal — ignore `tools`/`exec` and just return `{ok=true, text=...}`. Add
tool-calling later; `ClaudeProvider` / `GeminiProvider` show the full loop.

Tools are registered on the agent. A tool returns a `ToolResult` (implicitly
from a string) and may attach images for multimodal models:

```cpp
agent.set_tools(my_tools,
    [](const std::string& name, const std::string& json_args)
        -> agent_imgui::ToolResult {
      if (name == "do_thing")   return "done";          // text result
      if (name == "screenshot") {                        // multimodal result
        agent_imgui::ToolResult r{"here it is"};
        r.images.push_back({"image/png", base64_png});
        return r;
      }
      return "unknown tool";
    });
```

### Use a different network backend — the transport seam (`http_transport.h`)

Networking is one tiny interface, with no LLM knowledge:

```cpp
struct HttpResponse { bool ok; int status; std::string body; std::string error; };
class HttpTransport {
  virtual HttpResponse Post(const std::string& url,
                            const std::vector<std::string>& headers,
                            const std::string& body) = 0;
};
```

`DefaultHttpTransport()` returns the built-in WinHTTP/libcurl backend. To run
where those don't reach — e.g. a Dear ImGui app compiled to **WebAssembly** —
implement `HttpTransport` (over emscripten fetch, a host callback, ...) and hand
it to a provider:

```cpp
auto transport = std::make_shared<MyWasmTransport>();
agent.set_provider(std::make_unique<agent_imgui::ClaudeProvider>(key, transport));
```

## Recording & replaying ops

Every UI program the agent runs (`run_ui_program` → `TestRunner::Run`) is a list
of **ops** (`item_click`, `set_float`, `combo_select`, ...). `TestRunner` records
them as they run, so a sequence can be captured and replayed later — to build a
one-click macro, or to re-watch what the agent did with no model call.

```cpp
test_runner.GetRecording();   // {"ops":[...]} of everything run so far ("" if none)
test_runner.ClearRecording(); // start a fresh recording
test_runner.Replay(ops_json, agent_imgui::TestRunner::Speed::kNormal);  // human-paced
test_runner.set_recording(false);  // opt out (recording is on by default)
```

Recording is continuous and append-only, so the ops are available **whenever** —
including after a prompt is cancelled (whatever the agent had already dispatched
is in the recording). `Replay()` plays at a watchable speed (`kNormal`/
`kCinematic` animate the cursor; `kFast` teleports) and is **not** itself
recorded.

Two consumers use this:

- **A host "record" command** (e.g. a `/record <label>` the host wires up): grab
  the ops collected so far with `GetRecording()`, bind them to a button that
  calls `Replay()` at human speed, then `ClearRecording()` for the next one.
- **`--record FILE` / `--replay FILE`** (the example app): `--record` writes the
  recorded ops next to the screenshot; `--replay FILE` opens a window and plays
  an ops file back at human speed with no model involved (also what the eval
  runner emits, so any eval can be re-watched).

### How robust are ops to the initial state?

Replaying applies the ops to **whatever state the UI is in now**, so they
reproduce the *deltas* from there. The golden rule: **start the app in the same
UI state it was in when recording began** (the easiest baseline is a
freshly-launched window — which is exactly what `--replay` and the status-bar
button assume).

What this means in practice:

- **Scroll position — robust.** The Test Engine scrolls a target item into view
  before acting, so scrollbars do not need to be where they were.
- **Closed sections / tree nodes — robust when referenced by path.** Ops that
  target an item by its ImGui path auto-open any collapsing headers / tree nodes
  on the way (and `item_open` is idempotent), so a section being collapsed at the
  start is fine. Ops that use a raw numeric `id` (from `inspect_ui`) are **not**
  self-opening — an id only resolves while that item is actually being rendered.
- **Which windows/panels are open — NOT automatic.** An op that targets a widget
  in a window that isn't open is skipped. This is fine if an earlier op in the
  recording opened that window, but if the window was already open *before*
  recording started (so no op opens it), replaying from a state where it's closed
  will miss everything inside it.
- **Tabs / data-dependent ids — fragile.** A widget on a non-active tab, or whose
  id depends on loaded data / list contents that differ from record time, may not
  be found. Bring the UI to the same starting point first.

A missing target is skipped (logged to stderr), never fatal — the remaining ops
still run — so a replay degrades gracefully rather than hanging.

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

See [Design](#design) for the architecture and [Widget guidelines](#widget-guidelines)
for how to write agent-friendly widgets.

## Example app (optional)

[`example/`](example/) is a small, portable host/testbed built on **SDL3 +
SDL_GPU** (Windows / Linux / macOS). It is **fully separate from the library**:
everything it needs (SDL3, `stb_image_write`) is fetched inside `example/`, and
it's gated by the `AGENT_IMGUI_BUILD_EXAMPLE` option (ON only when this repo is
the top-level project, OFF when embedded). Integrators who just link the
`agent_imgui` library can ignore this whole directory.

It shows a full-screen Dear ImGui demo window and an "Agent" panel you can chat
in, wires up the agent's UI tools (`inspect_ui`, `run_ui_program`, `screenshot`),
and can run the agent headless. Build it (standalone build does so automatically,
fetching SDL3, Dear ImGui and the Test Engine):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target agent_imgui_example
```

```
agent_imgui_example <prompt-file> [options]
  <prompt-file>      file whose contents are sent as the prompt (positional)
  --model ID         model alias/id (opus|sonnet|haiku or claude-...)
  --key-file PATH    file containing the API key (else ANTHROPIC_API_KEY)
  --timeout SECONDS  max wait for the agent (default 120)
  --headless         run the prompt, print the reply, and exit (no window/GPU)
  --screenshot PATH  offscreen: run the prompt, save a final PNG, and exit
  --window           interactive window (default)
```

```sh
# Headless one-shot (no window needed):
./build/example/agent_imgui_example prompt.txt --key-file key.txt --model haiku --headless

# Drive the demo UI and save a screenshot of the result:
./build/example/agent_imgui_example prompt.txt --key-file key.txt --screenshot out.png
```

### Evals

[`agent_imgui/eval/`](agent_imgui/eval/) holds a set of small demo-window prompts
and a runner that samples N of them and runs them on M parallel instances of the
app, writing one screenshot per prompt for manual review:

```sh
python agent_imgui/eval/run_evals.py 8 4 \
    --exe build/example/agent_imgui_example --key-file key.txt  # 8 prompts, 4 parallel
```

Each run also writes the recorded ops, so any eval can be replayed in a window
at human speed: `agent_imgui_example --replay out/NNN.ops.json`.

## Widget guidelines

agent_imgui drives the UI through the Dear ImGui Test Engine. The agent learns
the UI by reading source and by calling `inspect_ui`, which lists the on-screen
widgets that report a label as

```
[<Window>]
  <label>  [id=<ImGuiID>]
```

and then acts on widgets by their exact `ImGuiID`. Two authoring rules keep your
widgets operable by the agent (and, as a bonus, give you robust test ids). Both
come from real failures while wiring up the agent.

### 1. Give interactive controls a descriptive `###<Name>` id

An ImGui widget's id text is whatever you pass as its label, with everything up
to and including the last `###` removed (`###` resets the id hash). For the test
engine and `inspect_ui`, that id text is the ONLY human-readable handle on the
widget — it is the truncated `DebugLabel` the engine reports.

So an icon-only button such as

```cpp
ImGui::Button(ICON_FA_CARET_UP);            // BAD: id text is the icon's bytes
```

has an id derived from the icon glyph's raw UTF-8 bytes. `inspect_ui` shows it as
gibberish (or empty), and neither you nor the agent can tell what it does or
build a wildcard ref for it. The agent can still click it *if* it happens to know
the id, but it can't recognise the control in the first place.

Append a self-describing `###<Name>`:

```cpp
ImGui::Button((std::string(ICON_FA_CARET_UP) + "###Next frame").c_str());
//             ^ visible: the icon            ^ id text: "Next frame"
```

The glyph still renders; the id text becomes `Next frame`; `inspect_ui` prints
`Next frame  [id=...]`; and the agent can find and operate it. Prefer to expose
the *operation the agent will want* as its own self-describing control — e.g. a
"Go to start" / "Oldest frame" button is far easier for the agent to use than
asking it to drag a slider to an unknown minimum value.

Rule of thumb: every clickable/editable widget should read like a verb or a name
after its `###`. Decorative-only widgets (separators, static text) don't need it.

### 2. Keep ids stable — don't let state leak into the id

A widget's identity must not change when its *state* changes, or ImGui treats it
as a brand-new widget every time the state flips (losing focus/active state) and
any stored ref or id the agent captured goes stale.

The trap is putting state-dependent text *before* the `###` (or with no `###` at
all):

```cpp
// BAD: the icon depends on `on`, so the id flips every toggle.
const char* icon = on ? ICON_FA_CHECK_SQUARE_O : ICON_FA_SQUARE_O;
ImGui::Button((std::string(icon) + " " + label).c_str());
```

Anchor the id with a stable `###<key>` so only the *appearance* before it
changes:

```cpp
// GOOD: appearance varies, id == hash of "<label>" stays put.
ImGui::Button((std::string(icon) + " " + label + "###" + label).c_str());
```

The same applies to any label that embeds a value, a frame counter, a row index
from sorted/filtered data, etc. — compute a stable key for the `###` part and let
the volatile text live before it. (`ImGui::SetItemTooltip`, value readouts, and
`%d`-formatted display strings are fine *before* the `###`.)

#### Why this matters here specifically

- `inspect_ui` reports a truncated `DebugLabel` (32 chars). The exact `ImGuiID`
  is always correct, but the agent picks its target by the readable label — so a
  bad/empty label means a control the agent can see but not understand.
- The agent addresses widgets by the `[id=N]` it saw, so a stable id means a ref
  the agent captured this turn is still valid next turn.
- Wildcard refs (`**/<label>`) match an item's label hash and require the item to
  be unique and on screen; a descriptive `###<Name>` makes those refs meaningful
  too.

