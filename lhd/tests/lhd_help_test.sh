#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd help` / `lhd <command> --help`: the help surface contract.
#   1. `lhd help X` renders identically to `lhd X --help` for every command and
#      sub-command (including the `formal lec` alias).
#   2. `lhd X Y --help` shows Y's own page, not X's generic overview.
#   3. --diag-fmt selects the rendering, in every case: pretty prints the human
#      page, jsonl prints the machine record (schema_version=1). auto == jsonl
#      here (stdout is piped), so pretty is forced where the human page is under
#      test.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_help_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Strict JSON validation when python3 is available (a shell glob only checks the
# shape); a no-op fallback keeps the test hermetic where python3 is absent.
HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
json_ok() {  # $1 = candidate JSON text
  [ "$HAVE_PY" = 0 ] && return 0
  printf '%s' "$1" | python3 -c 'import sys,json;json.loads(sys.stdin.read())' >/dev/null 2>&1
}

# Every real command + the pass/pyrope/formal sub-commands. This is the COMMAND
# help surface — the subject of the three guarantees. Deliberately excluded:
#   * lsp / semdiff — retired top-level spellings (`lhd lsp`/`lhd semdiff` error
#     with a migration hint); kept only as `lhd help` aliases (tested in §8).
#   * emit-kinds (verilog/ln/lg/...), recipes (O0/O1/O2), dump, config — these
#     are `lhd describe` items (JSON records by design, no pretty page); reaching
#     them via `lhd help X` is a describe courtesy, not a command help page.
CMDS=("" compile lec formal scan tool pyrope pass sim list describe version \
      "pass color" "pass partition" "pass abc" "pass liberty" "pass semdiff" \
      "pyrope fmt" "pyrope lsp" "formal verify" "formal lec")

# ---------------------------------------------------------------------------
# 1. `lhd help X` == `lhd X --help`, in BOTH render modes.
# ---------------------------------------------------------------------------
for mode in pretty jsonl; do
  for X in "${CMDS[@]}"; do
    a=$("$LHD" help $X --diag-fmt $mode 2>&1); ra=$?
    b=$("$LHD" $X --help --diag-fmt $mode 2>&1); rb=$?
    [ "$ra" = "$rb" ] || fail "help '$X' ($mode): exit $ra vs $rb"
    [ "$a" = "$b" ] || fail "help '$X' ($mode): 'help X' != 'X --help'"$'\n'"help X:  $a"$'\n'"X --help: $b"
    [ "$ra" = "0" ] || fail "help '$X' ($mode): non-zero exit $ra"
  done
done

# ---------------------------------------------------------------------------
# 2. --diag-fmt selects the format in every case.
#    pretty  -> human page (does NOT start with '{'; a command page has "usage:")
#    jsonl   -> one JSON object with schema_version=1
# ---------------------------------------------------------------------------
for X in "${CMDS[@]}"; do
  p=$("$LHD" help $X --diag-fmt pretty 2>&1)
  j=$("$LHD" help $X --diag-fmt jsonl 2>&1)
  case "$p" in
    '{'*) fail "help '$X' --diag-fmt pretty leaked JSON: $p" ;;
  esac
  case "$j" in
    '{'*'"schema_version":1'*'}') ;;
    *) fail "help '$X' --diag-fmt jsonl is not a schema_version=1 record: $j" ;;
  esac
  json_ok "$j" || fail "help '$X' --diag-fmt jsonl is not valid JSON: $j"
done

# A command page (pretty) carries its OWN "usage: lhd <command>" line — grepping
# the full topic (not a bare "usage:") so a page that collapsed to the general
# overview (whose usage line is "usage: lhd [flags] <command>") is caught.
for X in compile lec formal scan tool sim "pass abc" "pyrope fmt"; do
  "$LHD" help $X --diag-fmt pretty 2>&1 | grep -qF "usage: lhd $X" \
    || fail "help '$X' pretty is missing its own 'usage: lhd $X ...' line"
done

# ---------------------------------------------------------------------------
# 3. Sub-command help is SPECIFIC: `lhd pass abc --help` is the abc page, not
#    the generic `lhd pass` overview.
# ---------------------------------------------------------------------------
for pair in "pass color" "pass partition" "pass abc" "pass liberty" "pass semdiff" \
            "pyrope fmt" "pyrope lsp"; do
  # jsonl: the record's "name" is the two-word sub-command.
  "$LHD" $pair --help --diag-fmt jsonl 2>&1 | grep -qF "\"name\":\"$pair\"" \
    || fail "'$pair --help' (jsonl) name is not '$pair'"
  # and it differs from the parent overview.
  parent=${pair%% *}
  s=$("$LHD" $pair --help --diag-fmt jsonl 2>&1)
  p=$("$LHD" $parent --help --diag-fmt jsonl 2>&1)
  [ "$s" != "$p" ] || fail "'$pair --help' is identical to the '$parent' overview (not specific)"
done

# The abc pretty page names abc, not just the pass overview.
"$LHD" pass abc --help --diag-fmt pretty 2>&1 | grep -q 'lhd pass abc' \
  || fail "pass abc --help pretty is not the abc page"

