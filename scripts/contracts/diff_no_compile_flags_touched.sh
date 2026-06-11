#!/usr/bin/env bash
# diff_no_compile_flags_touched.sh — fail if `git diff` WEAKENS compiler
# warning options in build configuration files.
#
# Per CLAUDE.md "Compiler warning options" contract: agents must fix the
# source code instead of suppressing warnings/errors via build flags. The
# guard is direction-aware — increasing strictness is always allowed; only
# changes that weaken diagnostics are flagged. The only file-level exception
# is MODULE.bazel (external-dep warning suppression).
#
# Detection scope:
#   * Files matched: BUILD, BUILD.bazel, *.bzl
#   * Files excluded: MODULE.bazel
#   * Comments (anything after `#`) are ignored — only active flags count.
#
# Per-token classification (a `-W...` token is one of):
#   * WEAKEN       -Wno-*  (incl. -Wno-error=*),  -w  (disable all warnings)
#   * STRENGTHEN   -Werror[=cat],  -W<warn> (e.g. -Wall/-Wshadow),  -pedantic[-errors]
#
# A diff line is a VIOLATION only when it weakens:
#   * a `+` (added) line carrying a WEAKEN token       — suppression added
#   * a `-` (removed) line carrying a STRENGTHEN token  — strictness removed
# Adding strictness (`+ -Werror`, `+ -Wfoo`) or removing a suppression
# (`- -Wno-foo`) is allowed and never flagged.
#
# Exit codes:
#   0  no warning flags weakened
#   1  one or more warning flags weakened in a build file
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

    sign = substr($0, 1, 1)     # "+" added, "-" removed
    rest = substr($0, 2)
    sub(/#.*/, "", rest)        # drop comments — only active flags count
    gsub(/[",()\[\]]/, " ", rest)  # normalize string/list punctuation to spaces
    n = split(rest, tok, /[[:space:]]+/)

    for (i = 1; i <= n; i++) {
      t = tok[i]
      if (t == "") continue

      # Classify the token. Order matters: -Wno-* must be tested before the
      # generic -W<warn> rule (which would otherwise match -Wno-shadow).
      weaken = 0; strengthen = 0
      if      (t ~ /-Wno-/)             weaken = 1      # -Wno-*, incl. -Wno-error=*
      else if (t ~ /(^|=)-w$/)          weaken = 1      # -w, disable all warnings
      else if (t ~ /-Werror(=|$)/)      strengthen = 1  # -Werror[=cat]
      else if (t ~ /-W[A-Za-z]/)        strengthen = 1  # -Wall, -Wshadow, ...
      else if (t ~ /-pedantic/)         strengthen = 1  # -pedantic[-errors]
      else                              continue        # not a warning flag

      # Only WEAKENING changes are violations.
      if (sign == "+" && weaken)     { printf("%s: %s\n", file, $0); found = 1; break }
      if (sign == "-" && strengthen) { printf("%s: %s\n", file, $0); found = 1; break }
    }
  }

  END { exit (found ? 1 : 0) }
')
awk_status=$?

if [ "$awk_status" -ne 0 ]; then
  cat <<'EOF' >&2
ERROR: compiler warning flags were WEAKENED in build configuration.
       Per CLAUDE.md "Compiler warning options" contract, fix the
       source code instead of suppressing warnings (no -Wno-* / -w, do
       not remove -W*/-Werror). Increasing strictness is fine. Only
       MODULE.bazel (external deps) is exempt.

Offending diff lines:
EOF
  printf '%s\n' "$violations" >&2
  exit 1
fi

exit 0
