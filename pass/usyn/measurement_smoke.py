#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Exercise live command trees, refusal archives, and actual cold/warm synthesis."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def worker(mode, directory):
    directory = Path(directory)
    (directory / "root.pid").write_text(str(os.getpid()))
    child = subprocess.Popen([sys.executable, __file__, "leaf", mode, str(directory)],
                             start_new_session=True)
    (directory / "child.pid").write_text(str(child.pid))
    child.wait()


def leaf(mode, directory):
    # Separate session intentionally escapes the root's process group.
    size = 256 if mode == "memory" else 16
    payload = bytearray(size * 1024 * 1024)
    for i in range(0, len(payload), 4096):
        payload[i] = (i // 4096) % 251 + 1
    Path(directory, "ready").write_text(str(os.getpid()))
    time.sleep(0.25 if mode == "success" else 15)


if len(sys.argv) > 1:
    {"worker": worker, "leaf": leaf}[sys.argv[1]](*sys.argv[2:])
    sys.exit(0)


root = Path(tempfile.mkdtemp(prefix="measurement-", dir=os.environ.get("TEST_TMPDIR")))
monitor = str(Path("pass/usyn/measure_synth").resolve())
unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                             start_new_session=True)


def run(label, command, seconds=5, memory=1024, code=0):
    archive = root / label
    result = subprocess.run([monitor, "--archive", str(archive), "--seconds", str(seconds),
                             "--memory-mb", str(memory), "--", *command],
                            capture_output=True, text=True, timeout=seconds + 8)
    assert result.returncode == code, (label, result, (archive / "command.log").read_text())
    report = json.loads((archive / "measurement.json").read_text())
    samples = [json.loads(line) for line in (archive / "samples.jsonl").read_text().splitlines()]
    assert len(samples) == report["samples"] and not report["omitted_sample_rows"], report
    assert report["sampled_peak_bytes"] == max(row["bytes"] for row in samples), report
    assert sum(p["bytes"] for p in report["peak_processes"]) == report["sampled_peak_bytes"], report
    assert report["wall_ms"] >= max(row["ms"] for row in samples), report
    assert unrelated.poll() is None, "monitor killed unrelated process"
    assert unrelated.pid not in [p["pid"] for p in report["peak_processes"]], report
    assert not any(row["truncated"] for row in samples), report
    return report


try:
    for mode, expected in [("success", "completed"), ("timeout", "time_limit"), ("memory", "memory_limit")]:
        directory = root / (mode + "-pids")
        directory.mkdir()
        report = run(mode, [sys.executable, str(Path(__file__).resolve()), "worker", mode, str(directory)],
                     seconds=1 if mode == "timeout" else 5, memory=128 if mode == "memory" else 1024,
                     code=0 if mode == "success" else 1)
        assert report["reason"] == expected, report
        assert report["maximum_processes"] >= 2, report
        assert report["sampled_peak_bytes"] > 16 * 1024 * 1024, report
        if mode != "success":
            assert report["guard_cleanup_complete"] is True, report
            child_pid = int((directory / "child.pid").read_text())
            # A killed grandchild may briefly remain a zombie until init reaps it.
            deadline = time.monotonic() + 2
            while True:
                state = subprocess.run(["/bin/ps", "-o", "stat=", "-p", str(child_pid)],
                                       capture_output=True, text=True).stdout.strip()
                if not state or state.startswith("Z"):
                    break
                assert time.monotonic() < deadline, (child_pid, state, report)
                time.sleep(0.02)
        else:
            assert report["guard_cleanup_complete"] is None, report

    report = run("failed", [sys.executable, "-c", "print('kept failure'); raise SystemExit(7)"], code=1)
    assert report["reason"] == "command_failed" and report["exit_code"] == 7, report
    assert "kept failure" in (root / "failed/command.log").read_text()
    missing = root / "spawn-error"
    result = subprocess.run([monitor, "--archive", str(missing), "--", "/no/such/command"],
                            capture_output=True, timeout=5)
    assert result.returncode == 2 and (missing / "measurement_failure.json").exists(), result
    before = (root / "failed/measurement.json").read_bytes()
    result = subprocess.run([monitor, "--archive", str(root / "failed"), "--", sys.executable, "-c", "pass"],
                            capture_output=True, timeout=5)
    assert result.returncode == 2 and before == (root / "failed/measurement.json").read_bytes()
    assert not (root / "failed/measurement_failure.json").exists()

    directory = root / "interrupt-pids"
    directory.mkdir()
    interrupted = subprocess.Popen([monitor, "--archive", str(root / "interrupt"), "--seconds", "5",
                                    "--memory-mb", "1024", "--", sys.executable,
                                    str(Path(__file__).resolve()), "worker", "interrupt", str(directory)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 3
        while not (directory / "ready").exists():
            assert interrupted.poll() is None and time.monotonic() < deadline
            time.sleep(0.01)
        interrupted.send_signal(signal.SIGTERM)
        output, _ = interrupted.communicate(timeout=5)
        assert interrupted.returncode == 1, output
        report = json.loads((root / "interrupt/measurement.json").read_text())
        assert report["reason"] == "interrupted" and report["guard_cleanup_complete"] is True, report
        assert unrelated.poll() is None
    finally:
        if interrupted.poll() is None:
            interrupted.terminate()
            interrupted.wait(timeout=5)

    source = root / "top.v"
    source.write_text("module top(input a,b, output y); assign y = a & b; endmodule\n")
    args = [str(Path("lhd/lhd").resolve()), "synth", str(source), "--top", "top", "--workdir", str(root / "work"),
            "--set", "synth.mapper=usyn", "--set", "synth.liberty=inou/prp/tests/abc/test.lib",
            "--set", "synth.opentimer=false", "--set", "pass.usyn.support=2", "--set", "pass.usyn.literals=8", "--set", "pass.usyn.series=2", "-q"]
    for label in ("cold", "warm"):
        envelope = root / (label + "-result.json")
        report = run(label, args + ["--result-json", str(envelope)], seconds=20, memory=4096)
        result = json.loads(envelope.read_text())
        assert result["status"] == "pass", result
        assert report["wall_ms"] >= result["synthesis_invocation"]["wall_ms"], report
        assert report["argv"] == args + ["--result-json", str(envelope)], report
    print("measurement smoke passed:", root)
finally:
    if unrelated.poll() is None:
        unrelated.kill()
    unrelated.wait()
