# Agent eval set

A small, eyeball-able eval for the agent and its system prompt. Each prompt is a
short user request that exercises one piece of the **Dear ImGui demo window**
(the example app's main window). Prompts are organised into **per-model
subfolders** so each is paired with the model it was verified against:

```
eval/
  claude_sonnet_4_6/
    000_input.md     # one user request (a small demo-window task)
    ...
    029_input.md
  run_evals.sh
```

A subfolder name is the model id with `-` written as `_`, so `claude_sonnet_4_6`
runs against `claude-sonnet-4-6`.

## Run

[`run_evals.sh`](run_evals.sh) samples N prompts and runs them on M parallel
instances of the example app, writing one **final screenshot per prompt** for
manual verification:

```bash
EXE=path/to/agent_imgui_example KEY_FILE=path/to/key \
    bash run_evals.sh 8 4          # sample 8 prompts, 4 in parallel
```

Each run produces, in `OUT` (default `eval/out/`):

```
out/
  007.png        # final screenshot of the UI after the agent acted
  007.log        # the run's stderr (tool calls, errors)
  007_input.md   # the prompt, copied alongside for easy comparison
```

Knobs (environment): `MODEL` (default `sonnet`), `TIMEOUT` (default 90s),
`PROMPTS_DIR`, `OUT`, `SEED` (reproducible sampling). The agent is
non-deterministic, so these are samples to eyeball, not pass/fail assertions.

**Each run makes real Anthropic API calls** (a few tool-use round-trips per
prompt), billed to the key file.

## Adding cases

Drop the next `NNN_input.md` into a model folder (one short request per file);
add a model by creating a new `<model_id_with_underscores>/` folder.
