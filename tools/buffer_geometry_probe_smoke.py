#!/usr/bin/env python3
"""Malformed diagnostic input must fail cleanly, before JSON/native assertions."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    binary = Path(sys.argv[1]).resolve()
    cases = [([], 'JSON object'),
             ({'cases': [], 'joint_checkpoint_validation': {}}, 'joint validation requires'),
             ({'cases': [None]}, 'missing geometry-probe field'),
             ({'cases': [{'name': 'empty', 'core': [0, 0, 10, 10], 'terminals': []}]}, 'terminals must include')]
    for i, (value, message) in enumerate(cases):
        config = root / f'{i}.json'
        config.write_text(json.dumps(value))
        result = subprocess.run([binary, config], text=True, capture_output=True)
        assert result.returncode == 1, (result.returncode, result.stderr)
        assert message in result.stderr, result.stderr
