#!/usr/bin/env python3
"""Fixture-driven writer roundtrips and technology mapping (see abc/README.md)."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile

from lec import run_lec, verdict


def tags(source):
    if source.suffix == '.json':
        return json.loads(source.read_text())
    return dict(re.findall(r'^\s*(?://\s*)?:([\w_]+):\s*(.+)$', source.read_text(), re.M))


def settings(value):
    return [arg for setting in shlex.split(value) for arg in ('--set', setting)]


def run(cmd):
    print('+', shlex.join(map(str, cmd)), flush=True)
    proc = subprocess.Popen(list(map(str, cmd)), start_new_session=True)
    try:
        status = proc.wait(timeout=45)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()
        raise
    if status:
        raise subprocess.CalledProcessError(status, cmd)


def nonempty(path):
    if not path.is_file() or not path.stat().st_size:
        raise RuntimeError('missing or empty output: ' + str(path))


def check(lhd, impl, ref, top, work, extra=(), ref_top=None, strict=False):
    result = run_lec([lhd, 'lec', '--impl', impl, '--ref', ref,
                      '--impl-top', top, '--ref-top', ref_top or top,
                      '--workdir', str(work)] + list(extra), timeout=20 if strict else 5)
    print(result.stdout.decode('utf-8', 'replace'), end='')
    status = verdict(result, sanity=not strict)
    print('LEC check:', status)
    if status == 'failed':
        raise RuntimeError('LEC failed')


def roundtrip(lhd, source, meta, work, mapper='abc'):
    del mapper  # the writer roundtrip never technology-maps
    top = meta.get('top', source.stem)
    out = work / 'pyrope'
    run([lhd, 'compile', source, '--top', top, '--emit-dir', 'pyrope:' + str(out),
         '--workdir', work / 'emit'])
    emitted = sorted(out.glob('*.prp'))
    if not emitted:
        raise RuntimeError('writer emitted no Pyrope')
    for path in emitted:
        nonempty(path)
    # Reload all emitted units and compare actual behavior, not temporary names.
    run([lhd, 'compile', *emitted, '--top', top, '--emit-dir', 'lg:' + str(work / 'impl'),
         '--workdir', work / 'reload'])
    check(lhd, 'lg:' + str(work / 'impl'), str(source), top, work / 'lec', strict=True)


def synth(lhd, source, meta, work, mapper='abc'):
    fixture = source
    source = source.parent / meta['source'] if 'source' in meta else source
    top = meta.get('top', tags(source).get('pyrope_top', source.stem))
    ref = fixture.with_name(fixture.stem + '_ref.v')
    tb = fixture.with_name(fixture.stem + '_tb.v')
    compile_args = settings(meta.get('compile_set', ''))
    for reader in meta.get('readers', 'default').split():
        base = work / reader
        reader_args = [] if reader == 'default' else ['--reader', reader]
        run([lhd, 'compile', source, '--top', top, *reader_args, *compile_args,
             '--emit-dir', 'lg:' + str(base / 'source'), '--workdir', base / 'compile'])
        for lib_name in meta.get('libs', 'test').split():
            dest = base / lib_name
            dest.mkdir(parents=True)
            lib = Path('inou/prp/tests/abc') / (lib_name + '.lib')
            run([lhd, 'synth', 'lg:' + str(base / 'source'), '--top', top,
                 '--set', 'synth.liberty=' + str(lib), '--set', 'synth.opentimer=false',
                 '--set', 'synth.threads=1', '--set', 'synth.mapper=' + mapper,
                 *settings(meta.get('synth_set', '')),
                 '--emit-dir', 'lg:' + str(dest / 'mapped'), '--emit', 'verilog:' + str(dest / 'mapped.v'),
                 '--emit', 'diagnostics:' + str(dest / 'diagnostics.jsonl'), '--workdir', dest / 'synth'])
            nonempty(dest / 'mapped.v')
            nonempty(dest / 'mapped' / 'library.txt')
            diagnostics = (dest / 'diagnostics.jsonl').read_text()
            if 'pass.abc.memory=true' in meta.get('synth_set', '') and re.search(
                    'memory-unlowered|memory-max-bits', diagnostics):
                raise RuntimeError('memory was not mapped: ' + diagnostics)
            run([lhd, 'pass', 'liberty', 'gensim', lib,
                 '--emit-dir', 'lg:' + str(dest / 'models'), '--emit', 'verilog:' + str(dest / 'models.v'),
                 '--workdir', dest / 'gensim'])
            reference = str(ref) if ref.exists() else 'lg:' + str(base / 'source')
            extra = ['--lib', 'lg:' + str(dest / 'models')] + settings(meta.get('lec_set', ''))
            # strict: post-synthesis equivalence is the entire claim of a synth
            # fixture, so an UNKNOWN that merely hit the solver budget must fail
            # (measured 2026-09-21: 17/17 report `proven`). A fixture that
            # genuinely cannot be decided opts out with `:lec_may_timeout: true`.
            check(lhd, 'lg:' + str(dest / 'mapped'), reference, top, dest / 'lec', extra,
                  meta.get('ref_top', 'reference') if ref.exists() else top,
                  strict=not str(meta.get('lec_may_timeout', '')).strip().lower() in ('true', '1', 'yes'))
            # Handwritten RTL oracles remain executable fixtures. A missing tool,
            # failed compilation, or failed assertion is a failure, never a skip.
            if tb.exists():
                rtl = [tb, dest / 'mapped.v', dest / 'models.v']
                if ref.exists():
                    rtl.append(ref)
                run(['iverilog', '-g2012', '-s', 'tb', '-o', dest / 'sim', *rtl])
                run(['vvp', dest / 'sim'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['roundtrip', 'synth'])
    parser.add_argument('source', type=Path)
    # One fixture list, both technology mappers: `lhd synth --set synth.mapper=`.
    parser.add_argument('mapper', nargs='?', default='abc', choices=['abc', 'synth'])
    args = parser.parse_args()
    lhd = os.environ.get('LHD') or ('./bazel-bin/lhd/lhd' if Path('bazel-bin/lhd/lhd').exists() else './lhd/lhd')
    root = Path(tempfile.mkdtemp(prefix='integration_', dir=os.environ.get('TEST_TMPDIR')))
    try:
        globals()[args.mode](lhd, args.source, tags(args.source), root, args.mapper)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print('FAIL:', error, '\nArtifacts:', root, file=sys.stderr)
        return 1
    shutil.rmtree(root)
    print('PASS:', args.mode, args.source, args.mapper)
    return 0


if __name__ == '__main__':
    sys.exit(main())
