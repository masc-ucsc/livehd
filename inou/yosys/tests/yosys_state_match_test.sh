#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# External lgcheck state correspondence and proof-budget regressions. These
# exercise Yosys fallback strategies independently of the default lhd/CVC5 CLI.
set -u
W="${TEST_TMPDIR:-/tmp/lgcheck_state_$$}"
mkdir -p "$W"
YOSYS=inou/yosys/yosys2
fail() { echo "FAIL: $*" >&2; exit 1; }
part="${1:-all}"
case "$part" in
  all | temporary | packed | encoding | memory | rtl | bank | loop | init_names | poweron | budget) ;;
  *) fail "unknown state correspondence group: $part" ;;
esac
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS_ABS="$PWD/$YOSYS"
LGCHECK_BUDGET="${LGCHECK_BUDGET:-300}"

if [ "$part" = all ] || [ "$part" = temporary ]; then
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

fi

if [ "$part" = all ] || [ "$part" = packed ]; then
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
# The mapped side names bit i of `state` as the escaped identifier
# `\state[i] ` (LiveHD's bus-expansion standard, core/bus_name.hpp).
def bit(i):
    return f"\\state[{i}] "
for variant in ("impl", "bad"):
    lines = ["module packed_state(input clk, d, output y);"]
    lines += [f"reg {bit(i)};" for i in range(32)]
    feedback = f"{bit(31)} ^ {bit(21)} ^ {bit(1)} ^ {bit(0)} ^ d"
    if variant == "bad":
        feedback = "~(" + feedback + ")"
    lines += [f"always @(posedge clk) {bit(0)} <= {feedback};"]
    lines += [f"always @(posedge clk) {bit(i)} <= {bit(i-1)};" for i in range(1, 32)]
    lines += ["assign y = " + " ^ ".join(bit(i) for i in range(32)) + ";", "endmodule"]
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

fi

if [ "$part" = all ] || [ "$part" = encoding ]; then
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

fi

