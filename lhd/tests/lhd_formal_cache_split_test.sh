#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# 2f-fcore F2/F4: serialized verify-obligation cache + shared case splitting.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_formal_cache_split_$$}"
mkdir -p "$W/wd"

fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/split.prp" <<'EOF'
mod split(sel:u3, en:bool) -> (value:u16@[0]) {
  reg onehot:u16 = 0
  value = onehot
  assert((onehot & (onehot - 1)) == 0, "onehot invariant")
  if en {
    onehot = 1 << sel
  }
}
EOF

run() {
  "$LHD" formal verify "$W/split.prp" --top split --workdir "$W/wd" \
    --set formal.bound=5 --set formal.partitions=4 --set formal.split=sel \
    --set formal.prpfailrun=false >"$1" 2>&1
}

run "$W/first.out" || fail "first verify run failed: $(cat "$W/first.out")"
grep -q 'PROVEN (bounded)' "$W/first.out" || fail "invariant did not prove: $(cat "$W/first.out")"
grep -q 'case-split sel' "$W/first.out" || fail "verify did not use the selected control input: $(cat "$W/first.out")"
[ -s "$W/wd/formal_cache.json" ] || fail "formal_cache.json was not written"
grep -q '"verify:' "$W/wd/formal_cache.json" || fail "cache has no serialized verify-obligation key"

run "$W/second.out" || fail "second verify run failed: $(cat "$W/second.out")"
grep -Eq 'formal\[cache\]: [1-9][0-9]* obligation hit' "$W/second.out" \
  || fail "second run had no obligation cache hits: $(cat "$W/second.out")"

echo "PASS: verify serialized-obligation cache + formal case split"
