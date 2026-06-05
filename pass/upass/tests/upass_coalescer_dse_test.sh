#!/bin/sh
# Coalescer dead-store elimination via the lhd kernel. The post-upass LNAST
# is observed through `--emit-dir lnast-dump:` (the lnast.dump printer).

set -eu

LHD="${TEST_SRCDIR}/${TEST_WORKSPACE}/lhd/lhd"

# Pyrope source: write `tmp` three times before its first read, with each RHS
# referencing an unknown-bit value (`a = 0ub?`) so constprop cannot fold it.
# coalescer:1 should drop the first two `tmp = ___N` assigns (dead stores);
# coalescer:0 should keep all three. Keep these as bare top-level statements
# (no `comb`/`mod` wrapper): a lambda body would be SSA-renamed (`tmp`,
# `tmp___ssa_1`, …) and the same-name DSE the coalescer performs would no
# longer be observable in the dump.

PRP="${TEST_TMPDIR}/coalescer_dse.prp"
cat >"${PRP}" <<'EOF'
/*
:name: coalescer_dse
:type: upass
*/

mut a = 0ub?
mut tmp = a + 1
tmp = a + 2
tmp = a + 3
mut out = tmp
EOF

run_pipeline() {
  toggle="$1"
  dumpdir="$2"
  "${LHD}" compile "${PRP}" --set upass.coalescer="${toggle}" \
    --emit-dir lnast-dump:"${dumpdir}/" --workdir "${TEST_TMPDIR}/w${toggle}" -q \
    --result-json "${TEST_TMPDIR}/r${toggle}.json" >/dev/null 2>&1
}

DUMP0="${TEST_TMPDIR}/dump0"
DUMP1="${TEST_TMPDIR}/dump1"
run_pipeline 0 "${DUMP0}"
run_pipeline 1 "${DUMP1}"

# Count `store` stmts whose first child is `tmp` — these are the dead-store
# candidates. coalescer:0 should see >=3 (one per source-level `tmp = …`);
# coalescer:1 should see exactly 1 (only the live store before `out` reads tmp).
count_tmp_assigns() {
  # Task 1t — statement-level writes lower to `store` (formerly `assign`); the
  # dead-store behavior is unchanged, only the node name in the dump. A store
  # may carry a source span (`store @(loc=…)` — the declare/store loc-carry
  # chain), so match both the bare and the loc-annotated form.
  cat "$1"/*.lnast | awk '
    /store$/ || /store @\(loc=/ { in_assign = 1; next }
    in_assign {
      if ($0 ~ /ref '\''tmp'\''$/) count++
      in_assign = 0
    }
    END { print count + 0 }
  '
}

count0=$(count_tmp_assigns "${DUMP0}")
count1=$(count_tmp_assigns "${DUMP1}")

echo "coalescer:0 tmp-assign count = ${count0}"
echo "coalescer:1 tmp-assign count = ${count1}"

if [ "${count0}" -lt 3 ]; then
  echo "FAIL: coalescer:0 was expected to keep >=3 tmp-assigns (saw ${count0})"
  cat "${DUMP0}"/*.lnast
  exit 1
fi

if [ "${count1}" -ne 1 ]; then
  echo "FAIL: coalescer:1 was expected to keep exactly 1 tmp-assign (saw ${count1})"
  cat "${DUMP1}"/*.lnast
  exit 2
fi

echo "PASS: coalescer drops ${count0} -> ${count1} tmp-assigns"
