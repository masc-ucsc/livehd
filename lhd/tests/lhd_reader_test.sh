#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Native Slang remains the default; yosys selects the integrated read_slang
# frontend, with yosys-slang retained as a compatibility alias.

set -u

LHD=lhd/lhd
SV=lhd/tests/trivial.v
W="${TEST_TMPDIR:-/tmp/lhd_reader_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# slang reader: SV -> LNAST -> ln: (Forest dir)
"$LHD" compile "$SV" --reader slang --emit-dir ln:"$W/lns/" --workdir "$W/w1" -q --result-json "$W/r1.json" \
  || fail "slang reader ln: emit exited non-zero: $(cat "$W/r1.json" 2>/dev/null)"
[ -f "$W/lns/forest.txt" ] || fail "slang reader produced no forest.txt"
grep -q '"inou.slang' "$W/r1.json" || fail "expected an inou.slang step: $(cat "$W/r1.json")"

# slang reader: pyrope: re-emission (post-upass prp_writer)
"$LHD" compile "$SV" --reader slang --emit-dir pyrope:"$W/prps/" --workdir "$W/w2" -q --result-json "$W/r2.json" \
  || fail "slang reader pyrope: emit exited non-zero: $(cat "$W/r2.json" 2>/dev/null)"
ls "$W/prps/"*.prp >/dev/null 2>&1 || fail "slang reader produced no .prp re-emission"

# slang reader: verilog: emit goes through the same upass+tolg pipeline as
# pyrope sources (todo/ 2s lifted the old "stops at LNAST" gate)
"$LHD" compile "$SV" --reader slang --emit verilog:"$W/x.v" --workdir "$W/w3" -q --result-json "$W/r3.json" 2>/dev/null \
  || fail "slang reader verilog: emit exited non-zero: $(cat "$W/r3.json" 2>/dev/null)"
[ -s "$W/x.v" ] || fail "slang reader produced no verilog"

# yosys-verilog reader: the plain yosys verilog frontend, end-to-end
"$LHD" compile "$SV" --reader yosys-verilog --emit verilog:"$W/yv.v" --workdir "$W/w4" -q --result-json "$W/r4.json" \
  || fail "yosys-verilog reader exited non-zero: $(cat "$W/r4.json" 2>/dev/null)"
[ -s "$W/yv.v" ] || fail "yosys-verilog reader produced no verilog"

# The concise Yosys reader selects the SystemVerilog-capable frontend.
"$LHD" compile "$SV" --reader yosys --emit verilog:"$W/y.v" --workdir "$W/wy" -q --result-json "$W/ry.json" \
  || fail "yosys reader exited non-zero"
[ -s "$W/y.v" ] || fail "yosys reader produced no verilog"
grep -q 'inou.yosys.tolg' "$W/ry.json" || fail "yosys alias did not use Yosys"

# An unknown reader spelling is still a usage error (argv-stage errors write the
# result JSON to stdout, --result-json is not parsed yet).
out=$("$LHD" compile "$SV" --reader yosys-sverilog --emit-dir lg:"$W/lg/" -q 2>/dev/null) \
  && fail "--reader yosys-sverilog (unknown spelling) must be a usage error"
grep -q '"class":"usage"' <<<"$out" || fail "expected error.class=usage: $out"

echo "ok: native Slang, yosys-verilog and read_slang readers"

# Generated parameter specializations exceed filesystem component limits.
# Distinct long names sharing a prefix must survive emission and concatenation.
long_name=$(printf 'specialized_%0250d' 0)
cat > "$W/long.sv" <<EOF
module ${long_name}a(input a, output y); assign y = ~a; endmodule
module ${long_name}b(input a, output y); assign y = a; endmodule
module top(input a, output [1:0] y);
  ${long_name}a u0(a, y[0]);
  ${long_name}b u1(a, y[1]);
endmodule
EOF
"$LHD" compile "$W/long.sv" --top top --emit verilog:"$W/long.v" --workdir "$W/long-work" -q \
  || fail "long module names failed native compilation"
# The long-name policy is ONE mapping (livehd::unit_file_stem, core/
# file_name.hpp) shared by every per-unit emitter, so EVERY --emit-dir kind
# that names a file after a unit has to survive the same design. `ln:`/`lg:`
# name their files by index, so they are not part of this set.
for kind in verilog pyrope lnast-dump sim; do
  for run in first second; do
    "$LHD" compile "$W/long.sv" --top top --emit-dir "$kind:$W/long-$kind-$run" \
      --workdir "$W/long-$kind-$run-work" -q || fail "long module names broke --emit-dir $kind"
  done
