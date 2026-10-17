#!/usr/bin/env bash
# Negative and positive controls for the axiom gate (Projection/AxiomGate.lean).
#
# The controls file pins every expected message with `#guard_msgs`, so it
# compiles cleanly exactly when the gate accepts the standard axioms and
# refuses `sorryAx`, a user axiom and a `_native` axiom (the native variant
# admitting only the last). Needs the gate's olean: run
# `formal/lean/.build-proj.sh Projection/AxiomGate` first.
set -euo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$R/formal/lean"
if [ ! -f .olean-dev/LeanSemanticPrimitives/Projection/AxiomGate.olean ]; then
  echo "axiom_gate_controls: build the gate first (.build-proj.sh Projection/AxiomGate)" >&2
  exit 2
fi
out=$(LEAN_PATH="$PWD/.olean-dev" lean "$R/pass/lean/tests/fp/axiom_gate_controls.lean" 2>&1) || {
  echo "$out"; echo "axiom_gate_controls: FAIL"; exit 1; }
# The only permitted diagnostics are the two `sorry` declarations the controls need.
extra=$(echo "$out" | grep -v 'declaration uses `sorry`' | grep -v '^$' || true)
if [ -n "$extra" ]; then echo "$extra"; echo "axiom_gate_controls: FAIL (unexpected output)"; exit 1; fi
echo "axiom_gate_controls: PASS (7 controls)"
