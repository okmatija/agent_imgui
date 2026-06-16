#!/usr/bin/env bash
#
# Sample N eval prompts and run them on M parallel instances of the example app,
# writing one final screenshot per prompt for manual verification.
#
#   EXE=path/to/agent_imgui_example KEY_FILE=path/to/key \
#       run_evals.sh [N] [M]
#
#   N   number of prompts to sample (default: 8)
#   M   number of parallel app instances (default: 4)
#
# Environment:
#   EXE         path to the built agent_imgui_example (required)
#   KEY_FILE    file containing the API key (required)
#   MODEL       model alias/id (default: sonnet)
#   TIMEOUT     per-run timeout in seconds (default: 90)
#   PROMPTS_DIR folder of NNN_input.md prompts (default: ./claude_sonnet_4_6)
#   OUT         output directory (default: ./out)
#   SEED        if set, makes the random sample reproducible
#
# Each run writes OUT/NNN.png (final screenshot), OUT/NNN.log (stderr) and
# OUT/NNN_input.md (the prompt) so screenshots are easy to eyeball against asks.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
n="${1:-8}"
m="${2:-4}"
model="${MODEL:-sonnet}"
timeout_s="${TIMEOUT:-90}"
prompts_dir="${PROMPTS_DIR:-$here/claude_sonnet_4_6}"
out="${OUT:-$here/out}"

die() { echo "error: $*" >&2; exit 2; }
[[ -n "${EXE:-}" ]]       || die "set EXE=path/to/agent_imgui_example"
[[ -x "${EXE}" ]]         || die "EXE '$EXE' is not executable"
[[ -n "${KEY_FILE:-}" ]]  || die "set KEY_FILE=path/to/key"
[[ -f "${KEY_FILE}" ]]    || die "KEY_FILE '$KEY_FILE' not found"
[[ -d "$prompts_dir" ]]   || die "prompts dir '$prompts_dir' not found"

mkdir -p "$out"

# Sample N prompts (reproducible if SEED is set, else random).
if [[ -n "${SEED:-}" ]]; then
  mapfile -t sample < <(ls "$prompts_dir"/*_input.md \
      | awk -v s="$SEED" 'BEGIN{srand(s)}{print rand()"\t"$0}' \
      | sort | cut -f2- | head -n "$n")
else
  mapfile -t sample < <(ls "$prompts_dir"/*_input.md | shuf | head -n "$n")
fi
[[ "${#sample[@]}" -gt 0 ]] || die "no *_input.md prompts in $prompts_dir"

echo "running ${#sample[@]} prompt(s) on $m parallel instance(s) -> $out"

run_one() {
  local prompt="$1"
  local base; base="$(basename "$prompt" _input.md)"
  cp "$prompt" "$out/${base}_input.md"
  if "$EXE" "$prompt" --key-file "$KEY_FILE" --model "$model" \
        --screenshot "$out/${base}.png" --timeout "$timeout_s" \
        >"$out/${base}.log" 2>&1; then
    echo "ok   $base : $(cat "$prompt")"
  else
    echo "FAIL $base (see $out/${base}.log) : $(cat "$prompt")"
  fi
}
export -f run_one
export EXE KEY_FILE model timeout_s out

printf '%s\0' "${sample[@]}" | xargs -0 -P "$m" -I{} bash -c 'run_one "$@"' _ {}

echo "done. screenshots in $out (open them next to the *_input.md prompts)."
