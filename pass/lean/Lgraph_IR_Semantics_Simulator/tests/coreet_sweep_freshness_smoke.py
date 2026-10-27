#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Run the relocated completion controls in a fresh, hermetic project."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as temporary:
    scripts = Path(temporary) / 'pass/lean/Lgraph_IR_Semantics_Simulator/scripts'
    scripts.mkdir(parents=True)
    for source in sys.argv[1:]:
        shutil.copyfile(source, scripts / Path(source).name)
    subprocess.run(['bash', str(scripts / 'coreet_sweep_freshness_controls.sh')],
                   check=True, timeout=20)