if [ "$part" = all ] || [ "$part" = memory ]; then
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
        # entry `word` of the storage bus `_mem` (core/bus_name.hpp)
        lines += [f"reg [7:0] \\_mem[{word}] ;",
                  f"always @(posedge clk) if (we && wa == 2'd{word}) \\_mem[{word}]  <= {value};"]
    lines += ["assign y = " + "".join(f"ra == 2'd{i} ? \\_mem[{i}]  : " for i in range(3)) + "\\_mem[3] ;",
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

fi

if [[ "$part" = all || "$part" = rtl || "$part" = bank || "$part" = loop || "$part" = init_names ]]; then
# A golden RTL array is `mem[<word>][<bit>]` after `memory`, while cgen keeps
# the same memory as the reversible wrapper `__lhdmem_h<hex>_e.data` or, for an
# inline packed memory, one `__lhdmem_h<hex>_e_data` bus. A rolled loop replica
# `u_loop_<n>__li<k>.<inst>` is the unrolled `<inst>__li<k>`. Each form must be
# paired (stage 1r) and proven; a corrupted write must still refute.
python3 - "$W" <<'PYRTL_NAMES'
from pathlib import Path
import sys
w = Path(sys.argv[1])
ports = "input clk, we, input [1:0] wa, ra, input [7:0] d, output [7:0] y"
(w / "rtl_names_ref.v").write_text(f"""
module rtl_names({ports});
  reg [7:0] mem [0:3];
  always @(posedge clk) if (we) mem[wa] <= d;
  assign y = mem[ra];
endmodule
""")
(w / "rtl_names_wrap.v").write_text(f"""
module rtl_names({ports});
  array_memory __lhdmem_h6d656d_e (clk, we, wa, ra, d, y);
endmodule
module array_memory({ports});
  reg [7:0] data [0:3];
  always @(posedge clk) if (we) data[wa] <= d;
  assign y = data[ra];
endmodule
""")
for variant, value in (("inline", "d"), ("bad", "d ^ 8'h01")):
    (w / f"rtl_names_{variant}.v").write_text(f"""
module rtl_names({ports});
  reg [31:0] __lhdmem_h6d656d_e_data;
  always @(posedge clk) if (we) __lhdmem_h6d656d_e_data[wa*8 +: 8] <= {value};
  assign y = __lhdmem_h6d656d_e_data[ra*8 +: 8];
endmodule
""")
# The renamed pairs feed induction-only engines (no base case), so a power-on
# mismatch must stay unpaired: same writes, word 0 initialized 1 vs 9.
(w / "init_names_ref.v").write_text(f"""
module init_names({ports});
  reg [7:0] mem [0:3];
  initial begin mem[0] = 8'd1; mem[1] = 8'd2; mem[2] = 8'd3; mem[3] = 8'd4; end
  always @(posedge clk) if (we) mem[wa] <= d;
  assign y = mem[ra];
endmodule
""")
for variant, word0 in (("inline", 1), ("bad", 9)):
    (w / f"init_names_{variant}.v").write_text(f"""
module init_names({ports});
  reg [31:0] __lhdmem_h6d656d_e_data;
  initial __lhdmem_h6d656d_e_data = {{8'd4, 8'd3, 8'd2, 8'd{word0}}};
  always @(posedge clk) if (we) __lhdmem_h6d656d_e_data[wa*8 +: 8] <= d;
  assign y = __lhdmem_h6d656d_e_data[ra*8 +: 8];
endmodule
""")
# A one-bit view of an LFSR hides its state from the plain proof: the replica
# registers must be paired before induction can close.
# A hand-written golden bank keeps one flop per word (`mem0`..`mem3`); the
# memory's word w pairs with `mem<w>` (glued lane, as pass/lec pairs banks).
(w / "bank_names_ref.v").write_text(f"""
module bank_names({ports});
  reg [7:0] mem0, mem1, mem2, mem3;
  always @(posedge clk) if (we) case (wa)
    2'd0: mem0 <= d;
    2'd1: mem1 <= d;
    2'd2: mem2 <= d;
    default: mem3 <= d;
  endcase
  assign y = ra == 2'd0 ? mem0 : ra == 2'd1 ? mem1 : ra == 2'd2 ? mem2 : mem3;
endmodule
""")
for variant, value in (("wrap", "d"), ("bad", "d ^ 8'h01")):
    (w / f"bank_names_{variant}.v").write_text(f"""
module bank_names({ports});
  array_memory __lhdmem_h6d656d_e (clk, we, wa, ra, d, y);
endmodule
module array_memory({ports});
  reg [7:0] data [0:3];
  always @(posedge clk) if (we) data[wa] <= {value};
  assign y = data[ra];
endmodule
""")
lane = """
module lane(input clk, input d, output q);
  reg [7:0] acc;
  always @(posedge clk) acc <= {acc[6:0], acc[7] ^ acc[5] ^ d};
  assign q = ^acc;
endmodule
"""
top = "module loop_names(input clk, input d0, d1, output q0, q1);"
(w / "loop_names_ref.v").write_text(lane + top + """
  lane lane__li0 (clk, d0, q0);
  lane lane__li1 (clk, d1, q1);
endmodule
""")
for variant, second in (("impl", "d1"), ("bad", "~d1")):
    (w / f"loop_names_{variant}.v").write_text(lane + f"""
module body(input clk, input d, output q);
  lane lane (clk, d, q);
endmodule
{top}
  body u_loop_0__li0 (clk, d0, q0);
  body u_loop_0__li1 (clk, {second}, q1);
endmodule
""")
PYRTL_NAMES
# Two cases at a time, like the blocks above (each is one lgcheck/yosys run).
case "$part" in
  rtl) set -- rtl_names:wrap rtl_names:inline rtl_names:bad ;;
  bank) set -- bank_names:wrap bank_names:bad ;;
  loop) set -- loop_names:impl loop_names:bad ;;
  init_names) set -- init_names:inline init_names:bad ;;
  *) set -- rtl_names:wrap rtl_names:inline rtl_names:bad bank_names:wrap bank_names:bad loop_names:impl loop_names:bad \
       init_names:inline init_names:bad ;;
