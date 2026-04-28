#!/usr/bin/env bash
# check_contract_untouched.sh — fail if any *contract* path appears in the diff.
#
# Used by Bernstein as both a quality gate (lint_command) and an
# organizational rule (.bernstein/rules.yaml). Exit codes:
#   0  no contract files touched
#   1  one or more contract files modified/added/deleted/renamed
#   2  no git context available (treated as failure to be safe)
#
# Detection is case-insensitive on the path. We compare against `main`
# when present, else HEAD~1, else the working tree.

set -u

cd "$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "check_contract_untouched: not in a git repo" >&2
  exit 2
}

# Pick a base ref to diff against.
if git rev-parse --verify --quiet main >/dev/null; then
  base="main"
elif git rev-parse --verify --quiet HEAD~1 >/dev/null; then
  base="HEAD~1"
else
  base=""
fi

if [ -n "$base" ]; then
  changed=$(git diff --name-only --diff-filter=ACDMRT "$base"...HEAD; \
            git diff --name-only --diff-filter=ACDMRT)
else
  changed=$(git status --porcelain | awk '{print $NF}')
fi

# Case-insensitive match.
hits=$(printf '%s\n' "$changed" | sort -u | grep -i 'contract' || true)

if [ -n "$hits" ]; then
  echo "ERROR: contract files are immutable but were modified:" >&2
  printf '  %s\n' $hits >&2
  exit 1
fi

exit 0
