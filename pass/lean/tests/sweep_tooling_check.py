#!/usr/bin/env python3
"""Check recovered sweep drivers with local fake tools, never benchmark inputs."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    runtime = ROOT / 'generated' / 'sweep_tooling_check'
    runtime.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix='drivers-', dir=runtime) as temp:
        project = Path(temp)
        scripts = project / 'scripts'
        scripts.mkdir()
        for name in ('run_vc_sweep.sh', 'run_cva6_vc_sweep.sh', 'elab_cva6_wrappers.sh'):
            shutil.copy2(ROOT / 'scripts' / name, scripts / name)
        runner = '#!/bin/bash\nexit "${FAKE_EXIT:-0}"\n'
        for name in ('run_coreet_module_lean.sh', 'run_cva6_module_lean_stress.sh'):
            f = scripts / name
            f.write_text(runner)
            f.chmod(0o755)
        queue = scripts / 'run_lean_queue.sh'
        queue.write_text('#!/bin/bash\nprintf called > "$QUEUE_MARKER"\n')
        queue.chmod(0o755)
        module_list = project / 'modules.txt'
        module_list.write_text('sample\n')
        env = os.environ.copy()
        env.update(FAKE_EXIT='9', EMIT_TIMEOUT='10', QUEUE_MARKER=str(project / 'queued'))

        def invoke(script, args=(), **settings):
            e = env | settings
            proc = subprocess.run(['bash', str(scripts / script), *map(str, args)],
                                  env=e, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=20)
            assert proc.returncode == 0, proc.stdout

        out = project / 'generated' / 'core'
        for branch in ('mod', 'legacy'):
            d = out / branch / 'sample' / 'lean'
            d.mkdir(parents=True)
            (d / 'sample_Lgraph.lean').write_text('-- stale fixture\n')
        invoke('run_vc_sweep.sh', (module_list, 1), PHASE='emit', VC_OUTDIR=str(out))
        assert 'NO_EMIT' in (out / 'emit.tsv').read_text()
        invoke('run_vc_sweep.sh', (module_list, 1), PHASE='legacy', VC_OUTDIR=str(out))
        assert 'NO_EMIT' in (out / 'legacy_emit.tsv').read_text()
        results.append('CORE-ET failed exports cannot reuse stale artifacts in either mode')
        invoke('run_vc_sweep.sh', (module_list, 1), PHASE='emit', VC_OUTDIR=str(out), FAKE_EXIT='0')
        assert '\tEMITTED\t' in (out / 'emit.tsv').read_text()
        results.append('CORE-ET successful export remains accepted')

        (out / 'emit.tsv').write_text('module\temit\tsources\tnodes\tflops\tmemories\nsample\tEMITTED\t1\t1\t0\t0\n')
        (out / 'legacy_emit.tsv').write_text('module\temit\n sample\tEMITTED\n'.replace(' sample', 'sample'))
        header = 'module\tnodes\tflops\tmax_w\twall_s\tpeak_rss_kb\texit\taxioms\tverdict\n'
        for verdict, axioms in (('AXIOM-GATE-FAIL(0/1)', '0/1'), ('PROVEN', '1/1')):
            (out / 'prove.tsv').write_text(header + f'sample\t1\t0\t8\t1\t100\t0\t{axioms}\t{verdict}\n')
            invoke('run_vc_sweep.sh', (module_list, 1), PHASE='report', VC_OUTDIR=str(out))
            actual = (out / 'sweep.tsv').read_text().splitlines()[1].split('\t')[1]
            assert (actual == 'PROVEN') == (verdict == 'PROVEN'), actual
        results.append('CORE-ET report requires the proof queue verdict, not just exit zero')

        out = project / 'generated' / 'cva6'
        wrappers = project / 'wrappers'
        wrappers.mkdir()
        (wrappers / 'sample_gate.sv').write_text('// fixture\n')
        for d in (out / 'mod' / 'sample' / 'lean', out / 'lean'):
            d.mkdir(parents=True)
            (d / 'sample_gate_Lgraph.lean').write_text('-- stale fixture\n')
        invoke('run_cva6_vc_sweep.sh', (module_list, 1), WRAP_DIR=str(wrappers), VC_OUTDIR=str(out))
        assert 'NO_EMIT' in (out / 'emit.tsv').read_text()
        assert not (project / 'queued').exists()
        results.append('CVA6 failed export cannot enter the proof queue through a stale artifact')

        fake_slang = project / 'slang'
        fake_slang.write_text(runner)
        fake_slang.chmod(0o755)
        for rc, expected in (('124', 'FAIL'), ('0', 'OK')):
            result = project / ('elab-' + rc + '.tsv')
            invoke('elab_cva6_wrappers.sh', (wrappers, result, 'unused.f', 1),
                   SLANG=str(fake_slang), FAKE_EXIT=rc)
            assert f'\t{expected}\t' in result.read_text()
        results.append('Wrapper elaboration records silent timeout failure and accepts success')
    (runtime / 'results.json').write_text(json.dumps({'passed': results}, indent=2) + '\n')
    print(f'{len(results)} sweep tooling checks passed')


if __name__ == '__main__':
    main()
