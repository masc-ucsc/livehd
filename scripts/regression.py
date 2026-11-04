#!/usr/bin/env python3
"""Select frontend/backend regressions conservatively from changed paths."""

import argparse
import pathlib
import shlex
import subprocess
import sys

BACKEND = (
    'inou/lefdef/', 'pass/place/', 'pass/domplace/', 'pass/opentimer/',
    'pass/liberty/', 'tools/uroute/', 'tools/spef/', 'scripts/domplace/',
)
FRONTEND = ('core/', 'graph/', 'lnast/', 'parser/', 'upass/', 'inou/', 'pass/')
SHARED = ('lhd/', 'pass/abc/', 'pass/usyn/', 'pass/synth/', 'pass/domino/')


def select_group(paths):
    """Unknown/shared changes and an empty change list select all tests."""
    groups = set()
    for path in paths:
        if path.startswith(SHARED):
            return 'all'
        if path.startswith(BACKEND):
            groups.add('backend')
        elif path.startswith(FRONTEND):
            groups.add('frontend')
        else:
            return 'all'
    return next(iter(groups)) if len(groups) == 1 else 'all'


def changed_paths(root, base=None, head='HEAD'):
    """Include deleted/renamed paths and staged, unstaged and untracked work."""
    commands = ([['diff', '--name-only', '--no-renames', '-z', base, head, '--']]
                if base else [
                    ['diff', '--name-only', '--no-renames', '-z', 'HEAD', '--'],
                    ['ls-files', '--others', '--exclude-standard', '-z'],
                ])
    paths = set()
    for command in commands:
        result = subprocess.run(['git', *command], cwd=root, capture_output=True, check=True)
        paths.update(path for path in result.stdout.decode('utf-8', 'surrogateescape').split('\0') if path)
    return sorted(paths)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    for name in ('all', 'frontend', 'backend'):
        group.add_argument('--' + name, action='store_const', const=name, dest='group')
    parser.add_argument('--base')
    parser.add_argument('--head', default='HEAD')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('bazel_args', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    root = pathlib.Path(__file__).resolve().parent.parent
    selected = args.group
    if selected is None:
        try:
            selected = select_group(changed_paths(root, args.base, args.head))
        except (OSError, subprocess.CalledProcessError):
            selected = 'all'
            print('Regression history unavailable; selecting both groups.', file=sys.stderr)
    extra = args.bazel_args
    if extra[:1] == ['--']:
        extra = extra[1:]
    command = ['bazel', 'test', '--config=' + selected, *extra, '//...']
    print(shlex.join(command), flush=True)
    return 0 if args.dry_run else subprocess.call(command, cwd=root)


if __name__ == '__main__':
    sys.exit(main())