done
python3 - "$W" "$long_name" <<'PYCODE' || fail "long filename hashing is incorrect"
import hashlib, pathlib, sys
root, long_name = pathlib.Path(sys.argv[1]), sys.argv[2]
# stem = first 135 bytes + '_' + the 64-hex SHA-256 of the WHOLE name = 200
# bytes, and the two same-prefix modules must still land in distinct files.
stems = {s: long_name + s for s in ('a', 'b')}
for suffix, name in stems.items():
    stem = name[:135] + '_' + hashlib.sha256(name.encode()).hexdigest()
    assert len(stem) == 200, len(stem)
    stems[suffix] = stem
assert stems['a'] != stems['b']
for kind, ext in (('verilog', '.v'), ('pyrope', '.prp'), ('lnast-dump', '.lnast')):
    first, second = root / f'long-{kind}-first', root / f'long-{kind}-second'
    # Deterministic: an independent compilation produces the same names.
    assert sorted(p.name for p in first.iterdir()) == sorted(p.name for p in second.iterdir()), kind
    for stem in stems.values():
        assert (first / (stem + ext)).is_file(), (kind, stem)
    for path in first.iterdir():
        assert len(path.name.encode()) <= 255, path
    # The manifest must name the file that actually landed on disk.
    manifest = (first / 'manifest.json').read_text()
    for stem in stems.values():
        assert '"file":"' + stem + ext + '"' in manifest, (kind, stem)
# The `.v` sidecar rides the SAME stem, so it stays beside its module.
assert (root / 'long-verilog-first' / (next(iter(stems.values())) + '.v.map')).is_file()
# `sim:` names three files per module off the same stem, and lhd re-derives
# those names to build the BUILD srcs list -- the emit fails outright if the
# two mappings ever drift apart.
for stem in stems.values():
    for ext in ('.hpp', '.cpp', '.iface.json'):
        assert (root / 'long-sim-first' / (stem + ext)).is_file(), stem + ext
PYCODE
grep -q "module ${long_name}a" "$W/long.v" || fail "first long module identity lost"
grep -q "module ${long_name}b" "$W/long.v" || fail "second long module identity lost"
"$LHD" lec --impl "$W/long.v" --ref "$W/long.sv" --top top --set formal.solver=lgyosys --workdir "$W/long-lec" -q \
  || fail "long module name emission changed behavior"

# Integrated frontend errors must retain their source diagnostic; upstream's
# captureOutput API is adapted to LiveHD's Slang version without buffering
# diagnostics until stack unwinding (Yosys errors may terminate the command).
cat > "$W/bad.sv" <<'EOF'
module bad(output logic y);
assign y = missing_function();
endmodule
EOF
if "$LHD" compile "$W/bad.sv" --reader yosys --workdir "$W/bad-work" > "$W/bad.log" 2>&1; then
  fail "undeclared function unexpectedly compiled"
fi
grep -q "undeclared identifier 'missing_function'" "$W/bad.log" \
  || fail "integrated Slang lost its source diagnostic"

# Async-reset latches imported with read_slang must reparse as strict SV and
# retain reset values. Check each reset polarity with its own reset sequence.
#
# NOT the shared inou/yosys/tests/latch_async.v fixture: this leg compares a
# yosys-read implementation against a SLANG-read reference, and that fixture's
# 3-bit `qn` latch is split into three independent 1-bit latches by the yosys
# path but kept whole by slang, so the two disagree from unconstrained latch
# state before any input is applied. latch_async.v is covered like-for-like by
# yosys_compile-latch_async and slang_compile-latch_async instead.
for polarity in pos neg; do
  if [ "$polarity" = pos ]; then
    reset_port=rst
    reset_cond=rst
  else
    reset_port=reset_n
    reset_cond='!reset_n'
  fi
  cat > "$W/latch-$polarity.sv" <<EOF
module reader_latch(input c, $reset_port, d, output logic q, output logic [2:0] qn);
  always_latch if ($reset_cond) q <= 0; else if (c) q <= d;
  always_latch if ($reset_cond) qn <= 3'b101; else if (!c) qn <= {d, ~d, d};
endmodule
EOF
  "$LHD" compile "$W/latch-$polarity.sv" --reader yosys --top reader_latch \
    --emit verilog:"$W/latch-$polarity.v" --workdir "$W/latch-$polarity-work" -q || fail "Yosys latch import failed"
  "$LHD" lec --impl "$W/latch-$polarity.v" --ref "$W/latch-$polarity.sv" --top reader_latch \
    --set formal.solver=cvc5 --workdir "$W/latch-$polarity-lec" --result-json "$W/latch-$polarity-lec.json" -q \
    || fail "Yosys latch roundtrip failed ($polarity)"
  grep -q '"verdict":"proven"' "$W/latch-$polarity-lec.json" || fail "latch roundtrip was not proven ($polarity)"
done

echo "PASS: long module names, read_slang diagnostics, and async-reset latches"
