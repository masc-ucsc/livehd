#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A NON-FORWARDING or REGISTERED memory read feeding its own memory's write
# port is not a combinational loop.  The qualifier is load-bearing: a read that
# IS forwarded from that write port genuinely does depend on this cycle's write
# data, and such a cycle must still be refused (pass/lean/cert_dag_order_test.cpp).
#
# pass.lean's dependency walk used to model a Memory CELL as one atomic node, so
# read-output -> logic -> write-input came back as
#   "COMBINATIONAL CYCLE: node n_56 (get_mask_56) reads n_44 (memory_44:mem)"
# and four CORE-ET blocks were gated on it, minion_frontend_thread_buffer among
# them (pass/lean/CYCLE_PROVENANCE.txt part 3).  The real dependency is per
# PORT: a read sees only the write ports FORWARDED to it, and a SYNCHRONOUS
# read's output is a register, which depends on nothing in the current cycle.
#
# These three fixtures are that shape minimized, and the assertions are
# STRUCTURAL rather than "it emitted something": the certificate must show the
# read based on the committed array image and not on a write chain.  An emit-only
# check would also pass for a model that silently forwarded every write.
set -u

LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# ALWAYS A CHILD PATH, never $TEST_TMPDIR itself (that is bazel's sandbox temp
# root; see lhd/tests/census_consistency_test.sh).
if [ -n "${TEST_TMPDIR:-}" ]; then T="$TEST_TMPDIR/mem_read_write_feedback_runtime"
else T="$ROOT/generated/mem_read_write_feedback_test/runtime_tmp"; fi
rm -rf "$T"; mkdir -p "$T"

rc=0
fail() { echo "FAIL: $*"; rc=1; }

# emit <top> -> leaves $T/<top>/{pp-mem.il,cert.lean}; echoes nothing
emit() {
  local top="$1"
  local d="$T/$top"
  mkdir -p "$d"
  ( cd "$d" && LIVEHD_MEM_TIMING_DEBUG=1 "$LHD" compile verilog "$HERE/$top.v" --top "$top" \
      --reader yosys-slang --workdir w --emit-dir lg:lg ) > "$d/compile.log" 2>&1 \
    || { fail "$top: did not import"; tail -3 "$d/compile.log"; return 1; }
  "$LHD" pass single_edge --top "$top" "lg:$d/lg" --emit-dir "lg:$d/lgn" \
    --set multi_clock=true --workdir "$d/wse" > "$d/se.log" 2>&1 \
    || { fail "$top: pass.single_edge refused it"; tail -3 "$d/se.log"; return 1; }
  "$LHD" compile "lg:$d/lgn" --top "$top" --recipe O0 --workdir "$d/wl" \
    --emit-dir "lean:$d/lean" --set formal.lean.mode=verified_compiler \
    --set formal.lean.strict=true > "$d/lean.log" 2>&1
  if [ $? -ne 0 ]; then
    fail "$top: pass.lean refused a design with NO combinational loop"
    grep -oP '"message":"\K[^"]{0,200}' "$d/lean.log" | head -1 | sed 's/^/      /'
    return 1
  fi
  [ -s "$d/lean/${top}_Lgraph.lean" ] || { fail "$top: no certificate written"; return 1; }
  cp "$d/lean/${top}_Lgraph.lean" "$d/cert.lean"
  return 0
}

# ---------------------------------------------------------------------------
# 1. ASYNCHRONOUS read -> logic -> write data AND write enable.
# ---------------------------------------------------------------------------
if emit mem_async_read_feedback; then
  echo "ok: mem_async_read_feedback emits a certificate"
  d="$T/mem_async_read_feedback"
  # The fixture must still BE the shape it claims: an async, non-forwarding read.
  # Without this the structural check below could pass for a different design.
  if grep -q "RD_CLK_ENABLE 1'0" "$d/pp-mem.il" && grep -q "RD_TRANSPARENCY_MASK 1'0" "$d/pp-mem.il"; then
    echo "ok: yosys reports one ASYNC, NON-forwarding read port"
  else
    fail "the async fixture no longer imports as async/non-forwarding"
    grep -oP "RD_CLK_ENABLE.{0,10}|RD_TRANSPARENCY_MASK.{0,10}" "$d/pp-mem.il" | sed 's/^/      /'
  fi
  CERT="$d/cert.lean" python3 - <<'PYCHK' || rc=1
import os, re, sys
cert = open(os.environ["CERT"]).read()
src = re.search(r"sources\s*:=\s*#\[(.*?)\n\s*\]", cert, re.S).group(1)
entries = [l.strip().lstrip(", ").strip() for l in src.strip().splitlines()]
img = [i for i, e in enumerate(entries) if e.startswith("SourceDesc.memImg")]
if len(img) != 1:
    print(f"FAIL: expected exactly one memImg source, found {len(img)}"); sys.exit(1)
reads = re.findall(r"op := LGraphOp\.Op_MemRead, width := \d+, deps := #\[([0-9, ]+)\]", cert)
if len(reads) != 1:
    print(f"FAIL: expected exactly one Op_MemRead, found {len(reads)}"); sys.exit(1)
