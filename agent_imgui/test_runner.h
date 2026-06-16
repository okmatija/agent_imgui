#ifndef AGENT_IMGUI_TEST_RUNNER_H_
#define AGENT_IMGUI_TEST_RUNNER_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ImGuiTestEngine;
struct ImGuiTest;
struct ImGuiTestContext;

namespace agent_imgui {

// Owns the Dear ImGui Test Engine and runs LLM-authored UI programs through it.
//
// A program is a JSON array of typed ops (item_click, menu_click, set_float,
// ...) whose refs are ImGui item IDs/paths -- the sole actuator from
// LLM_INTEGRATION_DESIGN.md. The model emits the program via the run_ui_program
// tool; Run() enqueues it (safe to call from the LLM worker thread), and the UI
// thread (Pump, called from PostSwap) queues it on the engine, which performs
// the real clicks across frames. So widget mutation always happens on the UI
// thread regardless of who asked.
class TestRunner {
 public:
  // Playback speed for Replay(): kFast teleports the cursor (instant), while
  // kNormal / kCinematic animate it so a human can follow along.
  enum class Speed { kFast, kNormal, kCinematic };

  TestRunner() = default;
  ~TestRunner();

  TestRunner(const TestRunner&) = delete;
  TestRunner& operator=(const TestRunner&) = delete;

  // Creates + starts the engine bound to the current ImGui context. Call after
  // the context exists. Restartable across graphics-mode switches.
  void Start();
  // Stops the engine. Must be called while the ImGui context is still alive.
  void Stop();
  // Destroys the engine. Must be called AFTER ImGui::DestroyContext() (the test
  // engine asserts on the order), i.e. after the owning window is destroyed.
  void Destroy();
  bool started() const { return engine_ != nullptr && running_; }

  // Per-frame tick: drains the program queue (queues a test if the engine is
  // idle) and advances the engine. Call once per frame after rendering.
  void PostSwap();

  // Enqueues a UI program. `json_args` is the run_ui_program arguments object,
  // i.e. {"ops":[ ... ]}. Returns the number of ops parsed. While recording is
  // on (the default) the ops are also appended to the recording. Thread-safe.
  int Run(const std::string& json_args);

  // Replays an op-program for a human to watch: runs at `speed` (kNormal/
  // kCinematic animate the cursor), does NOT add to the recording, and restores
  // the previous run speed once it finishes. Returns the number of ops. The
  // ops reproduce the actions from whatever state the UI is in now -- see the
  // recording/replay notes in the README for how robust that is. Thread-safe.
  int Replay(const std::string& json_args, Speed speed = Speed::kNormal);

  // Op recording. While on (default), Run()'s ops are appended to an internal
  // log so the program "so far" is available even if the prompt that produced it
  // was cancelled. Replay() ops are never recorded.
  void set_recording(bool on) { recording_ = on; }
  bool recording() const { return recording_; }
  // The recorded ops as a run_ui_program argument object ({"ops":[...]}), or ""
  // if nothing has been recorded. Feed it to Run()/Replay() to repeat it, or
  // save it to a file to replay later. Thread-safe.
  std::string GetRecording();
  void ClearRecording();

  // Lists the items currently visible on screen (grouped by window, with their
  // labels), so the agent can confirm what actually opened / find a target.
  // BLOCKS the calling thread until the UI thread runs the gather, so it must
  // NOT be called from the UI thread (use the async agent). Thread-safe.
  std::string Inspect();

  // True when nothing is queued and the engine isn't running a test.
  bool idle();

  // Draws ImGui controls for the engine's playback settings (run speed, mouse
  // speed/wobble, typing/scroll speed), bound to the live engine IO. Call from
  // within an existing window.
  void DrawPlaybackSettings();

 private:
  // Hands a gather result back from the UI thread to a blocked caller.
  struct GatherResult {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    std::string text;
  };
  // A queued unit of work for the (single) registered test to run on the UI
  // thread: either a UI op-program or a "gather visible items" request.
  struct Job {
    bool gather = false;
    std::string payload;  // ops-array JSON for a program job
    std::shared_ptr<GatherResult> result;  // set for gather jobs
    int run_speed = -1;   // >=0: ImGuiTestRunSpeed to set for this job (Replay)
  };

  static void TestFuncThunk(ImGuiTestContext* ctx);
  void Execute(ImGuiTestContext* ctx, const std::string& ops_json);
  void DoGather(ImGuiTestContext* ctx, const std::shared_ptr<GatherResult>& out);
  // Shared body of Run()/Replay(): queues `json_args`, optionally recording its
  // ops and/or overriding the run speed (-1 = leave as-is).
  int Enqueue(const std::string& json_args, bool record, int run_speed);

  ImGuiTestEngine* engine_ = nullptr;
  ImGuiTest* test_ = nullptr;
  bool running_ = false;

  std::mutex mu_;
  std::vector<Job> jobs_;  // pending work (UI thread drains in PostSwap)
  Job running_job_;        // the job the currently-queued test is executing

  bool recording_ = true;            // append Run() ops to recorded_
  std::vector<std::string> recorded_;  // recorded op-object JSON strings
  int saved_speed_ = -1;             // ConfigRunSpeed to restore after a Replay
};

}  // namespace agent_imgui

#endif  // AGENT_IMGUI_TEST_RUNNER_H_
