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
# CLOCK-FIRES IS NOT ENABLED.  An earlier version classified a step only by
# `fires e md.clock`, so a planted image change on a step where the write was
# DISABLED still passed.  Three independent reasons a fired clock commits
# nothing, all visible in the trace of this very run:
#
#   step 13  pred 0      -- phase is 1, so EQ(0,phase) is false and the slot
#                          mask zeroes the enable (origEn is the full 2^69-1)
#   step 12  origEn 0    -- the design's own enable is low
#   step 14  quiet       -- fully enabled (pred 1, effEn 2^69-1, datum ready)
#                          but `edgeSchedule` suppresses k%3==2, so no edge
#
# So a step may commit only when the clock FIRES and the slot predicate is TRUE
# and the effective write enable is NONZERO, and those come from the recorded
# pre-step values -- evaluated by the driver from the real pre-state and input
# through DesignSemantics' own `evalGraphG`/`srcEnv`, with no overrides.
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
# the recorded PRE-STEP values, from the design's own semantics
pre = {}
for k, ph, pr, oe, ee, ad, dt in re.findall(
        r"TRACE step (\d+) pre phase (-?\d+) pred (-?\d+) origEn (-?\d+) effEn (-?\d+) addr (-?\d+) datum (-?\d+)", txt):
    pre[int(k)] = dict(phase=int(ph), pred=int(pr), origEn=int(oe), effEn=int(ee), addr=int(ad), datum=int(dt))
# The pre-step values are only meaningful if they were read off THE AUDITED
# WRITE GATE.  The driver discovers the slots from the certificate itself; these
# are the ids the independent audit recorded for this certificate (pinned by
# sha256 in SHA256SUMS), so a mismatch means the diagnostics describe some other
# node and every value below is about the wrong thing.  Re-derive both if the
# certificate is regenerated -- do not relax this.
AUDITED = dict(write=374, addr=371, datum=372, effEn=373, origEn=356,
               mask=337, pred=334, phaseSrc=29)
sl = re.search(r"TRACE slots (.+)", txt)
if not sl:
    fail("the trace does not record the discovered write-gate slots")
if "UNRESOLVED" in sl.group(1):
    fail(f"the driver could not resolve the write-gate slots: {sl.group(1).strip()} "
         f"-- the per-step values cannot be attributed to the write gate")
got = {k: int(v) for k, v in re.findall(r"(\w+) (\d+)", sl.group(1))}
if got != AUDITED:
    fail(f"the discovered write-gate slots {got} are not the audited {AUDITED} "
         f"-- the per-step diagnostics describe a different node")
print(f"ok: the per-step values are read off the AUDITED write gate {AUDITED}")
nexts = {}
for k, img in re.findall(r"TRACE step (\d+) mem 0 next (.+)", txt):
    nexts[int(k)] = parse_img(img)
pol = {int(k): v for k, v in re.findall(r"TRACE step (\d+) policy (\w+)", txt)}
missing = [k for k in range(STEPS) if k not in steps or k not in nexts or nexts[k] is None or k not in pre]
if missing:
    fail(f"the trace is missing or malformed for step(s) {missing} -- a truncated trace cannot be accepted")
badpol = [k for k in range(STEPS) if pol.get(k) != "true"]
if badpol:
    fail(f"the returned-state policy did not hold on step(s) {badpol}")
print(f"ok: a complete {STEPS}-step trace, policy checked on every step")

# ---- the ONLY permitted change, over the WHOLE image ---------------------
prev_w, prev_d = lwords, ldflt
changes, enabled = [], []
for k in range(STEPS):
    ws, dd = nexts[k]
    if len(ws) != len(SUPPLIED):
        fail(f"step {k}'s image has {len(ws)} words; a truncated image cannot be accepted")
    diffs = [i for i in range(len(ws)) if ws[i] != prev_w[i]]
    if dd != prev_d:
        fail(f"step {k} ({steps[k]}) CHANGED THE DEFAULT {prev_d} -> {dd}")
    p = pre[k]
    # A COMMIT IS POSSIBLE ONLY IF all three hold.  Checked against the recorded
    # values, not against the active/quiet label alone.
    can = (steps[k] == "active") and p["pred"] != 0 and p["effEn"] != 0
    if diffs and not can:
        why = []
        if steps[k] != "active": why.append("the clock is QUIET")
        if p["pred"] == 0:       why.append(f"the slot predicate is FALSE (phase {p['phase']})")
        if p["effEn"] == 0:      why.append(f"the write enable is ZERO (origEn {p['origEn']})")
        fail(f"step {k} changed address(es) {diffs} but could not commit: " + ", ".join(why))
    if can:
        enabled.append(k)
    if diffs:
        changes.append((k, diffs, [ws[i][0] for i in diffs]))
    prev_w, prev_d = ws, dd

if not enabled:
    fail("NO step was an enabled commit (clock fires AND slot predicate true AND write enable nonzero)")
print(f"ok: enabled-commit step(s) {enabled}; every other step could not commit, "
      f"and none of them changed the image, the default or any width")
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
if k not in enabled:
    fail(f"step {k} changed the image but is not an enabled commit")
pk = pre[k]
print(f"ok: exactly one commit, at step {k} (phase {pk['phase']}, pred {pk['pred']}, "
      f"effEn {pk['effEn']}): address {EXP_ADDR}  {before} -> {EXP_WORD} (0x223600abcde)")
if pk["effEn"] != (1 << DW) - 1:
    fail(f"the committing step's effective enable is {pk['effEn']}, not the full {DW}-lane mask")
print(f"ok: the committing step's effective enable is the FULL {DW}-lane mask")
if pk["addr"] != EXP_ADDR or pk["datum"] != EXP_WORD:
    fail(f"the committing step's address/datum are {pk['addr']}/{pk['datum']}, "
         f"not the source-derived {EXP_ADDR}/{EXP_WORD}")
print("ok: the committing step's recorded address and datum are the source-derived pair")
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