base = int(reads[0].split(",")[0])
# THE assertion. An Op_MemRead's first dep is the image it reads. For a port
# with no forwarding that must be the COMMITTED array source -- if it were an
# Op_MemWrite node the read would depend on this cycle's write data, which is
# exactly the dependency that makes the feedback a real cycle.
if base != img[0]:
    print(f"FAIL: the read is based on slot {base}, not on the committed array "
          f"image (slot {img[0]}). A non-forwarded read must not depend on a write chain.")
    sys.exit(1)
print(f"ok: the async read is based on the committed array image (slot {base})")
writes = re.findall(r"op := LGraphOp\.Op_MemWrite(?:BE \d+)?, width := \d+, deps := #\[([0-9, ]+)\]", cert)
if len(writes) != 1:
    print(f"FAIL: expected exactly one write chain step, found {len(writes)}"); sys.exit(1)
print("ok: the write chain is a separate certificate node, as it must be")
PYCHK
fi

# ---------------------------------------------------------------------------
# 2. SYNCHRONOUS read -> logic -> write.  The read output is a REGISTER, so it
#    cannot be on a combinational path at all.
# ---------------------------------------------------------------------------
if emit mem_sync_read_feedback; then
  echo "ok: mem_sync_read_feedback emits a certificate"
  d="$T/mem_sync_read_feedback"
  if grep -q "RD_CLK_ENABLE 1'1" "$d/pp-mem.il"; then
    echo "ok: yosys reports one SYNCHRONOUS read port"
  else
    fail "the sync fixture no longer imports as synchronous"
  fi
  CERT="$d/cert.lean" python3 - <<'PYCHK' || rc=1
import os, re, sys
cert = open(os.environ["CERT"]).read()
src = re.search(r"sources\s*:=\s*#\[(.*?)\n\s*\]", cert, re.S).group(1)
entries = [l.strip().lstrip(", ").strip() for l in src.strip().splitlines()]
flops = [i for i, e in enumerate(entries) if e.startswith("SourceDesc.flopQ")]
mux = re.findall(r"op := LGraphOp\.Op_MuxBool, width := \d+, deps := #\[([0-9, ]+)\]", cert)
if not mux:
    print("FAIL: no Op_MuxBool -- the sync read register's next value is missing"); sys.exit(1)
deps = [int(x) for x in mux[0].split(",")]
# sram_sync_read_reg_next ren raw cur: deps are (enable, HELD value, raw read),
# and the held value is the read-data register SOURCE. A consumer resolving to
# that source is what makes a synchronous read unable to close a loop.
if deps[1] not in flops:
    print(f"FAIL: the sync read register's held value is slot {deps[1]}, which is not "
          f"a flopQ source (flopQ slots: {flops})"); sys.exit(1)
print(f"ok: the sync read register holds a flopQ source (slot {deps[1]})")

# ...and WHAT CONSUMERS SEE must be that register, never the raw read.  Without
# this the whole claim is untested: the mux above is identical whether a reader
# of the port resolves to the register SOURCE or to the combinational
# Op_MemRead, and resolving to the latter both returns the wrong value and puts
# a synchronous read back on the combinational path.
nodes = re.findall(r"\{ op := (LGraphOp\.\S+(?: \d+)?), width := \d+, deps := #\[([0-9, ]*)\]", cert)
nsrc = len(entries)
raw = [nsrc + i for i, (op, d) in enumerate(nodes) if op == "LGraphOp.Op_MemRead"]
muxb = [nsrc + i for i, (op, d) in enumerate(nodes) if op == "LGraphOp.Op_MuxBool"]
if len(raw) != 1:
    print(f"FAIL: expected exactly one Op_MemRead, found {len(raw)}"); sys.exit(1)
users = [nsrc + i for i, (op, d) in enumerate(nodes)
         if raw[0] in [int(x) for x in d.split(",") if x.strip()]]
if users != muxb:
    print(f"FAIL: the UNGATED read (slot {raw[0]}) is referenced by {users}, not only by "
          f"the read register's next value {muxb}. A consumer of a synchronous read port "
          f"must resolve to the register source, not to the combinational read.")
    sys.exit(1)
for sec in ("outputs", "flops", "memories"):
    m2 = re.search(rf"{sec}\s*:=\s*#\[(.*?)\n\s*\]", cert, re.S)
    if m2 and str(raw[0]) in re.findall(r"\b\d+\b", m2.group(1)):
        print(f"FAIL: `{sec}` references the ungated read slot {raw[0]}"); sys.exit(1)
print(f"ok: the ungated read (slot {raw[0]}) is used only by the register's next value")
PYCHK
fi

# ---------------------------------------------------------------------------
# 3. TWO memories, one's read output being the other's read address.
# ---------------------------------------------------------------------------
if emit mem_two_memories_chained; then
  echo "ok: mem_two_memories_chained emits a certificate"
  CERT="$T/mem_two_memories_chained/cert.lean" python3 - <<'PYCHK' || rc=1
