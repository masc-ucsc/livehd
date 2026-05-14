#!/usr/bin/env bash
# diff_no_compile_flags_touched.sh — fail if `git diff` modifies compiler
# warning options in build configuration files.
#
# Per CLAUDE.md "Compiler warning options" contract: agents must fix the
# source code instead of suppressing warnings/errors via build flags. The
# only built-in exception is MODULE.bazel (external-dep warning
# suppression).
#
# Detection scope:
#   * Files matched: BUILD, BUILD.bazel, *.bzl
#   * Files excluded: MODULE.bazel
#   * Patterns flagged on +/- lines:
#       -W<letter>...   (e.g. -Wall, -Wextra, -Wshadow, -Wno-error=shadow)
#       -Werror, -Wno-*
#       -pedantic, -pedantic-errors
#       -w              (disable all warnings, as standalone token)
#
# Exit codes:
#   0  no warning flags touched
#   1  one or more warning flags added/removed in a build file
#   2  no git context available (treated as failure)

set -u

cd "$(git rev-parse --show-toplevel 2>/dev/null)" || {
  echo "diff_no_compile_flags_touched: not in a git repo" >&2
  exit 2
}

# Pick a base ref to diff against: prefer main, else previous commit, else
# just the working tree.
if git rev-parse --verify --quiet main >/dev/null; then
  base="main"
elif git rev-parse --verify --quiet HEAD~1 >/dev/null; then
  base="HEAD~1"
else
  base=""
fi

if [ -n "$base" ]; then
  diff_output=$( { git diff "$base"...HEAD; git diff; } )
else
  diff_output=$(git diff)
fi

violations=$(printf '%s\n' "$diff_output" | awk '
  # Track current file from the diff header.
  /^diff --git / { file = ""; next }
  /^\+\+\+ b\// { file = substr($0, 7); next }
  /^--- /        { next }

  {
    if (file == "")              next
    if (file == "MODULE.bazel")  next  # explicit exception
    # Restrict to bazel build configuration files.
    if (file !~ /(^|\/)BUILD$/ && file !~ /(^|\/)BUILD\.bazel$/ && file !~ /\.bzl$/) next

    # Only +/- diff content lines (skip the +++/--- file headers).
    if ($0 !~ /^[+-]/)         next
    if ($0 ~ /^(\+\+\+|---)/)  next

    # Compiler warning flag patterns.
    is_warning = 0
    if ($0 ~ /-W[A-Za-z]/)             is_warning = 1
    if ($0 ~ /-Werror([=[:space:]"]|$)/) is_warning = 1
    if ($0 ~ /-pedantic([-[:space:]"]|$)/) is_warning = 1
    # Match -w as a standalone token (avoid matching -Wall/-Wno-*).
    if ($0 ~ /(^|[[:space:]"\(\[])-w([[:space:]"\),\]]|$)/) is_warning = 1

    if (is_warning) {
      printf("%s: %s\n", file, $0)
      found = 1
    }
  }

  END { exit (found ? 1 : 0) }
')
awk_status=$?

if [ "$awk_status" -ne 0 ]; then
  cat <<'EOF' >&2
ERROR: compiler warning flags were modified in build configuration.
       Per CLAUDE.md "Compiler warning options" contract, fix the
       source code instead of suppressing warnings. Only MODULE.bazel
       (external deps) is exempt.

Offending diff lines:
EOF
  printf '%s\n' "$violations" >&2
  exit 1
fi

exit 0
