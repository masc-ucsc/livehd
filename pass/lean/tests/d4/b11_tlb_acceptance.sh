#!/bin/bash
# B11 (minion_tlb) INITIALIZED-TRANSITION ACCEPTANCE GATE, over a COMPLETE
# per-step trace.
#
# WHY THE TRACE.  The first version of this gate compared only the INITIAL and
# FINAL images.  That cannot establish quiet/disabled preservation: a write on
# a quiet step that is overwritten before the end is invisible to it.  It also
# excluded the written address itself and read only the driver's SIX-word
# preview of an EIGHT-word memory, so addresses 6 and 7 were never compared at
# all.  Synthetic log controls confirmed it accepted a 12-step log missing the
# clear/quiet tail and even a two-word preview.
#
# So the driver is run with `trace`, which records the loaded image with every
# width and the default, each step's edges/inputs and its active/quiet
# classification (from `fires e md.clock`, i.e. actual state, NOT the final
# image), each DECODED next image, and the returned-state policy per step.
#
# THE EXPECTATION IS SOURCE-DERIVED: address 1, word 0x223600abcde, packed from
# the pinned CORE-ET DV fill_entry sequence for NrMinions=1, Entries=8.
set -u
LOG="${1:?usage: b11_tlb_acceptance.sh <run.log> [--expect-no-transition]}"
MODE="${2:-}"
exec python3 - "$LOG" "$MODE" <<'PY'
import re, sys
log, mode = sys.argv[1], sys.argv[2]
EXP_ADDR, EXP_WORD, STEPS, DW = 1, 0x223600abcde, 18, 69
SUPPLIED = [(1 << 60) + i for i in range(8)]
SUPPLIED_DFLT = (1 << 68) + 0x3FF

def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)

try:
    txt = open(log).read()
except OSError as e:
    fail(f"cannot read {log}: {e}")
if not txt.strip():
    fail(f"run log {log} is empty")

# ---- the NEGATIVE mode reuses the retained v4 transition control ----------
if mode == "--expect-no-transition":
    m = re.search(r"final memory 0 words \[([^\]]*)\]", txt)
    if not m:
        fail("the negative log reports no final image")
    w = m.group(1).split(',')
    if len(w) <= EXP_ADDR:
        fail("the negative log's image preview is too short to inspect the written address")
    if int(w[EXP_ADDR]) == EXP_WORD:
        fail(f"NEGATIVE CONTROL FAILED: the pre-repair certificate produced {EXP_WORD}")
    print(f"ok: negative control -- address {EXP_ADDR} holds {w[EXP_ADDR]}, not {EXP_WORD}")
    print("PASS: b11_tlb_acceptance (negative)")
    sys.exit(0)

if "PRODUCER_EXIT=0" not in txt:
    got = re.findall(r"PRODUCER_EXIT=(\d+)", txt)
    fail(f"the producer did not exit 0 (got {got or 'nothing'})")

# ---- route / production shape --------------------------------------------
for pat, why in (
    (r"memory route: .*ADMITTED true", "the memory route did not admit the design"),
    (r"residual \d+ terms, 1 function\(s\), arity \[3\], checker bound \d+", "no arity-3 residual at a checked bound"),
    (r"executing \d+ cycle\(s\) of the residual at the checked bound", "execution was not at the checked bound"),
    (r"reference comparison: COMPLETE", "the reference comparison was not COMPLETE"),
    (r"state survives encStateM/decStateM unchanged", "the supplied state did not survive the codec"),
    (r"final returned-state policy: stateFullOKB true", "the returned state failed the policy"),
    (r"state provenance: supplied file", "the initial state was not SUPPLIED from a file"),
):
    if not re.search(pat, txt):
        fail(why)
tags = re.search(r"step outcomes: ([a-z,\-]+)", txt)
if not tags:
    fail("no step outcomes")
outcomes = tags.group(1).split(',')
if len(outcomes) != STEPS:
    fail(f"expected {STEPS} steps, the log reports {len(outcomes)}")
if any(t != "ok" for t in outcomes):
    fail(f"not every step returned ok: {outcomes}")

# ---- the trace must be present and COMPLETE ------------------------------
def parse_img(s):
    m = re.match(r"\[([^\]]*)\] dflt (\d+):(\d+)$", s.strip())
    if not m:
        return None
    body = m.group(1)
    ws = [] if not body else [tuple(int(x) for x in p.split(':')) for p in body.split(',')]
    return ws, (int(m.group(2)), int(m.group(3)))

loaded = re.search(r"TRACE loaded memory 0 (.+)", txt)
if not loaded:
    fail("the log has NO trace: rerun the driver with the `trace` token")
li = parse_img(loaded.group(1))
if li is None:
    fail("the loaded image line is malformed")