# ---------------------------------------------------------------------------
# 4. `formal lec` is a behavior-preserving alias of `lec`: its help IS lec help.
# ---------------------------------------------------------------------------
for mode in pretty jsonl; do
  a=$("$LHD" formal lec --help --diag-fmt $mode 2>&1)
  b=$("$LHD" lec --help --diag-fmt $mode 2>&1)
  [ "$a" = "$b" ] || fail "formal lec --help ($mode) != lec --help ($mode)"
  c=$("$LHD" help formal lec --diag-fmt $mode 2>&1)
  [ "$c" = "$b" ] || fail "help formal lec ($mode) != lec --help ($mode)"
done

# ---------------------------------------------------------------------------
# 5. `help sim` is the sim COMMAND, distinct from the `sim` emit-kind describe
#    returns (the collision the jsonl help path must resolve correctly).
# ---------------------------------------------------------------------------
simj=$("$LHD" help sim --diag-fmt jsonl 2>&1)
echo "$simj" | grep -q 'test` blocks' || fail "help sim (jsonl) is not the sim command record"
# The machine record must document the same probe flags as the pretty page /
# parse_args (the jsonl record was once missing probe-from/probe-to).
for f in probe-from probe-to vcd-from vcd-to; do
  echo "$simj" | grep -qF "\"name\":\"$f\"" || fail "sim record (jsonl) missing the --$f flag"
done
"$LHD" describe sim 2>&1 | grep -q 'Executable C++ simulation' \
  || fail "describe sim should still be the sim emit-kind record"
# Same collision for `pyrope` (command family vs emit-kind).
"$LHD" help pyrope --diag-fmt jsonl 2>&1 | grep -q 'developer tools' \
  || fail "help pyrope (jsonl) is not the pyrope overview record"
"$LHD" describe pyrope 2>&1 | grep -q 'Pyrope source' \
  || fail "describe pyrope should still be the pyrope emit-kind record"

# ---------------------------------------------------------------------------
# 6. The general page: pretty lists commands, jsonl carries a commands array,
#    and neither face omits a real command (pretty & jsonl must not diverge on
#    the command set — e.g. `formal` must appear on BOTH).
# ---------------------------------------------------------------------------
gp=$("$LHD" help --diag-fmt pretty 2>&1)
gj=$("$LHD" help --diag-fmt jsonl 2>&1)
echo "$gp" | grep -q '^commands:' || fail "general help (pretty) is missing the 'commands:' section"
echo "$gj" | grep -q '"commands":\[' || fail "general help (jsonl) is missing the commands array"
for c in compile sim lec formal scan tool pyrope pass list describe version; do
  echo "$gp" | grep -qE "^  $c " || fail "general help (pretty) omits the '$c' command"
  echo "$gj" | grep -qF "\"name\":\"$c\"" || fail "general help (jsonl) omits the '$c' command"
done
# `lhd help` and `lhd --help` are the same general page.
[ "$("$LHD" --help --diag-fmt pretty 2>&1)" = "$gp" ] || fail "lhd --help != lhd help (general page)"

# ---------------------------------------------------------------------------
# 7. An unknown sub-command is a clean error in both modes (non-zero, hint).
# ---------------------------------------------------------------------------
"$LHD" pass bogus --help --diag-fmt jsonl >"$W/o7" 2>&1 && fail "pass bogus --help must be non-zero"
grep -q "unknown pass subcommand 'bogus'" "$W/o7" || fail "pass bogus --help: hint missing -> $(cat "$W/o7")"

# ---------------------------------------------------------------------------
# 8. Help aliases route to the canonical page in BOTH modes.
#    lsp -> `pyrope lsp`, semdiff -> `pass semdiff`, tools -> `tool`.
# ---------------------------------------------------------------------------
for mode in pretty jsonl; do
  [ "$("$LHD" help lsp --diag-fmt $mode 2>&1)" = "$("$LHD" help pyrope lsp --diag-fmt $mode 2>&1)" ] \
    || fail "help lsp ($mode) != help pyrope lsp"
  [ "$("$LHD" help semdiff --diag-fmt $mode 2>&1)" = "$("$LHD" help pass semdiff --diag-fmt $mode 2>&1)" ] \
    || fail "help semdiff ($mode) != help pass semdiff"
  # `tools` is the accepted alias for `tool`: help parity must hold on both
  # spellings, and `lhd help tools` must match `lhd tools --help`.
  [ "$("$LHD" help tools --diag-fmt $mode 2>&1)" = "$("$LHD" help tool --diag-fmt $mode 2>&1)" ] \
    || fail "help tools ($mode) != help tool"
  a=$("$LHD" help tools --diag-fmt $mode 2>&1); ra=$?
  b=$("$LHD" tools --help --diag-fmt $mode 2>&1); rb=$?
  { [ "$a" = "$b" ] && [ "$ra" = "$rb" ] && [ "$ra" = "0" ]; } \
    || fail "help tools ($mode) != tools --help (rc $ra/$rb)"
done

echo "PASS: lhd help / --help parity, sub-command specificity, aliases, --diag-fmt pretty|jsonl"
