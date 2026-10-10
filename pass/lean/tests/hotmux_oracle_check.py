#!/usr/bin/env python3
"""Check the Hotmux oracle harness with local fake tools and stale artifacts."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    runtime = ROOT / 'generated' / 'hotmux_oracle_check'
    runtime.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='harness-', dir=runtime) as temp:
        project = Path(temp)
        script = project / 'pass/lean/scripts/hotmux_oracle.sh'
        script.parent.mkdir(parents=True)
        shutil.copy2(ROOT / 'pass/lean/scripts/hotmux_oracle.sh', script)
        lean = project / 'formal/lean'
        lean.mkdir(parents=True)
        olean = lean / '.lake/build/lib/lean/LeanSemanticPrimitives/Compiler/CompileDesign.olean'
        olean.parent.mkdir(parents=True)
        olean.write_text('stale fixture\n')
        out = project / 'out'
        out.mkdir()
        test_bin = project / 'gtest'
        test_bin.write_text('''#!/bin/bash
set -eu
[[ "$MODE" == missing ]] && exit 0
printf 'import LeanSemanticPrimitives.Compiler.CompileDesign\\n' > "$LEAN_HOTMUX_FIXTURE"
[[ "$MODE" == empty ]] && exit 0
printf 'example : True := by trivial\\n' >> "$LEAN_HOTMUX_FIXTURE"
[[ "$MODE" == unsigned_only ]] && exit 0
cp "$LEAN_HOTMUX_FIXTURE" "$LEAN_HOTMUX_SIGNED_FIXTURE"
''')
        test_bin.chmod(0o755)
        lake = project / 'lake'
        lake.write_text('''#!/bin/bash
set -eu
[[ "$PWD" == "$EXPECTED_LEAN_DIR" ]] || exit 9
if [[ "$1" == build ]]; then
  [[ "$2" == LeanSemanticPrimitives.Compiler.CompileDesign ]] || exit 8
  printf built > "$BUILD_MARKER"
elif [[ "$*" == 'env lean --version' ]]; then
  echo 'fake Lean for harness validation'
elif [[ "$MODE" == sorry ]]; then
  echo 'warning: declaration uses sorry'
elif [[ "$MODE" == fail ]]; then
  exit 1
fi
''')
        lake.chmod(0o755)
        env = os.environ | dict(TEST_BIN=str(test_bin), LAKE=str(lake), OUT=str(out),
                                EXPECTED_LEAN_DIR=str(lean), BUILD_MARKER=str(out / 'built'))
        for mode in ('missing', 'empty', 'unsigned_only', 'sorry', 'fail', 'valid'):
            for name in ('HotmuxOracle.lean', 'HotmuxSignedOracle.lean'):
                (out / name).write_text('example : True := by trivial\n')
            proc = subprocess.run(['bash', str(script)], env=env | {'MODE': mode},
                                  text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=20)
            assert (proc.returncode == 0) == (mode == 'valid'), (mode, proc.stdout)
            if mode != 'valid':
                assert '[hotmux-oracle] PASS:' not in proc.stdout, proc.stdout
            if mode == 'missing':
                assert 'no fixture produced' in proc.stdout, proc.stdout
        assert (out / 'built').read_text() == 'built', 'Lake freshness check was skipped'
    print('6 Hotmux oracle harness checks passed')


if __name__ == '__main__':
    main()
