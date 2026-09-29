#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
python3 - <<'PY'
import json
import os
from pathlib import Path
import shutil
import subprocess

lhd = Path('lhd/lhd').resolve()
work = Path(os.environ['TEST_TMPDIR']) / 'tuple-register'
fixture = Path('inou/prp/tests/equiv/generic_tuple_register.prp')
# Retain the packed/scalar proof independently of the field-named statematch golden.
golden = (fixture.parent / 'generic_tuple_register/packed.v').resolve()
helper = fixture.parent / 'generic_tuple_register/cell.prp'

def run(label, source, expected):
    result = work / (label + '.json')
    cmd = [str(lhd), 'lec', '--impl', str(source), '--ref', str(golden),
           '--top', 'generic_tuple_register', '--set', 'formal.lec.hier=false',
           '--workdir', str(work / (label + '-work')), '--result-json', str(result)]
    with (work / (label + '.log')).open('w') as out:
        rc = subprocess.call(cmd, stdout=out, stderr=subprocess.STDOUT)
    payload = json.loads(result.read_text())
    lec = payload.get('lec', {})
    assert lec.get('verdict') == expected, (label, rc, payload)
    if expected == 'proven':
        assert rc == 0 and lec.get('bounded') is False, (label, payload)
    else:
        assert rc != 0, (label, payload)

for label, edit in [('good', lambda s: s),
                    ('missing_flush', lambda s: s.replace('reset | flush', 'reset')),
                    ('wrong_enable', lambda s: s.replace('enable != 0', 'enable == 0')),
                    ('wrong_reset', lambda s: s.replace('= 0\n', '= 1\n'))]:
    root = work / label
    (root / 'generic_tuple_register').mkdir(parents=True)
    source = root / fixture.name
    shutil.copyfile(fixture, source)
    (root / 'generic_tuple_register/cell.prp').write_text(edit(helper.read_text()))
    run(label, source, 'proven' if label == 'good' else 'refuted')

# A whole exported bundle must retain its specialized tuple types and state.
source = work / 'good' / fixture.name
bundle = 'ln:' + str(work / 'bundle')
subprocess.run([str(lhd), 'compile', str(source),
                '--emit-dir', bundle, '--workdir', str(work / 'export')], check=True)
run('precompiled', bundle, 'proven')
print('PASS: unbounded tuple-register proof, precompiled proof, and three behavioral mutations')
PY
