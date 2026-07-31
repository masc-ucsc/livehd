#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The Verilog -> Pyrope emission must be BYTE-REPRODUCIBLE.
#
# The reader linearizes several pointer-keyed `flat_hash_set`s before appending
# IR, and abseil perturbs such a table's iteration order by its heap address, so
# the order varies run to run under ASLR. Those sites were already sorted by
# `(location, name)` -- but that is NOT a total order: a generate-loop body is
# elaborated once per index, so EVERY replica of a symbol declared inside it
# shares both keys. `std::sort` then keeps whatever order it was handed.
#
# It was not cosmetic. `lname_of` numbers a colliding name `<base>_sN` in
# FIRST-REFERENCE order, so a shuffled declare order RENAMES the replicas: two
# back-to-back regenerations of minion emitted `intpipe_csr_msgs` pairing
# `oob_mem` with `oob_wr_ptr_s6` in one run and `_s7` in the next. A checked-in
# Pyrope tree cannot be LEC'd against a freshly compiled Verilog reference under
# that -- the two sides' state names permute and the miter compares the wrong
# replicas, which is what made `//bench:minion_lec`'s `intpipe_csr_msgs`
# counterexample move from run to run (`nxt:oob_rd_ptr_s5`, then `oob_mem_s2`,
# then `oob_wr_ptr_s6`).
#
# The fixture below is the minimal shape: one generate loop, N replicas of two
# same-named regs, so the ties are real. It compares two independent compiles of
# the same source; ASLR differs between them, which is exactly the perturbation.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/slangdet}"
mkdir -p "$WORK"
fail=0

cat > "$WORK/genrep.sv" <<'EOF'
module genrep #(parameter int N = 8) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic [N-1:0]     push_i,
  input  logic [N-1:0]     pop_i,
  output logic [N-1:0]     full_o,
  output logic [N-1:0]     head_o
);
  for (genvar g = 0; g < N; ++g) begin : gen_slot
    // Same declaration, elaborated N times: every replica shares BOTH the
    // source location and the name, so (location, name) cannot order them.
    logic [3:0] wr_ptr, rd_ptr;
    logic [7:0] slot_mem;

    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        wr_ptr   <= '0;
        rd_ptr   <= '0;
        slot_mem <= '0;
      end else begin
        if (push_i[g]) begin
          slot_mem[wr_ptr[2:0]] <= 1'b1;
          wr_ptr                <= wr_ptr + 1'b1;
        end
        if (pop_i[g]) begin
          rd_ptr <= rd_ptr + 1'b1;
        end
      end
    end

    assign full_o[g] = (wr_ptr - rd_ptr) >= 4'd7;
    assign head_o[g] = slot_mem[rd_ptr[2:0]];
  end
endmodule
EOF

emit() {  # $1 = output dir
  rm -rf "$WORK/$1" "$WORK/w_$1"
  $LHD compile "$WORK/genrep.sv" --top genrep --emit-dir "pyrope:$WORK/$1" --workdir "$WORK/w_$1" \
       >/dev/null 2>&1 || { echo "FAIL: compile ($1)"; exit 1; }
}

emit a
emit b

if cmp -s "$WORK/a/genrep.prp" "$WORK/b/genrep.prp"; then
  echo "ok: two compiles of the same source emit byte-identical Pyrope"
else
  echo "FAIL: Verilog -> Pyrope emission is not reproducible"
  diff "$WORK/a/genrep.prp" "$WORK/b/genrep.prp" | head -30
  fail=1
fi

# The identity half, stated separately so a regression reads clearly: each
# replica's `_sN` suffix must land on the SAME replica both times. Comparing the
# lines that PAIR two uniquified names catches a permutation even if some other
# (harmless) statement order were to drift.
for pat in 'wr_ptr' 'rd_ptr' 'slot_mem'; do
  ka=$(grep -oE "${pat}(_s[0-9]+)?" "$WORK/a/genrep.prp" | sort -u | tr '\n' ' ')
  kb=$(grep -oE "${pat}(_s[0-9]+)?" "$WORK/b/genrep.prp" | sort -u | tr '\n' ' ')
  if [ "$ka" != "$kb" ]; then
    echo "FAIL: '$pat' name set differs -> [$ka] vs [$kb]"; fail=1
  fi
done
# Pairing: every `head_o` bit reads one slot_mem replica with one rd_ptr
# replica. Those pairs are the thing a shuffled `_sN` numbering breaks.
if ! diff <(grep -a 'head_o' "$WORK/a/genrep.prp") <(grep -a 'head_o' "$WORK/b/genrep.prp") >/dev/null; then
  echo "FAIL: the (slot_mem, rd_ptr) replica PAIRING is not stable across runs"
  diff <(grep -a 'head_o' "$WORK/a/genrep.prp") <(grep -a 'head_o' "$WORK/b/genrep.prp") | head -20
  fail=1
fi

if [ $fail -ne 0 ]; then echo "slang_emit_determinism: FAILED"; exit 1; fi
echo "slang_emit_determinism: PASS"