lwords, ldflt = li
if len(lwords) != len(SUPPLIED):
    fail(f"the loaded image has {len(lwords)} words; the supplied file declares {len(SUPPLIED)} "
         f"(a TRUNCATED image cannot support a preservation claim)")
for i, (v, w) in enumerate(lwords):
    if v != SUPPLIED[i] or w != DW:
        fail(f"loaded address {i} is {v}:{w}; the supplied file declares {SUPPLIED[i]}:{DW} "
             f"-- loading or the codec changed it")
if ldflt != (SUPPLIED_DFLT, DW):
    fail(f"the loaded default is {ldflt[0]}:{ldflt[1]}; the supplied file declares {SUPPLIED_DFLT}:{DW}")
print(f"ok: all {len(SUPPLIED)} supplied words and the default loaded unchanged, each at width {DW}")

steps = {}
for k, act in re.findall(r"TRACE step (\d+) mem 0 (active|quiet) ", txt):
    steps[int(k)] = act
nexts = {}
for k, img in re.findall(r"TRACE step (\d+) mem 0 next (.+)", txt):
    nexts[int(k)] = parse_img(img)
pol = {int(k): v for k, v in re.findall(r"TRACE step (\d+) policy (\w+)", txt)}
missing = [k for k in range(STEPS) if k not in steps or k not in nexts or nexts[k] is None]
if missing:
    fail(f"the trace is missing or malformed for step(s) {missing} -- a truncated trace cannot be accepted")
badpol = [k for k in range(STEPS) if pol.get(k) != "true"]
if badpol:
    fail(f"the returned-state policy did not hold on step(s) {badpol}")
print(f"ok: a complete {STEPS}-step trace, policy checked on every step")

# ---- the ONLY permitted change, over the WHOLE image ---------------------
prev_w, prev_d = lwords, ldflt
changes = []
for k in range(STEPS):
    ws, dd = nexts[k]
    if len(ws) != len(SUPPLIED):
        fail(f"step {k}'s image has {len(ws)} words; a truncated image cannot be accepted")
    diffs = [i for i in range(len(ws)) if ws[i] != prev_w[i]]
    if dd != prev_d:
        fail(f"step {k} ({steps[k]}) CHANGED THE DEFAULT {prev_d} -> {dd}")
    if diffs:
        if steps[k] != "active":
            fail(f"step {k} is {steps[k]} but changed address(es) {diffs} "
                 f"-- quiet/disabled preservation is broken at the step, not merely at the end")
        changes.append((k, diffs, [ws[i][0] for i in diffs]))
    prev_w, prev_d = ws, dd

if not changes:
    fail("NO memory transition occurred on any step")
if len(changes) != 1:
    fail(f"the image changed on {len(changes)} steps {[c[0] for c in changes]}; exactly one commit was expected "
         f"(a transient write that is later restored would show up here and not in the final image)")
k, diffs, vals = changes[0]
if diffs != [EXP_ADDR]:
    fail(f"step {k} changed address(es) {diffs}; the source-derived expectation is address {EXP_ADDR}")
if vals[0] != EXP_WORD:
    fail(f"step {k} wrote {vals[0]} at address {EXP_ADDR}; the source-derived expectation is {EXP_WORD} (0x223600abcde)")
wrote_w = nexts[k][0][EXP_ADDR][1]
if wrote_w != DW:
    fail(f"step {k} wrote address {EXP_ADDR} at width {wrote_w}, not the declared {DW}")
before = SUPPLIED[EXP_ADDR]
if (before >> 32) == (EXP_WORD >> 32):
    fail("nothing above bit 31 changed; a low-lane-only gate could have produced this")
print(f"ok: exactly one commit, at step {k}: address {EXP_ADDR}  {before} -> {EXP_WORD} (0x223600abcde)")
print(f"ok: bits above 31 changed ({before >> 32} -> {EXP_WORD >> 32}): the high lanes were genuinely written")
print(f"ok: every other address AND the default are bit-for-bit preserved on ALL {STEPS} steps, "
      f"including address {EXP_ADDR} on every step but {k}")

# ---- the final image must still agree ------------------------------------
fin = re.search(r"TRACE final memory 0 (.+)", txt)
if not fin:
    fail("no final trace image")
fw, fd = parse_img(fin.group(1))
expect = list(SUPPLIED); expect[EXP_ADDR] = EXP_WORD
if [v for v, _ in fw] != expect or fd != (SUPPLIED_DFLT, DW):
    fail(f"the final image {[v for v, _ in fw]} dflt {fd} does not match the expected {expect} dflt {SUPPLIED_DFLT}:{DW}")
print("ok: the final image is exactly the supplied image with the one expected commit")
print("PASS: b11_tlb_acceptance")
PY
