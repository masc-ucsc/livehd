#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# FIXME tracker for todo/livehd/2f-latch (M3) — THE CONTRACT CHECK. The
# no-time-borrowing scope ruling is an ABSTRACTION PRECONDITION, not a
# semantics: modeling a latch as a flop-with-enable that commits at the closing
# edge of its window is only sound when nothing observes the transparent
# window's timing. M3's classifier must FAIL CLOSED with a directed diagnostic
# on designs that violate that precondition — never silently approximate them:
#
#   1. self-gated latch      — the gate is derived from the latch's own Q
#                              (a self-timed loop; rule 3: the gate must be a
#                              recognizable clock-phase expression)
#   2. Q feeds its own D     — combinational path Q -> D on one latch
#                              (transparent self-update / oscillator; rule 2)
#   3. same-phase latch pair — a comb path between two latches that are
#                              SIMULTANEOUSLY transparent (same gate net, same
#                              polarity; rule 1 — this is where real time
#                              borrowing lives)
#
# All three compile clean and silently mis-lower TODAY (verified 2026-07-20) —
# that is the bug. Each must exit NONZERO with a diagnostic that names the
# latch contract once M3 lands. The check is expected at compile time (the
# classifier is one shared analysis); if M3 lands it in lec/sim entry points
# instead, update the driver line here, not the expectation.
#
# Tagged `fixme` so `bazel test //...` stays green. Run it with:
#   bazel test //lhd/tests:latch_contract_check_test --test_tag_filters=

set -u

LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# case 1: gate derived from the latch's own Q
cat > "$W/selfgate.v" <<'EOF'
module dut(input [7:0] d, output reg [7:0] q);
  wire g = ~q[0];
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
EOF

# case 2: comb path from Q back to its own D
cat > "$W/selfd.v" <<'EOF'
module dut(input en, output reg [7:0] q);
  always_latch begin
    if (en)
      q <= q + 8'd1;
  end
endmodule
EOF

# case 3: comb path between two SIMULTANEOUSLY transparent latches
cat > "$W/samephase.v" <<'EOF'
module dut(input en, input [7:0] d, output reg [7:0] b);
  reg [7:0] a;
  always_latch begin
    if (en)
      a <= d;
  end
  always_latch begin
    if (en)
      b <= a ^ 8'h5a;
  end
endmodule
EOF

expect_rejected() {
  local name="$1" what="$2"
  local out rc
  out="$("$LHD" compile "$W/$name.v" --reader slang --top dut \
          --workdir "$W/w_$name" 2>&1)"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    fail "$what: compiled clean (exit 0) — the latch contract check did not fire"
  fi
  # The rejection must be the DIRECTED diagnostic, not an unrelated crash.
  echo "$out" | grep -qiE "latch" \
    || fail "$what: exited nonzero but without a latch-contract diagnostic: $out"
  echo "ok: $what rejected with a latch diagnostic"
}

expect_rejected selfgate  "self-gated latch (gate from own Q)"
expect_rejected selfd     "latch Q feeding its own D"
expect_rejected samephase "comb path between simultaneously-transparent latches"

echo "PASS: the latch contract check fails closed on all three violation shapes"
