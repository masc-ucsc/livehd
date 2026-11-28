#!/usr/bin/env bash
# scanner_dcert_test.sh: multi-domain certificates from the real scanner reach Lean (case C12 of
# the format test plan).
#
# design_scan_test (DESIGN_SCAN_TEST, the //pass/lean:design_scan_test binary) scans small graphs
# with two clocks and, when its variables ask, writes each one's DCERT4 bytes beside its Lean
# literal (both from one CertificateIR):
#   clk   DesignScanClocks.TwoDomainValueOracle        LHD_D4_CLOCK_DCERT, LHD_LEAN_CLOCK_FIXTURE
#         two flops, each on its own clock
#   mem   DesignScanClocks.MemoryQuietDomainValueOracle  LHD_D4_MEM_DCERT, LHD_LEAN_MEM_FIXTURE
#         a 4-word RAM and a ROM on two clocks, a read register on each
#   memb  DesignScanClocks.MemoryNonDefaultDomainCertificate
#                                                      LHD_D4_MEM_B_DCERT, LHD_D4_MEM_B_FIXTURE
#         the same, with the RAM on clock ordinal 1
# Checks:
#   - the three gtests ran and passed, none skipped, and every file a variable names was written:
#     a hook that writes nothing fails, it never passes by not running;
#   - each certificate is admitted by the loader every driver uses (dcert_summary.lean) and has
#     the shape the C++ tests assert: clk two clocks and its two flops on different ones; mem two
#     clocks, the two read registers on different ones, one memory (2-bit address, depth 4, no
#     commit table) on the first register's clock; memb one memory on clock 1, no commit table,
#     and a read register on its clock;
#   - dcert_parity.py on each: the bytes are renderCert of the Lean literal (equal bytes are the
#     same certificate), the verified reader re-renders them and admits them, and the sensitivity
#     and refusal controls ran (clk has no memory: its memory controls may skip, and must be the
#     only ones that do).
# Prints `PASS scanner_dcert_test: ...` and exits 0, or a FAIL line and exits 1.
#
# Environment: DESIGN_SCAN_TEST (default bazel-bin/pass/lean/design_scan_test), OUT (default
# generated/scanner_dcert_test). Runs interpreted against formal/lean/.olean-dev.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
L="$ROOT/formal/lean"
OUT="${OUT:-$ROOT/generated/scanner_dcert_test}"
BIN="${DESIGN_SCAN_TEST:-$ROOT/bazel-bin/pass/lean/design_scan_test}"
fail() { echo "FAIL: $*"; exit 1; }
[[ -x "$BIN" ]] || { echo "FAIL: no design_scan_test at $BIN (bazel build //pass/lean:design_scan_test)"; exit 2; }
command -v lean > /dev/null || { echo "FAIL: lean is not on PATH"; exit 2; }
bash "$SCRIPT_DIR/check_fresh.sh" "$ROOT" || exit 1
rm -rf -- "$OUT"
mkdir -p "$OUT/run"

# 1. the scanner writes the certificates (from a folder of its own: the test keeps scratch files)
TESTS=DesignScanClocks.TwoDomainValueOracle:DesignScanClocks.MemoryQuietDomainValueOracle
TESTS+=:DesignScanClocks.MemoryNonDefaultDomainCertificate
(cd "$OUT/run" && env LHD_LEAN_CLOCK_FIXTURE="$OUT/clk.lean" LHD_D4_CLOCK_DCERT="$OUT/clk.dcert" \
   LHD_LEAN_MEM_FIXTURE="$OUT/mem.lean" LHD_D4_MEM_DCERT="$OUT/mem.dcert" \
   LHD_D4_MEM_B_FIXTURE="$OUT/memb.lean" LHD_D4_MEM_B_DCERT="$OUT/memb.dcert" \
   "$BIN" --gtest_filter="$TESTS") > "$OUT/gtest.log" 2>&1 \
  || { tail -20 "$OUT/gtest.log"; fail "design_scan_test (log $OUT/gtest.log)"; }
