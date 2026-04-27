#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Contract benchmark: prp parse+upass must outperform yosys read_verilog+opt.

Runs both pipelines on equivalent constant-propagation synthetics (a long
chain of `+1` operations that folds to a single constant). Computes the
median wall-clock ratio over N_RUNS and asserts it stays under MAX_RATIO.

Gated to `-c opt` builds via `target_compatible_with` in BUILD — this test
is skipped under fastbuild/dbg because relative perf there is not the
contract we want to enforce.
"""

import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

import constprop_gen

# Synthetic size. Big enough that parse + IR-fold work dominates process
# startup; small enough to keep the bench cheap to run on every -c opt build.
N = 10_000

# Total runs per side. First is discarded as cold-cache warmup; median is
# taken over the remaining runs. 7 keeps the bench fast while giving a
# robust median.
N_RUNS = 7

# Contract: median(T_prp) / median(T_yosys) must stay below this.
#
# Empirically prp runs at ~0.11x yosys on this synthetic (parse + IR-level
# fold of an N=10000 +1 chain). 0.50 gives ~4.5x headroom for cross-machine
# variance and yosys upstream speedups while still enforcing "prp stays at
# least 2x faster than yosys." A failure means either prp regressed or
# yosys got faster — both worth investigating before bumping this number.
MAX_RATIO = 0.50


def runfile(rel: str) -> Path:
    src = os.environ.get("TEST_SRCDIR")
    ws = os.environ.get("TEST_WORKSPACE")
    if not src or not ws:
        raise RuntimeError("TEST_SRCDIR / TEST_WORKSPACE not set; run via bazel test")
    return Path(src) / ws / rel


def time_cmd(cmd: list[str], stdin_bytes: bytes | None, env: dict[str, str]) -> float:
    t0 = time.monotonic_ns()
    res = subprocess.run(
        cmd,
        input=stdin_bytes,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    dt = (time.monotonic_ns() - t0) / 1e9
    if res.returncode != 0:
        sys.stderr.write(f"command failed: {cmd}\n")
        sys.stderr.write(f"stdout: {res.stdout.decode(errors='replace')}\n")
        sys.stderr.write(f"stderr: {res.stderr.decode(errors='replace')}\n")
        raise SystemExit(2)
    return dt


def measure(label: str, cmd: list[str], stdin_bytes: bytes | None, env: dict[str, str]) -> float:
    samples: list[float] = []
    for i in range(N_RUNS):
        dt = time_cmd(cmd, stdin_bytes, env)
        samples.append(dt)
        print(f"  {label} run {i}: {dt:.4f}s", flush=True)
    # Drop the first as cold-cache warmup.
    body = samples[1:]
    med = statistics.median(body)
    print(f"  {label} median (n={len(body)}): {med:.4f}s", flush=True)
    return med


def main() -> int:
    tmpdir = Path(os.environ.get("TEST_TMPDIR", "/tmp"))
    prp_path = tmpdir / f"bench_{N}.prp"
    v_path = tmpdir / f"bench_{N}.v"

    print(f"Generating synthetics N={N} ...", flush=True)
    constprop_gen.emit_prp(N, prp_path)
    constprop_gen.emit_verilog(N, v_path)

    lgshell = runfile("main/lgshell")
    yosys = runfile("inou/yosys/yosys2")

    for binary in (lgshell, yosys):
        if not binary.is_file() or not os.access(binary, os.X_OK):
            sys.stderr.write(f"binary not found or not executable: {binary}\n")
            return 1

    env = {**os.environ, "HOME": str(tmpdir)}

    # Four measurements: parse-only and parse+constfold on each side. The
    # parse-only baselines isolate frontend cost from IR-level fold cost,
    # so a regression in one stage is diagnosable from the other.
    prp_parse_only = (
        f"inou.prp files:{prp_path} parse_only:true\nquit\n"
    ).encode()
    prp_full = (
        f"inou.prp files:{prp_path} |> pass.lnastfmt "
        f"|> pass.upass constprop:1 verifier:false max_iters:1\nquit\n"
    ).encode()
    yosys_parse_cmd = [
        str(yosys), "-Q", "-T", "-q",
        "-p", f"read_verilog {v_path}",
    ]
    yosys_full_cmd = [
        str(yosys), "-Q", "-T", "-q",
        "-p", f"read_verilog {v_path}; opt",
    ]

    print("\n[prp parse] inou.prp parse_only:true", flush=True)
    t_prp_parse = measure("prp_parse", [str(lgshell)], prp_parse_only, env)

    print("\n[prp full] inou.prp |> pass.lnastfmt |> pass.upass constprop:1", flush=True)
    t_prp_full = measure("prp_full", [str(lgshell)], prp_full, env)

    print("\n[yosys parse] read_verilog", flush=True)
    t_yosys_parse = measure("yosys_parse", yosys_parse_cmd, None, env)

    print("\n[yosys full] read_verilog ; opt", flush=True)
    t_yosys_full = measure("yosys_full", yosys_full_cmd, None, env)

    ratio_parse = t_prp_parse / t_yosys_parse
    ratio_full = t_prp_full / t_yosys_full
    print(
        f"\nresult:"
        f"\n  parse-only:  T_prp={t_prp_parse:.4f}s T_yosys={t_yosys_parse:.4f}s ratio={ratio_parse:.3f}"
        f"\n  full:        T_prp={t_prp_full:.4f}s T_yosys={t_yosys_full:.4f}s ratio={ratio_full:.3f}"
        f"\n  contract:    full ratio must be < {MAX_RATIO}",
        flush=True,
    )

    if ratio_full > MAX_RATIO:
        print(
            f"FAIL: full ratio {ratio_full:.3f} exceeds contract MAX_RATIO={MAX_RATIO}",
            flush=True,
        )
        return 1

    print("PASS: prp/yosys full ratio within contract", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
