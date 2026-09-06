#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# EMITTED-WIDTH SCOREBOARD.
#
# The per-pin `bits`/`pin_signed` contract (node_util.hpp set_ubits) is that
# `bits` is the LITERAL container width: an unsigned b-bit pin represents
# [0,2^b-1], a signed one [-2^(b-1),2^(b-1)-1]. There is no sign slot in an
# unsigned width -- `u2` holds 3, `s3` holds 3, and hlop agrees (`Slop_u<W>`
# carries W value bits in a `Slop<W+1>` carrier, so `Slop_arg<Slop_u<W>>::bits`
# is W+1).
#
# Nothing enforces TIGHTNESS, though, and nothing can: a pass that stamps one
# extra bit -- or twenty -- is still CORRECT, just wasteful, so neither
# debug_check_pin_hint (which only asserts the value FITS) nor `lhd lec` (which
# proves the two netlists agree) can see it. Width inflation is invisible to
# every existing test.
#
# This test makes it visible the only way an always-legal regression can be
# caught: as a pinned NUMBER. For each design it sums the declared bits of
# every port and net in the emitted Verilog and compares the total against
# `width_scoreboard_golden.txt`.
#
#   total goes UP   -> a pass started inflating widths. That is the regression
#                      this test exists to catch. Find it before updating.
#   total goes DOWN -> inference got tighter. Re-run with UPDATE_GOLDEN=1 and
#                      commit the smaller numbers.
#
# Regenerate the whole table:  UPDATE_GOLDEN=1 bazel run //lhd/tests:width_scoreboard_test
# (or run this script directly from the repo root with UPDATE_GOLDEN=1).

set -u

LHD="${LHD:-lhd/lhd}"
EQUIV="${EQUIV_DIR:-inou/prp/tests/equiv}"
GOLDEN="${GOLDEN_FILE:-lhd/tests/width_scoreboard_golden.txt}"
W="${TEST_TMPDIR:-/tmp/lhd_width_scoreboard_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$LHD" ] || fail "lhd binary not found at $LHD"

# One design per width-shaping cell class, so an inflation in any of them moves
# a number here: arithmetic carry growth (sum), variable shift envelopes
# (shl/sra), mask capacity (get_mask/set_mask), mux arm union, register
# feedback, memory geometry, and the Sub instance boundary.
DESIGNS="
trivial_if
bitrange_dyn
bitrange_dyn_narrow
codeblock_expr
arr2d_dyn_sel
arr_comb_tmp
array_bitview_runtime
bit_sel_sext
bitsel_dyn
bitset_reg
async_reset_enable
comb_array_const_index_read
assign_pattern
packed_assign
match_no_else
"

# Sum the declared bits of every port/net declaration in an emitted module.
# Declarations are one per line in cgen_verilog's output and take exactly two
# shapes: `<kw> [msb:lsb] name` (msb-lsb+1 bits) and `<kw> name` (1 bit).
# `reg [7:0] mem [0:15]` (a memory) contributes its WORD width times its depth.
sum_bits() {
  awk '
    match($0, /^[[:space:],]*(input|output|inout)?[[:space:]]*(reg|wire|logic)?[[:space:]]*(signed)?[[:space:]]*(\[[0-9]+:[0-9]+\])?[[:space:]]+[\\]?[A-Za-z_]/) {
      line = $0
      # skip anything that is not a declaration (assignments, instances)
      if (line ~ /=/ || line ~ /^[[:space:]]*(always|assign|end|begin|module|endmodule|\/\/|\/\*)/) next
      if (line !~ /^[[:space:],]*(input|output|inout|reg|wire|logic)[[:space:]]/) next
      w = 1
      if (match(line, /\[[0-9]+:[0-9]+\]/)) {
        r = substr(line, RSTART+1, RLENGTH-2)
        split(r, se, ":")
        w = se[1] - se[2] + 1
        # a second range after the name is an unpacked memory depth
        rest = substr(line, RSTART+RLENGTH)
        if (match(rest, /\[[0-9]+:[0-9]+\][[:space:]]*;/)) {
          d = substr(rest, RSTART+1, RLENGTH-2)
          sub(/\].*/, "", d)
          split(d, de, ":")
          n = de[1] - de[2] + 1
          if (n > 0) w = w * n
        }
      }
      total += w
      nets  += 1
    }
    END { printf "%d %d\n", total+0, nets+0 }
  ' "$@"
}

measured="$W/measured.txt"
: > "$measured"

for name in $DESIGNS; do
  src="$EQUIV/$name.v"
  [ -f "$src" ] || fail "$name: missing source $src"
  d="$W/$name"
  mkdir -p "$d"
  "$LHD" compile "$src" --workdir "$d/w" --emit-dir "verilog:$d/v/" \
    >"$d/compile.log" 2>&1 || fail "$name: compile failed (see $d/compile.log)"
  # shellcheck disable=SC2046
  set -- $(sum_bits "$d"/v/*.v)
  [ "${2:-0}" -gt 0 ] || fail "$name: parsed 0 declarations from $d/v -- the emission format changed and this scoreboard is measuring nothing"
  echo "$name $1 $2" >> "$measured"
done

if [ "${UPDATE_GOLDEN:-0}" = "1" ]; then
  cat > "$GOLDEN" <<HDR
# Emitted-width scoreboard: <design> <total declared bits> <declaration count>.
# Regenerate with UPDATE_GOLDEN=1; see width_scoreboard_test.sh for what a
# change in either direction means.
HDR
  cat "$measured" >> "$GOLDEN"
  echo "updated $GOLDEN"
  exit 0
fi

[ -f "$GOLDEN" ] || fail "missing golden $GOLDEN (regenerate with UPDATE_GOLDEN=1)"

rc=0
while read -r name bits nets; do
  case "$name" in \#*|"") continue ;; esac
  got="$(awk -v n="$name" '$1==n {print $2, $3}' "$measured")"
  [ -n "$got" ] || { echo "FAIL: $name is in the golden but was not measured" >&2; rc=1; continue; }
  set -- $got
  if [ "$1" != "$bits" ] || [ "$2" != "$nets" ]; then
    if [ "$1" -gt "$bits" ]; then
      echo "FAIL: $name WIDER: $bits -> $1 bits over $2 nets (was $nets). A pass inflated widths; find it before updating the golden." >&2
    else
      echo "FAIL: $name changed: $bits/$nets -> $1/$2 bits/nets. Narrower is an improvement -- re-run with UPDATE_GOLDEN=1 and commit." >&2
    fi
    rc=1
  fi
done < "$GOLDEN"

[ "$rc" -eq 0 ] || exit 1
echo "PASS: emitted-width scoreboard -- every pinned design matches its declared-bit total"
