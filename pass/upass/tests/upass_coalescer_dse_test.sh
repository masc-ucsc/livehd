#!/bin/sh

set -eu

LGSHELL="${TEST_SRCDIR}/${TEST_WORKSPACE}/main/lgshell"

# Pyrope source: write `tmp` three times before its first read, with each RHS
# referencing an input port (`$a`) so constprop cannot fold it. coalescer:1
# should drop the first two `tmp = ___N` assigns (dead stores); coalescer:0
# should keep all three.

PRP="${TEST_TMPDIR}/coalescer_dse.prp"
cat >"${PRP}" <<'EOF'
/*
:name: coalescer_dse
:type: upass
*/

mut tmp = $a + 1
tmp = $a + 2
tmp = $a + 3
%out = tmp
EOF

run_pipeline() {
  local toggle="$1"
  local out="$2"
  printf 'inou.prp files:%s |> pass.lnastfmt |> pass.upass coalescer:%s verifier:false |> lnast.dump\nquit\n' "${PRP}" "${toggle}" \
    | HOME="${TEST_TMPDIR}" "${LGSHELL}" >"${out}" 2>&1
}

OUT0="${TEST_TMPDIR}/coalescer_dse.0.out"
OUT1="${TEST_TMPDIR}/coalescer_dse.1.out"
run_pipeline 0 "${OUT0}"
run_pipeline 1 "${OUT1}"

# Count `assign:` stmts whose first child is `tmp` — these are the dead-store
# candidates. coalescer:0 should see >=3 (one per source-level `tmp = …`);
# coalescer:1 should see exactly 1 (only the live store before %out reads tmp).
count_tmp_assigns() {
  # Task 1t — statement-level writes lower to `store` (formerly `assign`); the
  # dead-store behavior is unchanged, only the node name in the dump.
  awk '
    /store$/ { in_assign = 1; next }
    in_assign {
      if ($0 ~ /ref '\''tmp'\''$/) count++
      in_assign = 0
    }
    END { print count + 0 }
  ' "$1"
}

count0=$(count_tmp_assigns "${OUT0}")
count1=$(count_tmp_assigns "${OUT1}")

echo "coalescer:0 tmp-assign count = ${count0}"
echo "coalescer:1 tmp-assign count = ${count1}"

if [ "${count0}" -lt 3 ]; then
  echo "FAIL: coalescer:0 was expected to keep >=3 tmp-assigns (saw ${count0})"
  cat "${OUT0}"
  exit 1
fi

if [ "${count1}" -ne 1 ]; then
  echo "FAIL: coalescer:1 was expected to keep exactly 1 tmp-assign (saw ${count1})"
  cat "${OUT1}"
  exit 2
fi

echo "PASS: coalescer drops ${count0} -> ${count1} tmp-assigns"
