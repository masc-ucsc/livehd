#!/usr/bin/env python3
"""Serially check an exported legacy corpus, recording time, RSS and axiom audits.

The inventory lists top, scope (bridge/typecheck), and replay_mode
(regenerated/saved_artifact). Files live at RUNTIME/TOP/export/TOP_Lgraph.lean
or RUNTIME/TOP/saved_artifact/TOP_Lgraph.lean respectively. Saved artifacts are
library compatibility checks; they do not validate the current exporter.
Build the support library first and supply its environment in ENV_JSON.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory', type=Path)
    parser.add_argument('runtime', type=Path)
    parser.add_argument('env_json', type=Path)
    parser.add_argument('--jobs', type=int, default=8, help='Lean workers; modules always run serially')
    parser.add_argument('--only', nargs='+', help='Restrict to these top names')
    parser.add_argument('--resume', action='store_true', help='Reuse successful results with identical artifacts and libraries')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    env = os.environ.copy()
    env.update(json.loads(args.env_json.read_text()))
    args.runtime.mkdir(parents=True, exist_ok=True)
    # Fingerprint the actual imported library artifacts, not only source paths.
    libraries = {}
    for root in env.get('LEAN_PATH', '').split(os.pathsep):
        if root:
            for f in sorted(Path(root).glob('**/*.olean')):
                libraries[str(f)] = digest(f)
    version = subprocess.check_output(['lean', '--version'], env=env, text=True).strip()
    cpus = sorted(os.sched_getaffinity(0))[:args.jobs]
    context = {'lean_version': version, 'libraries': libraries, 'lean_path': env.get('LEAN_PATH', '')}
    context_sha = hashlib.sha256(json.dumps(context, sort_keys=True).encode()).hexdigest()
    write_json(args.runtime / 'library_context.json', context)
    rows = json.loads(args.inventory.read_text())
    if args.only:
        requested = set(args.only)
        known = {row['top'] for row in rows}
        if requested - known:
            parser.error('unknown tops: ' + ', '.join(sorted(requested - known)))
        rows = [row for row in rows if row['top'] in requested]

    def file_for(row):
        folder = 'export' if row['replay_mode'] == 'regenerated' else 'saved_artifact'
        return args.runtime / row['top'] / folder / (row['top'] + '_Lgraph.lean')

    # Short cases first so a long proof cannot conceal broad regressions.
    rows.sort(key=lambda row: file_for(row).stat().st_size if file_for(row).exists() else -1)
    results = []
    for row in rows:
        top = row['top']
        file = file_for(row)
        out = args.runtime / top
        out.mkdir(exist_ok=True)
        result_file = out / 'proof_result.json'
        result = {'top': top, 'scope': row['scope'], 'replay_mode': row['replay_mode'],
                  'file': str(file), 'library_context_sha256': context_sha,
                  'lean_version': version, 'workers': args.jobs, 'cpus': cpus}
        if row['replay_mode'] == 'regenerated' and row.get('export_status', 0) != 0:
            result['verdict'] = 'EXPORT_FAILED'
        elif not file.exists():
            result['verdict'] = 'MISSING_ARTIFACT'
        else:
            result['sha256'] = digest(file)
            previous = json.loads(result_file.read_text()) if result_file.exists() else {}
            if (args.resume and previous.get('verdict') in ('PROVEN', 'TYPECHECKED')
                    and previous.get('sha256') == result['sha256']
                    and previous.get('library_context_sha256') == context_sha):
                results.append(previous)
                print('REUSE', top, previous['verdict'], flush=True)
                continue
            source = file.read_text()
            wanted = ['comb_refines_fast'] if row['scope'] == 'bridge' else []
            if row['scope'] == 'bridge' and re.search(r'^structure ' + re.escape(top) + r'_state\b', source, re.M):
                wanted += ['next_refines_fast', 'step_refines_fast']
            if row.get('cert_wf') == 'chunked':
                wanted.insert(0, 'graphCert_wf')
            # Explicitly require every expected theorem AND its emitted audit.
            missing = [s for s in wanted if f'#print axioms {top}_{s}' not in source]
            if missing:
                result.update(verdict='MISSING_SOURCE_AUDIT', missing_audits=missing)
            else:
                print('START', top, row['replay_mode'], flush=True)
                result.update(verdict='RUNNING', started_utc=datetime.now(timezone.utc).isoformat())
                write_json(result_file, result)
                command = ['taskset', '-c', ','.join(map(str, cpus)), 'nice', '-n', '19',
                           '/usr/bin/time', '-v', '-o', str(out / 'proof.time'),
                           'lean', '-j', str(args.jobs), str(file)]
                with (out / 'proof.log').open('w') as log:
                    proc = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                log = (out / 'proof.log').read_text()
                timing = (out / 'proof.time').read_text()
                missing = [s for s in wanted if not re.search(
                    r"'" + re.escape(f'{top}_Lgraph.{top}_{s}')
                    + r"' (depends on axioms:|does not depend on any axioms)", log)]
                result.update(exit_code=proc.returncode, sorryAx='sorryAx' in log,
                              errors=len(re.findall(r'\berror:', log)), missing_audits=missing,
                              completed_utc=datetime.now(timezone.utc).isoformat())
                for field, pattern in [('wall_time', r'Elapsed.*: ([0-9:.]+)'),
                                       ('peak_rss_kib', r'Maximum resident set size \(kbytes\): (\d+)')]:
                    match = re.search(pattern, timing)
                    result[field] = match[1] if match else None
                ok = proc.returncode == 0 and not result['errors'] and not result['sorryAx'] and not missing
                result['verdict'] = ('PROVEN' if wanted else 'TYPECHECKED') if ok else 'FAILED'
        write_json(result_file, result)
        results.append(result)
        write_json(args.runtime / 'proof_results.json', results)
        print('DONE', top, result['verdict'], result.get('wall_time'), result.get('peak_rss_kib'), flush=True)
    write_json(args.runtime / 'proof_results.json', results)
    return int(any(row['verdict'] not in ('PROVEN', 'TYPECHECKED') for row in results))


if __name__ == '__main__':
    raise SystemExit(main())