import os, re, sys
cert = open(os.environ["CERT"]).read()
src = re.search(r"sources\s*:=\s*#\[(.*?)\n\s*\]", cert, re.S).group(1)
entries = [l.strip().lstrip(", ").strip() for l in src.strip().splitlines()]
img = [i for i, e in enumerate(entries) if e.startswith("SourceDesc.memImg")]
if len(img) != 2:
    print(f"FAIL: expected two memImg sources (two memories), found {len(img)}"); sys.exit(1)
nodes = re.findall(r"\{ op := (LGraphOp\.\S+(?: \d+)?), width := (\d+), deps := #\[([0-9, ]*)\]", cert)
reads = [(i, d) for i, (op, w, d) in enumerate(nodes) if op == "LGraphOp.Op_MemRead"]
if len(reads) != 2:
    print(f"FAIL: expected two Op_MemRead nodes, found {len(reads)}"); sys.exit(1)
nsrc = len(entries)
# Slot numbering: sources occupy 0..nsrc-1 and node i occupies nsrc+i.
deps_of = {nsrc + i: [int(x) for x in d.split(",") if x.strip()] for i, (op, w, d) in enumerate(nodes)}
def reaches(start, target, seen=None):
    seen = seen if seen is not None else set()
    if start in seen:
        return False
    seen.add(start)
    for d in deps_of.get(start, []):
        if d == target or reaches(d, target, seen):
            return True
    return False
a, b = nsrc + reads[0][0], nsrc + reads[1][0]
# ONE of the two reads must depend on the other: that is the cross-memory
# dependency this fixture exists for, and the one that cannot be built unless
# every memory's read ids are allocated before any memory's ports are filled.
if not (reaches(a, b) or reaches(b, a)):
    print(f"FAIL: neither Op_MemRead depends on the other (slots {a}, {b}); the "
          f"fixture no longer chains one memory's read into the other's address")
    sys.exit(1)
print("ok: one memory's read address depends on the other memory's read output")
PYCHK
fi

# ---------------------------------------------------------------------------
# 4. A FORWARDED read: the other side of the same decision.
#
# Fixtures 1-3 all have RD_TRANSPARENCY_MASK 0, so they would still pass if
# `memory_fwd_bit` stopped selecting write ports altogether -- the guard would
# be half-tested.  This design forwards, through Pyrope program order (no
# program-ordered RTL can forward a write into the read that FEEDS that write,
# which is why the cycle case is a unit test over the certificate DAG).
# ---------------------------------------------------------------------------
fd="$T/mem_fwd_read_feedback"; mkdir -p "$fd"
if "$LHD" compile pyrope "$HERE/mem_fwd_read_feedback.prp" --top mem_fwd_read_feedback \
     --workdir "$fd/w" --emit-dir "lg:$fd/lg" > "$fd/compile.log" 2>&1 \
   && "$LHD" compile "lg:$fd/lg" --top mem_fwd_read_feedback --recipe O0 --workdir "$fd/wl" \
     --emit-dir "lean:$fd/lean" --set formal.lean.mode=verified_compiler \
     --set formal.lean.strict=true > "$fd/lean.log" 2>&1; then
  echo "ok: mem_fwd_read_feedback emits a certificate"
  CERT="$fd/lean/mem_fwd_read_feedback_Lgraph.lean" python3 - <<'PYCHK' || rc=1
import os, re, sys
cert = open(os.environ["CERT"]).read()
src = re.search(r"sources\s*:=\s*#\[(.*?)\n\s*\]", cert, re.S).group(1)
entries = [l.strip().lstrip(", ").strip() for l in src.strip().splitlines()]
nsrc = len(entries)
img = [i for i, e in enumerate(entries) if e.startswith("SourceDesc.memImg")]
nodes = re.findall(r"\{ op := (LGraphOp\.\S+(?: \d+)?), width := \d+, deps := #\[([0-9, ]*)\]", cert)
writes = {nsrc + i for i, (op, d) in enumerate(nodes) if op.startswith("LGraphOp.Op_MemWrite")}
reads = [(nsrc + i, [int(x) for x in d.split(",") if x.strip()])
         for i, (op, d) in enumerate(nodes) if op == "LGraphOp.Op_MemRead"]
if not reads:
    print("FAIL: no Op_MemRead in a design whose whole point is a forwarded read"); sys.exit(1)
base = reads[0][1][0]
if base in img:
    print(f"FAIL: the forwarded read is based on the COMMITTED array (slot {base}); "
          f"memory_fwd_bit selected no write port, so the model returns stale data")
    sys.exit(1)
if base not in writes:
    print(f"FAIL: the read's base slot {base} is neither the committed image nor a "
          f"write-chain step"); sys.exit(1)
print(f"ok: the forwarded read is based on a write-chain step (slot {base}), not the committed array")
PYCHK
else
  fail "mem_fwd_read_feedback: compile or pass.lean refused it"
  tail -3 "$fd/lean.log" 2>/dev/null | sed 's/^/      /'
fi

[ "$rc" -eq 0 ] || { echo "FAIL: mem_read_write_feedback_test"; exit 1; }
echo "PASS: mem_read_write_feedback_test"
