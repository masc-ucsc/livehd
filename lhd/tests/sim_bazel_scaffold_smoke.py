#!/usr/bin/env python3
"""Check the standalone sim workspace against LiveHD's toolchain settings."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def rc_options(path):
    return {
        (words[0], option)
        for line in path.read_text().splitlines()
        if (words := shlex.split(line, comments=True))
        for option in words[1:]
    }


def main():
    lhd, module, rc, version = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as work:
        work = Path(work)
        source = work / "counter.prp"
        source.write_text("""mod counter(clock:Clock, reset:Reset, en:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  if en { wrap count += 1 }
  value = count
}
mod nested_counter(clock:Clock, reset:Reset, en:Bool) -> (value:U32@[0]) {
  mut sum:U32 = 0
  for i in 0..<2 {
    for j in 0..<3 {
      const lane = counter(clock=clock, reset=reset, en=en)
      sum = sum + lane
    }
  }
  value = sum
}
test nested_counter.counts {
  mut dut = nested_counter
  tick 4 { dut.en = true; step }
}
""")
        subprocess.run(
            [str(lhd.resolve()), "sim", str(source), "--setup-only",
             "--set", "sim.tune.profile=off", "--workdir", str(work / "run")],
            check=True,
        )
        sim = work / "run" / "sim"
        assert (sim / ".bazelversion").read_text() == version.read_text()
        generated = (sim / "MODULE.bazel").read_text()
        for name in ("rules_cc", "platforms", "apple_support"):
            pattern = rf'bazel_dep\(name = "{name}", version = "([^"]+)"'
            expected = re.search(pattern, module.read_text()).group(1)
            actual = re.search(pattern, generated).group(1)
            assert actual == expected, (name, actual, expected)
        expected_options = {
            (config, option.replace("//tools:command_line_tools", "//:command_line_tools"))
            for config, option in rc_options(rc)
            if config in ("build:macos", "build:darwin")
        }
        actual_options = rc_options(sim / ".bazelrc")
        assert expected_options <= actual_options, expected_options - actual_options
        assert ("build", "--enable_platform_specific_config") in actual_options
        assert ("build", "--cxxopt=-std=c++23") in actual_options
        build = (sim / "BUILD").read_text()
        assert 'xcode_config(name = "command_line_tools")' in build
        assert '@build_bazel_apple_support//xcode:xcode_config.bzl' in build
        assert 'name = "drv"' in build
        for iface in sim.glob("*.iface.json"):
            body = iface.name.removesuffix(".iface.json") + ".cpp"
            assert f'"{body}"' in build, f"generated module omitted from BUILD: {body}"
    print("PASS: standalone simulator Bazel toolchain matches LiveHD")


if __name__ == "__main__":
    main()