grep -q '^\[  PASSED  \] 3 tests' "$OUT/gtest.log" || fail "design_scan_test did not pass 3 tests (log $OUT/gtest.log)"
if grep -q '^\[  SKIPPED \]' "$OUT/gtest.log"; then fail "a design_scan_test case skipped (log $OUT/gtest.log)"; fi
for f in clk.dcert clk.lean mem.dcert mem.lean memb.dcert memb.lean; do
  [[ -s "$OUT/$f" ]] || fail "design_scan_test wrote no $f: the hook its variable asks wrote nothing"
done
echo "ok: design_scan_test wrote clk, mem and memb (DCERT4 and the Lean literal)"

# 2. each one admits, with the shape the C++ tests assert
(cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$SCRIPT_DIR/dcert_summary.lean" \
   "$OUT/clk.dcert" "$OUT/mem.dcert" "$OUT/memb.dcert") > "$OUT/summary.log" 2>&1 \
  || { cat "$OUT/summary.log"; fail "a scanner certificate is refused"; }
line() { grep -F "$OUT/$1.dcert: admitted: " "$OUT/summary.log"; }
re='clocks 2; flop clocks \[([01]), ([01])\]; memories \[\]$'
[[ "$(line clk)" =~ $re && ${BASH_REMATCH[1]} != "${BASH_REMATCH[2]}" ]] \
  || fail "clk is not two flops on two clocks: $(line clk)"
re='clocks 2; flop clocks \[([01]), ([01])\]; memories \[2/8/4 on clock ([01]) with 0 commits\]$'
[[ "$(line mem)" =~ $re && ${BASH_REMATCH[1]} != "${BASH_REMATCH[2]}" && ${BASH_REMATCH[3]} == "${BASH_REMATCH[1]}" ]] \
  || fail "mem: not one 4-word memory on its read register's clock, the registers on two clocks: $(line mem)"
re='clocks 2; flop clocks \[([01, ]*)\]; memories \[2/8/4 on clock 1 with 0 commits\]$'
[[ "$(line memb)" =~ $re && ", ${BASH_REMATCH[1]}," == *", 1,"* ]] \
  || fail "memb: not one 4-word memory on clock 1 with a read register on it: $(line memb)"
echo "ok: each admits in Lean with the shape the C++ tests assert"

# 3. the C++ bytes against renderCert of the Lean literal, with the verified reader's controls
LEANBIN=$(dirname "$(command -v lean)")
for d in clk mem memb; do
  base=$(sed -n 's/^def \(.*\)_designCert : DesignCert :=$/\1/p' "$OUT/$d.lean" | head -1)
  [[ -n "$base" ]] || fail "$d.lean defines no <base>_designCert"
  skip=(); [[ $d == clk ]] && skip=(--allow-skip)
  python3 -I "$ROOT/pass/lean/Exporter/DesignCert/scripts/dcert_parity.py" --base "$base" --lean "$OUT/$d.lean" \
    --dcert "$OUT/$d.dcert" --out "$OUT/parity_$d" --toolchain "$(dirname "$LEANBIN")" "${skip[@]}" \
    > "$OUT/parity_$d.log" 2>&1 && grep -q '^DCERT4-PARITY OK$' "$OUT/parity_$d.log" \
    || { tail -15 "$OUT/parity_$d.log"; fail "dcert_parity on $d (log $OUT/parity_$d.log)"; }
  # only the controls the design has nothing for may skip
  want=0; [[ $d == clk ]] && want=1
  n=$(grep -c '^  --   ' "$OUT/parity_$d.log")
  [[ $n == "$want" ]] || fail "dcert_parity on $d skipped $n blocks of controls, expected $want"
  if [[ $d == clk ]] && ! grep -q '^  --   no memories' "$OUT/parity_$d.log"; then
    fail "dcert_parity on clk skipped something other than the memory controls"
  fi
done
echo "ok: dcert_parity on clk, mem and memb: DCERT4-PARITY OK"
echo "PASS scanner_dcert_test: 3 multi-domain scanner certificates admitted in Lean, shapes as asserted, parity OK"
