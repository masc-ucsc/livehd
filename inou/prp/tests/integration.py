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

from lec import acceptable, run_lec, verdict


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
    if not acceptable(status):
        raise RuntimeError('LEC {}'.format(status))


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


def synth_one(lhd, top, ref, meta, base, tech):
    """Map the compiled `base/source` with one technology mapper and check it."""
    for lib_name in meta.get('libs', 'test').split():
        dest = base / tech / lib_name
        dest.mkdir(parents=True)
        lib = Path('inou/prp/tests/abc') / (lib_name + '.lib')
        run([lhd, 'synth', 'lg:' + str(base / 'source'), '--top', top,
             '--set', 'synth.liberty=' + str(lib), '--set', 'synth.opentimer=false',
             '--set', 'lhd.jobs=1', '--set', 'synth.mapper=' + tech,
             *settings(meta.get('synth_set', '')),
             '--emit-dir', 'lg:' + str(dest / 'mapped'), '--emit', 'verilog:' + str(dest / 'mapped.v'),
             '--emit', 'diagnostics:' + str(dest / 'diagnostics.jsonl'), '--workdir', dest / 'synth'])
        nonempty(dest / 'mapped.v')
        nonempty(dest / 'mapped' / 'library.txt')
        diagnostics = (dest / 'diagnostics.jsonl').read_text()
        if 'pass.abc.memory=true' in meta.get('synth_set', '') and re.search(
                'memory-unlowered|memory-max-bits', diagnostics):
            raise RuntimeError('memory was not mapped: ' + diagnostics)
        # `lhd lec --lib lg:` materializes the model Verilog a Verilog side
        # needs itself, so gensim only has to emit the graph library.
        run([lhd, 'pass', 'liberty', 'gensim', lib,
             '--emit-dir', 'lg:' + str(dest / 'models'), '--workdir', dest / 'gensim'])
        reference = str(ref) if ref.exists() else 'lg:' + str(base / 'source')
        ref_top = meta.get('ref_top', 'reference') if ref.exists() else top
        extra = ['--lib', 'lg:' + str(dest / 'models')] + settings(meta.get('lec_set', ''))
        # strict: post-synthesis equivalence is the entire claim of a synth
        # fixture, so a TIMEOUT that merely hit the solver budget must fail
        # (measured 2026-09-21: 17/17 report `proven`). A fixture that
        # genuinely cannot be decided opts out with `:lec_may_timeout: true`.
        strict = str(meta.get('lec_may_timeout', '')).strip().lower() not in ('true', '1', 'yes')
        check(lhd, 'lg:' + str(dest / 'mapped'), reference, top, dest / 'lec', extra, ref_top,
              strict=strict)
        # The emitted mapped netlist, re-read through the Verilog reader.
        check(lhd, 'verilog:' + str(dest / 'mapped.v'), reference, top, dest / 'lec-verilog',
              extra, ref_top, strict=strict)


def synth(lhd, source, meta, work, mapper='abc'):
    """`mapper` is a comma list: the source is compiled once per reader, then
    mapped and checked once per mapper. Every mapper runs even if an earlier one
    fails, so one broken mapper cannot hide the result of the others."""
    fixture = source
    source = source.parent / meta['source'] if 'source' in meta else source
    top = meta.get('top', tags(source).get('pyrope_top', source.stem))
    ref = fixture.with_name(fixture.stem + '_ref.v')
    compile_args = settings(meta.get('compile_set', ''))
    failures = []
    for reader in meta.get('readers', 'default').split():
        base = work / reader
        reader_args = [] if reader == 'default' else ['--reader', reader]
        run([lhd, 'compile', source, '--top', top, *reader_args, *compile_args,
             '--emit-dir', 'lg:' + str(base / 'source'), '--workdir', base / 'compile'])
        for tech in mapper.split(','):
            try:
                synth_one(lhd, top, ref, meta, base, tech)
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                print('FAIL [{} {}]: {}'.format(reader, tech, error), file=sys.stderr, flush=True)
                failures.append('{} {}: {}'.format(reader, tech, error))
    if failures:
        raise RuntimeError(' | '.join(failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['roundtrip', 'synth'])
    parser.add_argument('source', type=Path)
    # Comma list of technology mappers (`lhd synth --set synth.mapper=`), all run by one target.
    parser.add_argument('mapper', nargs='?', default='abc')
    args = parser.parse_args()
    if not all(tech in ('abc', 'usyn') for tech in args.mapper.split(',')):
        parser.error('mapper must be a comma list of abc, usyn: ' + args.mapper)
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
