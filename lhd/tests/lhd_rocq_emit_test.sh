#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `--emit-dir rocq:DIR/` (pass.rocq): the LGraph -> Rocq exporter emits a fast
# model file and a graph certificate file per design.
#
# This test deliberately does NOT require Rocq to be installed -- it asserts on
# the emitted TEXT only, so it stays green in CI on a machine with no prover.
# Typechecking the output is the job of scripts/run_dino_lgraph_rocq.sh.
#
# What it locks down:
#   - both files are produced, and _CoqProject lists them;
#   - the records/definitions the proof stack depends on by NAME exist;
#   - certificate ids are spelled %N, never bare nat.  Rocq's nat is unary, so a
#     LiveHD node id of 2000000000 as a nat literal would build a term with two
#     billion successors the moment anything reduced it;
#   - no `sorry`/`admit` marker leaks into a default (cert_wf=skip) run;
#   - --emit rocq:FILE (single-file form) is rejected: rocq: is a directory
#     container, like isabelle: and lean:.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_rocq_emit_$$}"
mkdir -p "$W/out"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat >"$W/simple_add.v" <<'EOF'
module simple_add(
  input  [3:0] a,
  input  [3:0] b,
  output [3:0] y
);
  assign y = a + b;
endmodule
EOF

"$LHD" compile verilog "$W/simple_add.v" \
  --reader yosys-verilog \
  --top simple_add \
  --emit-dir rocq:"$W/out" \
  --workdir "$W/work" >"$W/run.json" 2>"$W/run.err" \
  || fail "lhd compile --emit-dir rocq: failed: $(cat "$W/run.err")"

model="$W/out/simple_add_Lgraph.v"
cert="$W/out/simple_add_Lgraph_Cert.v"

[ -f "$model" ] || fail "missing fast model $model"
[ -f "$cert" ] || fail "missing certificate $cert"
[ -f "$W/out/_CoqProject" ] || fail "missing _CoqProject"

# --- fast model shape ---------------------------------------------------------
grep -q 'Record simple_add_in' "$model" || fail "no simple_add_in record: $(cat "$model")"
grep -q 'Record simple_add_out' "$model" || fail "no simple_add_out record"
grep -q 'Definition simple_add_comb' "$model" || fail "no simple_add_comb"
grep -q 'From RocqSemanticPrimitives Require Import SemanticPrimitives' "$model" \
  || fail "model does not import the support library"
# Purely combinational design: no state record, no _next/_step.
grep -q 'Record simple_add_state' "$model" && fail "combinational design must not emit a state record"

# --- certificate shape --------------------------------------------------------
grep -q 'Definition simple_add_nodeCerts : list NodeCert' "$cert" || fail "no nodeCerts"
grep -q 'Definition simple_add_graphCert : GraphCert' "$cert" || fail "no graphCert"
grep -q 'Definition simple_add_sourceEnv' "$cert" || fail "no sourceEnv"
grep -q 'Definition simple_add_comb_cert' "$cert" || fail "no comb_cert"
grep -q 'apply evalGraphCorrectForCert' "$cert" || fail "keystone not instantiated"
grep -q 'From LiveHD Require Import simple_add_Lgraph' "$cert" \
  || fail "certificate does not require the model through its -Q logical path"
# The certificate must be non-empty (a stub would silently pass everything else).
grep -q 'nc_nid :=' "$cert" || fail "certificate has no node entries"

# --- ids must be N, not nat ---------------------------------------------------
grep -q 'nc_deps := \[.*\]%N' "$cert" || fail "dep lists are not %N-scoped: $(grep nc_deps "$cert" | head -1)"
grep -q 'gc_topo := \[.*\]%N' "$cert" || fail "gc_topo is not %N-scoped"
grep -q 'N.eqb n ' "$cert" || fail "sourceEnv does not dispatch on N.eqb"
grep -q '%nat' "$cert" && fail "certificate ids must be %N (Rocq nat is unary): $(grep -n '%nat' "$cert" | head -3)"

# --- no proof holes in a default run -----------------------------------------
for f in "$model" "$cert"; do
  grep -qE 'Admitted|admit\.|TODO pass\.rocq' "$f" && fail "proof hole or TODO marker in $f"
done

# --- _CoqProject lists both units ---------------------------------------------
grep -q 'simple_add_Lgraph.v' "$W/out/_CoqProject" || fail "_CoqProject missing the model"
grep -q 'simple_add_Lgraph_Cert.v' "$W/out/_CoqProject" || fail "_CoqProject missing the certificate"

# --- emit_cert=false emits the model only -------------------------------------
mkdir -p "$W/out_nocert"
"$LHD" compile verilog "$W/simple_add.v" \
  --reader yosys-verilog \
  --top simple_add \
  --emit-dir rocq:"$W/out_nocert" \
  --workdir "$W/work_nocert" \
  --set formal.rocq.emit_cert=false >/dev/null 2>&1 \
  || fail "emit_cert=false run failed"
[ -f "$W/out_nocert/simple_add_Lgraph.v" ] || fail "emit_cert=false dropped the model"
[ -f "$W/out_nocert/simple_add_Lgraph_Cert.v" ] && fail "emit_cert=false still wrote a certificate"

# --- rocq: is a directory container, never a single --emit file ---------------
if "$LHD" compile verilog "$W/simple_add.v" \
     --reader yosys-verilog --top simple_add \
     --emit rocq:"$W/single.v" --workdir "$W/work_single" >/dev/null 2>&1; then
  fail "--emit rocq:FILE must be rejected (rocq: is a directory container)"
fi

echo "PASS"
