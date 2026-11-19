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

# Cross-FILE ties. A slang SourceLocation orders by buffer id, and buffer ids
# follow the order files are loaded (slang loads in parallel, so on vortex the
# order changed from run to run). Two interface definitions in two files, read
# in both command-line orders, must lower identically: the signals' declare
# order is by file name and offset, never by load order. Before the fix the two
# orders swapped the `__if` declarations, renumbered LNAST temporaries and
# changed the compiled graphs' bytes, so a warm `lhd synth` missed its completed
# placement (physical cache keyed on the compiled library).
cat > "$WORK/zeta_if.sv" <<'EOF'
interface zeta_if;
  logic       valid;
  logic [7:0] data;
  modport src(output valid, output data);
endinterface
EOF
cat > "$WORK/alpha_if.sv" <<'EOF'
interface alpha_if;
  logic       ready;
  logic [3:0] tag;
  modport src(output ready, output tag);
endinterface
EOF
cat > "$WORK/xfile.sv" <<'EOF'
module xfile_leaf(input logic clk, input logic [7:0] d, zeta_if.src z, alpha_if.src a);
  always_ff @(posedge clk) begin
    z.valid <= d[0];
    z.data  <= d;
    a.ready <= d[1];
    a.tag   <= d[5:2];
  end
endmodule
module xfile(input logic clk, input logic [7:0] d, output logic [7:0] o, output logic r);
  zeta_if  z0();
  alpha_if a0();
  zeta_if  z1();
  alpha_if a1();
  xfile_leaf u0(.clk(clk), .d(d),  .z(z0), .a(a0));
  xfile_leaf u1(.clk(clk), .d(~d), .z(z1), .a(a1));
  assign o = z0.data ^ z1.data ^ {a0.tag, a1.tag};
  assign r = z0.valid & z1.valid & a0.ready & a1.ready;
endmodule
EOF
xemit() {  # $1 = output tag, rest = sources in command-line order
  local tag=$1; shift
  rm -rf "$WORK/x_$tag" "$WORK/xl_$tag" "$WORK/xw_$tag"
  $LHD compile "$@" --top xfile --emit-dir "pyrope:$WORK/x_$tag" --emit-dir "lg:$WORK/xl_$tag" \
       --workdir "$WORK/xw_$tag" >"$WORK/x_$tag.log" 2>&1 || { cat "$WORK/x_$tag.log"; echo "FAIL: compile ($tag)"; exit 1; }
}
xemit fwd "$WORK/zeta_if.sv" "$WORK/alpha_if.sv" "$WORK/xfile.sv"
xemit rev "$WORK/xfile.sv" "$WORK/alpha_if.sv" "$WORK/zeta_if.sv"
if diff -r "$WORK/x_fwd" "$WORK/x_rev" >/dev/null; then
  echo "ok: interface files read in either order emit byte-identical Pyrope"
else
  echo "FAIL: Pyrope emission depends on the source-file order"
  diff -r "$WORK/x_fwd" "$WORK/x_rev" | head -30
  fail=1
fi
xdiff=0
for f in $(cd "$WORK/xl_fwd" && find . -type f -name '*.bin' | sort); do
  cmp -s "$WORK/xl_fwd/$f" "$WORK/xl_rev/$f" || { echo "FAIL: compiled graph $f differs with the source-file order"; xdiff=1; }
done
[ $xdiff -eq 0 ] && echo "ok: compiled graphs are byte-identical in either source-file order" || fail=1

if [ $fail -ne 0 ]; then echo "slang_emit_determinism: FAILED"; exit 1; fi
echo "slang_emit_determinism: PASS"