esac
while [ "$#" -gt 0 ]; do
  batch="$1 ${2:-}"
  shift
  [ "$#" -eq 0 ] || shift
  for case in $batch; do
    top=${case%%:*}
    variant=${case##*:}
    mkdir -p "$W/${top}_$variant"
    (cd "$W/${top}_$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
      --yosys "$YOSYS_ABS" --top "$top" \
      --reference "$W/${top}_ref.v" --implementation "$W/${top}_$variant.v") \
      >"$W/${top}_$variant.log" 2>&1 &
    eval "${top}_${variant}_pid=$!"
  done
  for case in $batch; do
    top=${case%%:*}
    variant=${case##*:}
    eval "wait \"\${${top}_${variant}_pid}\""
    rc=$?
    if [ "$variant" = bad ]; then
      [ "$rc" -eq 1 ] || { cat "$W/${top}_$variant.log"; fail "$top: corrupted state update or power-on value was not refuted"; }
    else
      [ "$rc" -eq 0 ] || { cat "$W/${top}_$variant.log"; fail "$top ($variant): state correspondence was not proven"; }
      grep -q '^1r.Successfully matched' "$W/${top}_$variant.log" || fail "$top ($variant) did not exercise state-name recovery"
    fi
  done
done
echo "PASS: RTL array, flop bank, cgen wrapper/inline memory and rolled-loop replica names pair; corrupted updates and power-on values refute"

fi

if [ "$part" = all ] || [ "$part" = poweron ]; then
# Stages 1..1c and 4 pair state by NAME (equiv_make) or by STRUCTURE
# (equiv_struct) and prove it with induction-only engines, which ASSUME every
# pair equal at power-on and unroll through unpaired state as if a clock edge
# had set it. A pair that starts apart must fall through to the bounded miter:
# a same-named register initialized 1 vs 9 (stage 1, and 1c for its
# clk2fflogic model) and a cgen wrapper memory whose word 0 starts 1 vs 9,
# which equiv_struct merges with the golden array (stage 1m), must refute. So
# must a RENAMED register (`r` vs `s`, on no pair; only `q = r + 1` is) that
# starts 1 vs 9: the stage's base case from power-on sees it. The same
# power-on values still prove.
python3 - "$W" <<'PYPOWER_ON'
from pathlib import Path
import sys
w = Path(sys.argv[1])
for variant, value in (("ref", 1), ("impl", 1), ("bad", 9)):
    (w / f"flop_init_{variant}.v").write_text(f"""
module flop_init(input clk, input [3:0] d, output [3:0] q);
  reg [3:0] r = 4'd{value};
  always @(posedge clk) r <= d;
  assign q = r;
endmodule
""")
for variant, reg, value in (("ref", "r", 1), ("impl", "s", 1), ("bad", "s", 9)):
    (w / f"rename_init_{variant}.v").write_text(f"""
module rename_init(input clk, input [3:0] d, output [3:0] q);
  reg [3:0] {reg} = 4'd{value};
  always @(posedge clk) {reg} <= d;
  assign q = {reg} + 4'd1;
endmodule
""")
ports = "input clk, we, input [1:0] wa, ra, input [7:0] d, output [7:0] y"
(w / "wrap_init_ref.v").write_text(f"""
module wrap_init({ports});
  reg [7:0] mem [0:3];
  initial begin mem[0] = 8'd1; mem[1] = 8'd2; mem[2] = 8'd3; mem[3] = 8'd4; end
  always @(posedge clk) if (we) mem[wa] <= d;
  assign y = mem[ra];
endmodule
""")
for variant, word0 in (("wrap", 1), ("bad", 9)):
    (w / f"wrap_init_{variant}.v").write_text(f"""
module wrap_init({ports});
  array_memory __lhdmem_h6d656d_e (clk, we, wa, ra, d, y);
endmodule
module array_memory({ports});
  reg [7:0] data [0:3];
  initial begin data[0] = 8'd{word0}; data[1] = 8'd2; data[2] = 8'd3; data[3] = 8'd4; end
  always @(posedge clk) if (we) data[wa] <= d;
  assign y = data[ra];
endmodule
""")
PYPOWER_ON
# Two cases at a time, like the blocks above (each is one lgcheck/yosys run).
set -- flop_init:impl flop_init:bad wrap_init:wrap wrap_init:bad rename_init:impl rename_init:bad
while [ "$#" -gt 0 ]; do
  batch="$1 ${2:-}"
  shift
  [ "$#" -eq 0 ] || shift
  for case in $batch; do
    top=${case%%:*}
    variant=${case##*:}
    mkdir -p "$W/${top}_$variant"
    (cd "$W/${top}_$variant" && LGCHECK_EQUIV_TIMEOUT="$LGCHECK_BUDGET" "$LGCHECK" \
      --yosys "$YOSYS_ABS" --top "$top" \
      --reference "$W/${top}_ref.v" --implementation "$W/${top}_$variant.v") \
      >"$W/${top}_$variant.log" 2>&1 &
    eval "${top}_${variant}_pid=$!"
  done
  for case in $batch; do
    top=${case%%:*}
    variant=${case##*:}
    eval "wait \"\${${top}_${variant}_pid}\""
    rc=$?
    if [ "$variant" = bad ]; then
      [ "$rc" -eq 1 ] || { cat "$W/${top}_$variant.log"; fail "$top: a power-on mismatch was proven by a stage that assumes paired state starts equal"; }
      grep -q 'power-on value' "$W/${top}_$variant.log" || fail "$top (bad) did not exercise the power-on check"
    else
      [ "$rc" -eq 0 ] || { cat "$W/${top}_$variant.log"; fail "$top ($variant): equal power-on values were not proven"; }
    fi
  done
done
echo "PASS: same-named, structurally merged and renamed state starting at different power-on values refutes; equal values prove"

fi

if [ "$part" = all ] || [ "$part" = budget ]; then
# An expensive correspondence guess must leave time for another strategy.
# The fake solver isolates scheduling: the first attempt would outlast the
# whole four-second budget; the next attempt returns an explicit proof. The
# clock-domain guard gets a stateless design so the clock-blind strategies run.
# A budget this short can also be eaten by a slow or loaded machine (lgcheck
# meters it in whole $SECONDS ticks), and no fixed number of seconds is safe on
# every CPU. A timeout is never a failure: INCONCLUSIVE (exit 2) passes, only a
# wrong verdict or a proof without the later strategy fails.
cat >"$W/strategy_budget_yosys" <<'SHSTRATEGY'
#!/bin/sh
case "$*" in
  *"write_json lgcheck_clock_domains.json"*)
    echo '{"modules":{"gold":{},"gate":{}}}' >lgcheck_clock_domains.json ;;
  *"write_verilog trace1.v"*) exec sleep 6 ;;
  *"select -set state_outputs"*)
    # the pairs a real 1n dumps before proving (nothing paired here)
    echo '{"modules":{"equiv":{}}}' >lgcheck1n_pairs.json
    echo 'Equivalence successfully proven!' ;;
esac
exit 0
SHSTRATEGY
chmod +x "$W/strategy_budget_yosys"
mkdir -p "$W/strategy_budget"
cat >"$W/strategy_ref.v" <<'RTL'
module strategy(input d, output q);
  assign q = d;
endmodule
RTL
(cd "$W/strategy_budget" && LGCHECK_EQUIV_TIMEOUT=4 LGCHECK_HEURISTIC_TIMEOUT=1 \
  "$LGCHECK" --yosys "$W/strategy_budget_yosys" --top strategy \
  --reference "$W/strategy_ref.v" --implementation "$W/strategy_ref.v") \
  >"$W/strategy_budget.log" 2>&1
rc=$?

if [ "$rc" -eq 2 ]; then
  echo "PASS: strategy budget timed out on this machine (INCONCLUSIVE is not a failure)"
else
  [ "$rc" -eq 0 ] || { cat "$W/strategy_budget.log"; fail "strategy budget case gave a wrong verdict (exit $rc)"; }
  grep -q '^1n.Successfully matched' "$W/strategy_budget.log" || fail "later proof strategy did not run"
  echo "PASS: expensive matching attempt leaves budget for a later proof strategy"
fi
fi
