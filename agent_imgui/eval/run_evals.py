#!/usr/bin/env python3
"""Sample N eval prompts and run them on M parallel instances of the example app,
writing one screenshot (and recorded ops file) per prompt for manual review.

Portable (Windows / macOS / Linux), standard library only.

    python run_evals.py [N] [M] --exe PATH --key-file PATH [options]

      N   number of prompts to sample (default 8)
      M   number of parallel app instances (default 4)

Options may also come from the environment: EXE, KEY_FILE, MODEL, TIMEOUT,
PROMPTS_DIR, OUT, SEED.

Each run writes, in OUT (default ./out): NNN.png (final screenshot),
NNN.ops.json (replayable ops), NNN.log (output) and NNN_input.md (the prompt).
Replay any of them with:  agent_imgui_example --replay out/NNN.ops.json

Each run makes real Anthropic API calls, billed to the key file.
"""

import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    here = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(
        description="Run a sample of agent_imgui evals in parallel.")
    p.add_argument("n", nargs="?", type=int, default=8,
                   help="number of prompts to sample (default 8)")
    p.add_argument("m", nargs="?", type=int, default=4,
                   help="number of parallel app instances (default 4)")
    p.add_argument("--exe", default=os.environ.get("EXE"),
                   help="path to the built agent_imgui_example (or set EXE)")
    p.add_argument("--key-file", default=os.environ.get("KEY_FILE"),
                   help="file containing the API key (or set KEY_FILE)")
    p.add_argument("--model", default=os.environ.get("MODEL", "sonnet"))
    p.add_argument("--timeout", type=float,
                   default=float(os.environ.get("TIMEOUT", "90")))
    p.add_argument("--prompts-dir",
                   default=os.environ.get("PROMPTS_DIR",
                                          str(here / "claude_sonnet_4_6")))
    p.add_argument("--out", default=os.environ.get("OUT", str(here / "out")))
    p.add_argument("--seed", type=int,
                   default=(int(os.environ["SEED"])
                            if os.environ.get("SEED") else None),
                   help="seed for reproducible sampling")
    args = p.parse_args()

    if not args.exe:
        sys.exit("error: pass --exe or set EXE=path/to/agent_imgui_example")
    if not args.key_file:
        sys.exit("error: pass --key-file or set KEY_FILE=path/to/key")
    if not Path(args.key_file).is_file():
        sys.exit(f"error: key file not found: {args.key_file}")

    prompts = sorted(Path(args.prompts_dir).glob("*_input.md"))
    if not prompts:
        sys.exit(f"error: no *_input.md prompts in {args.prompts_dir}")

    sample = random.Random(args.seed).sample(prompts, min(args.n, len(prompts)))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    print(f"running {len(sample)} prompt(s) on {args.m} parallel instance(s) "
          f"-> {out}", flush=True)

    def run_one(prompt: Path) -> str:
        base = prompt.name[:-len("_input.md")]
        shutil.copyfile(prompt, out / f"{base}_input.md")
        cmd = [args.exe, str(prompt),
               "--key-file", args.key_file, "--model", args.model,
               "--screenshot", str(out / f"{base}.png"),
               "--record", str(out / f"{base}.ops.json"),
               "--timeout", str(args.timeout)]
        with open(out / f"{base}.log", "wb") as log:
            try:
                rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.timeout + 30).returncode
            except subprocess.TimeoutExpired:
                rc = -1
        status = "ok  " if rc == 0 else "FAIL"
        return f"{status} {base} : {prompt.read_text().strip()}"

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.m) as ex:
        for line in ex.map(run_one, sample):
            print(line, flush=True)

    print(f"done. screenshots + ops in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
