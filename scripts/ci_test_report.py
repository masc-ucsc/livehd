#!/usr/bin/env python3
"""Turn a Bazel build-event JSON file into a GitHub Actions failure report.

CI job logs are readable only by repository admins, so a red run used to say
just "exit code 3". This prints one `::error` annotation per failing test (the
label plus the tail of its test.log) and a table in the job summary; both are
visible to anyone who can see the run. It also copies each failing test.log
into --log-dir so the workflow can upload them as an artifact.

Usage: ci_test_report.py BEP_JSON [--log-dir DIR] [--tail N] [--max-annotations N]
Exits 0 once the report is written (1 only when the build-event file is missing).
"""

import argparse
import json
import os
import pathlib
import shutil
import sys
import urllib.parse

BAD = {'FAILED', 'TIMEOUT', 'INCOMPLETE', 'REMOTE_FAILURE', 'FAILED_TO_BUILD',
       'TOOL_HALTED_BEFORE_TESTING', 'NO_STATUS'}


def uri_path(uri):
    parsed = urllib.parse.urlparse(uri)
    return pathlib.Path(urllib.parse.unquote(parsed.path)) if parsed.scheme == 'file' else None


def read_events(path):
    """Yield (failing tests, flaky tests, targets that failed to build)."""
    failed, flaky, broken = {}, [], []
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            ident = event.get('id', {})
            if 'testSummary' in ident:
                label = ident['testSummary'].get('label', '?')
                summary = event.get('testSummary', {})
                status = summary.get('overallStatus', 'NO_STATUS')
                if status == 'FLAKY':
                    flaky.append(label)
                elif status in BAD:
                    logs = [uri_path(f.get('uri', '')) for f in summary.get('failed', [])]
                    failed[label] = (status, [p for p in logs if p])
            elif 'targetCompleted' in ident and event.get('completed', {}).get('success') is False:
                broken.append(ident['targetCompleted'].get('label', '?'))
            elif 'targetCompleted' in ident and 'aborted' in event:
                broken.append(ident['targetCompleted'].get('label', '?'))
    return failed, flaky, sorted(set(broken) - set(failed))


def tail(path, lines):
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except OSError as err:
        return f'(cannot read {path}: {err})'
    return '\n'.join(text.rstrip().splitlines()[-lines:])


def escape(message):
    # Workflow-command escaping: these three are the only special characters.
    return message.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')


def escape_property(value):
    # A property value (title=...) additionally escapes ':' and ','.
    return escape(value).replace(':', '%3A').replace(',', '%2C')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('bep')
    parser.add_argument('--log-dir')
    parser.add_argument('--tail', type=int, default=40)
    # GitHub keeps at most 10 error annotations per step; the last one is the full list.
    parser.add_argument('--max-annotations', type=int, default=9)
    args = parser.parse_args(argv)

    if not os.path.exists(args.bep):
        print(f'::error title=regression::no build-event file at {args.bep}; Bazel did not start')
        return 1
    failed, flaky, broken = read_events(args.bep)

    for i, (label, (status, logs)) in enumerate(sorted(failed.items())):
        if args.log_dir:
            for n, log in enumerate(logs):
                dest = pathlib.Path(args.log_dir) / (label.lstrip('/').replace(':', '/').replace('/', '__')
                                                     + (f'.{n}' if n else '') + '.log')
                dest.parent.mkdir(parents=True, exist_ok=True)
                try:
                    shutil.copyfile(log, dest)
                except OSError:
                    pass
        if i < args.max_annotations:
            body = tail(logs[-1], args.tail) if logs else '(no test.log)'
            print(f'::error title={escape_property(f"{status} {label}")}::{escape(body)}')

    lines = [f'{status} {label}' for label, (status, _) in sorted(failed.items())]
    lines += [f'FAILED_TO_BUILD {label}' for label in broken]
    if lines:
        print(f'::error title={len(lines)} failing targets::{escape(chr(10).join(lines))}')
    for label in flaky:
        print(f'::warning title={escape_property("FLAKY " + label)}::passed only on a retry')

    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as out:
            out.write(f'## Regression: {len(lines)} failing, {len(flaky)} flaky\n\n')
            if lines or flaky:
                out.write('| status | target |\n|---|---|\n')
                for line in lines:
                    status, label = line.split(' ', 1)
                    out.write(f'| {status} | `{label}` |\n')
                for label in flaky:
                    out.write(f'| FLAKY | `{label}` |\n')
            for label, (status, logs) in sorted(failed.items()):
                if logs:
                    out.write(f'\n<details><summary>{status} <code>{label}</code></summary>\n\n```\n'
                              f'{tail(logs[-1], args.tail)}\n```\n</details>\n')
    print(f'{len(lines)} failing, {len(flaky)} flaky', file=sys.stderr)
    # The regression step already failed the job; a second red step only adds noise.
    return 0


if __name__ == '__main__':
    sys.exit(main())
