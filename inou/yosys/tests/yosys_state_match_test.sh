#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# External lgcheck state correspondence and proof-budget regressions. These
# exercise Yosys fallback strategies independently of the default lhd/CVC5 CLI.
set -u
W="${TEST_TMPDIR:-/tmp/lgcheck_state_$$}"
mkdir -p "$W"
YOSYS=inou/yosys/yosys2
fail() { echo "FAIL: $*" >&2; exit 1; }

# A generated temporary can have the same name but a different expression in
# independently lowered graphs. Keep register/port correspondence, hide the
# combinational cutpoints, and prove every observable output. Also check that
# this fallback cannot hide a real change to the register's next-state value.
cat >"$W/temp_names_ref.v" <<'V'
module temp_names(input clk, input [3:0] a, b, output reg [3:0] q);
  wire [3:0] mux_528 = a + b;
  wire [3:0] mux_524 = a - b;
  always @(posedge clk) q <= mux_528 ^ mux_524;
endmodule
V
cat >"$W/temp_names_impl.v" <<'V'
module temp_names(input clk, input [3:0] a, b, output reg [3:0] q);
  wire [3:0] mux_528 = a - b;
  wire [3:0] mux_524 = a + b;
  always @(posedge clk) q <= mux_528 ^ mux_524;
endmodule
V
sed 's/mux_528 \^ mux_524/mux_528 | mux_524/' "$W/temp_names_impl.v" >"$W/temp_names_bad.v"
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS_ABS="$PWD/$YOSYS"
# Keep enough headroom for slow debug builders, while requiring a definitive
# verdict. The scheduling regression below supplies its own small budget.
LGCHECK_BUDGET="${LGCHECK_BUDGET:-300}"
# The proof and the refutation are independent yosys runs; start both, then
# read their verdicts in order.
for variant in impl bad; do
  mkdir -p "$W/temp_names_$variant"
  (cd "$W/temp_names_$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top temp_names \
    --reference "$W/temp_names_ref.v" --implementation "$W/temp_names_$variant.v") \
    >"$W/temp_names_$variant.log" 2>&1 &
  eval "temp_names_${variant}_pid=$!"
done
for variant in impl bad; do
  eval "wait \"\${temp_names_${variant}_pid}\""
  rc=$?
  if [ "$variant" = impl ]; then
    [ "$rc" -eq 0 ] || { cat "$W/temp_names_$variant.log"; fail "temporary names blocked equivalence"; }
    grep -q '1n.Successfully matched' "$W/temp_names_$variant.log" \
      || fail "temporary-name regression did not exercise the new fallback"
  else
    [ "$rc" -eq 1 ] || { cat "$W/temp_names_$variant.log"; fail "changed next-state function was not refuted"; }
  fi
done
echo "PASS: temporary names do not constrain combinational equivalence"

# Packed state and mapped scalar bits must carry the same induction relation.
# A one-bit observation of a 32-bit recurrence does not expose all state in the
# four-cycle induction window. A changed feedback bit must still be refuted.
python3 - "$W" <<'PYSTATE'
from pathlib import Path
import sys
w = Path(sys.argv[1])
(w / "packed_state_ref.v").write_text("""
module packed_state(input clk, d, output y);
  reg [31:0] state;
  always @(posedge clk) state <= {state[30:0], state[31] ^ state[21] ^ state[1] ^ state[0] ^ d};
  assign y = ^state;
endmodule
""")
for variant in ("impl", "bad"):
    lines = ["module packed_state(input clk, d, output y);"]
    lines += [f"reg state_{i};" for i in range(32)]
    feedback = "state_31 ^ state_21 ^ state_1 ^ state_0 ^ d"
    if variant == "bad":
        feedback = "~(" + feedback + ")"
    lines += [f"always @(posedge clk) state_0 <= {feedback};"]
    lines += [f"always @(posedge clk) state_{i} <= state_{i-1};" for i in range(1, 32)]
    lines += ["assign y = " + " ^ ".join(f"state_{i}" for i in range(32)) + ";", "endmodule"]
    (w / f"packed_state_{variant}.v").write_text("\n".join(lines) + "\n")
PYSTATE
# The proof and the refutation are independent yosys runs; start both, then
# read their verdicts in order.
for variant in impl bad; do
  mkdir -p "$W/packed_state_$variant"
  (cd "$W/packed_state_$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top packed_state \
    --reference "$W/packed_state_ref.v" --implementation "$W/packed_state_$variant.v") \
    >"$W/packed_state_$variant.log" 2>&1 &
  eval "packed_state_${variant}_pid=$!"
done
for variant in impl bad; do
  eval "wait \"\${packed_state_${variant}_pid}\""
  rc=$?
  if [ "$variant" = impl ]; then
    [ "$rc" -eq 0 ] || { cat "$W/packed_state_$variant.log"; fail "packed/scalar state relation was not proven"; }
  else
    [ "$rc" -eq 1 ] || { cat "$W/packed_state_$variant.log"; fail "changed scalar feedback was not refuted"; }
  fi
done
echo "PASS: packed/scalar state correspondence proves, changed feedback refutes"

# A register name can survive an encoding change without preserving its value.
# Keep useful recurrence matches, discard the inverted internal match, and
# reprove all outputs. A real output inversion must still produce a refutation.
python3 - "$W" <<'PYSTATE_ENCODING'
from pathlib import Path
import sys
w = Path(sys.argv[1])
for variant in ("ref", "impl", "bad"):
    q_next = "d" if variant == "ref" else "~d"
    q_out = "q" if variant != "impl" else "~q"
    (w / f"state_encoding_{variant}.v").write_text(f"""
module state_encoding(input clk, d, output y);
  reg [31:0] state;
  reg q;
  always @(posedge clk) begin
    state <= {{state[30:0], state[31] ^ state[21] ^ state[1] ^ state[0] ^ d}};
    q <= {q_next};
  end
  assign y = (^state) ^ ({q_out});
endmodule
""")
PYSTATE_ENCODING
# The proof and the refutation are independent yosys runs; start both, then
# read their verdicts in order.
for variant in impl bad; do
  mkdir -p "$W/state_encoding_$variant"
  (cd "$W/state_encoding_$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top state_encoding \
    --reference "$W/state_encoding_ref.v" --implementation "$W/state_encoding_$variant.v") \
    >"$W/state_encoding_$variant.log" 2>&1 &
  eval "state_encoding_${variant}_pid=$!"
done
for variant in impl bad; do
  eval "wait \"\${state_encoding_${variant}_pid}\""
  rc=$?
  if [ "$variant" = impl ]; then
    [ "$rc" -eq 0 ] || { cat "$W/state_encoding_$variant.log"; fail "changed internal state encoding was not proven"; }
    grep -q '^1p.Successfully matched' "$W/state_encoding_$variant.log" || fail "state encoding did not exercise fresh proof recovery"
  else
    [ "$rc" -eq 1 ] || { cat "$W/state_encoding_$variant.log"; fail "changed encoded output was not refuted"; }
  fi
done
echo "PASS: changed internal state encoding proves, changed output refutes"

# The reversible cgen wrapper name and mapped memory-bank names must propose
# the same word/bit states. Four byte-wide words retain dynamic addressing
# and multiple word/bit matches. Every proposed pair must prove its transition.
python3 - "$W" <<'PYMEMORY_NAMES'
from pathlib import Path
import sys
w = Path(sys.argv[1])
ports = "input clk, we, input [1:0] wa, ra, input [7:0] d, output [7:0] y"
(w / "memory_names_ref.v").write_text(f"""
module memory_names({ports});
  array_memory __lhdmem_h6d656d_e (clk, we, wa, ra, d, y);
endmodule
module array_memory({ports});
  reg [7:0] data [0:3];
  always @(posedge clk) if (we) data[wa] <= d;
  assign y = data[ra];
endmodule
""")
for variant in ("impl", "bad"):
    lines = [f"module memory_names({ports});", "mapped_memory mem (clk, we, wa, ra, d, y);",
             "endmodule", f"module mapped_memory({ports});"]
    for word in range(4):
        value = "d ^ 8'h01" if variant == "bad" and word == 0 else "d"
        lines += [f"reg [7:0] _mem{word};",
                  f"always @(posedge clk) if (we && wa == 2'd{word}) _mem{word} <= {value};"]
    lines += ["assign y = " + "".join(f"ra == 2'd{i} ? _mem{i} : " for i in range(3)) + "_mem3;",
              "endmodule"]
    (w / f"memory_names_{variant}.v").write_text("\n".join(lines) + "\n")
PYMEMORY_NAMES
# The proof and the refutation are independent yosys runs; start both, then
# read their verdicts in order.
for variant in impl bad; do
  mkdir -p "$W/memory_names_$variant"
  (cd "$W/memory_names_$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
    --yosys "$YOSYS_ABS" --top memory_names \
    --reference "$W/memory_names_ref.v" --implementation "$W/memory_names_$variant.v") \
    >"$W/memory_names_$variant.log" 2>&1 &
  eval "memory_names_${variant}_pid=$!"
done
for variant in impl bad; do
  eval "wait \"\${memory_names_${variant}_pid}\""
  rc=$?
  if [ "$variant" = impl ]; then
    [ "$rc" -eq 0 ] || { cat "$W/memory_names_$variant.log"; fail "memory word/bit correspondence was not proven"; }
    grep -q '^1r.Successfully matched' "$W/memory_names_$variant.log" || fail "memory did not exercise state-name recovery"
  else
    [ "$rc" -eq 1 ] || { cat "$W/memory_names_$variant.log"; fail "corrupted memory write was not refuted"; }
  fi
done
echo "PASS: memory word/bit correspondence proves, corrupted write refutes"

# An expensive correspondence guess must leave time for another strategy.
# The fake solver isolates scheduling: the first attempt would outlast the
# whole three-second budget; the next attempt returns an explicit proof.
cat >"$W/strategy_budget_yosys" <<'SHSTRATEGY'
#!/bin/sh
case "$*" in
  *"write_verilog trace1.v"*) exec sleep 4 ;;
  *"select -set state_outputs"*) echo 'Equivalence successfully proven!' ;;
esac
exit 0
SHSTRATEGY
chmod +x "$W/strategy_budget_yosys"
mkdir -p "$W/strategy_budget"
(cd "$W/strategy_budget" && LGCHECK_EQUIV_TIMEOUT=3 LGCHECK_HEURISTIC_TIMEOUT=1 \
  "$LGCHECK" --yosys "$W/strategy_budget_yosys" --top packed_state \
  --reference "$W/packed_state_ref.v" --implementation "$W/packed_state_impl.v") \
  >"$W/strategy_budget.log" 2>&1
[ "$?" -eq 0 ] || { cat "$W/strategy_budget.log"; fail "first matching attempt exhausted the shared proof budget"; }
grep -q '^1n.Successfully matched' "$W/strategy_budget.log" || fail "later proof strategy did not run"
echo "PASS: expensive matching attempt leaves budget for a later proof strategy"
