#!/usr/bin/env python3
"""Derive the B11 acceptance gate's VERIFIER CONTROLS from a real run log.

These are SYNTHETIC LOG MUTATIONS.  They are controls on the VERIFIER -- each
plants one defect a passing run must not have, so the gate is shown to reject
it.  They are NOT execution evidence and no claim about B11 rests on them.

Regenerating them from the current real trace (rather than keeping hand-edited
copies) matters: a control written against an older trace format fails the gate
for the WRONG REASON -- a missing diagnostic line -- and silently stops testing
the defect it was named for.  The runner pins each control's failure MESSAGE
for exactly that reason.
"""
import re, sys, os

src, outdir = sys.argv[1], sys.argv[2]
lines = open(src).read().splitlines()
os.makedirs(outdir, exist_ok=True)

SUPPLIED = [(1 << 60) + i for i in range(8)]
EXP_ADDR, EXP_WORD, DW = 1, 0x223600abcde, 69
DFLT = (1 << 68) + 0x3FF

def img(words, dflt=DFLT):
    return "[" + ",".join(f"{v}:{DW}" for v in words) + f"] dflt {dflt}:{DW}"

SUP, COMMITTED = list(SUPPLIED), list(SUPPLIED)
COMMITTED[EXP_ADDR] = EXP_WORD

def emit(name, fn):
    out = []
    for ln in lines:
        r = fn(ln)
        if r is not None:
            out.append(r)
    p = os.path.join(outdir, name)
    open(p, "w").write("\n".join(out) + "\n")
    print(f"wrote {p}")

def step_of(ln):
    m = re.match(r"\s*TRACE step (\d+) mem 0 next ", ln)
    return int(m.group(1)) if m else None

def set_next(ln, words, dflt=DFLT):
    k = step_of(ln)
    return None if k is None else f"  TRACE step {k} mem 0 next {img(words, dflt)}"

# c1 -- a TRUNCATED trace: the quiet/clear tail is simply absent.
def c1(ln):
    m = re.match(r"\s*TRACE step (\d+) ", ln)
    if m and int(m.group(1)) >= 14:
        return None
    return ln

# c2 -- the driver's SIX-word preview of an EIGHT-word memory.
def c2(ln):
    if "TRACE loaded memory 0 " in ln:
        return "  TRACE loaded memory 0 " + img(SUP[:6])
    k = step_of(ln)
    if k is not None:
        return set_next(ln, (COMMITTED if k >= 15 else SUP)[:6])
    if "TRACE final memory 0 " in ln:
        return "  TRACE final memory 0 " + img(COMMITTED[:6])
    return ln

# c3 -- the right address at the WRONG WORD (one bit off).
WRONG = list(SUPPLIED); WRONG[EXP_ADDR] = EXP_WORD ^ 1
def c3(ln):
    k = step_of(ln)
    if k is not None:
        return set_next(ln, WRONG if k >= 15 else SUP)
    if "TRACE final memory 0 " in ln:
        return "  TRACE final memory 0 " + img(WRONG)
    return ln

# c3b -- NO transition ever occurs.
def c3b(ln):
    k = step_of(ln)
    if k is not None:
        return set_next(ln, SUP)
    if "TRACE final memory 0 " in ln:
        return "  TRACE final memory 0 " + img(SUP)
    return ln

# c4 -- a TRANSIENT write on a QUIET step (step 14), restored at 15 and so
#       invisible to any initial-vs-final comparison.
T4 = list(SUPPLIED); T4[3] = 12345
def c4(ln):
    k = step_of(ln)
    if k == 14:
        return set_next(ln, T4)
    if k is not None:
        return set_next(ln, COMMITTED if k >= 15 else SUP)
    return ln

# c5 -- a TRANSIENT DEFAULT change, likewise restored before the end.
def c5(ln):
    k = step_of(ln)
    if k == 7:
        return set_next(ln, SUP, DFLT ^ 0xFF)
    if k is not None:
        return set_next(ln, COMMITTED if k >= 15 else SUP)
    return ln

# c6 -- THE SUPERVISOR'S FIRST COUNTEREXAMPLE.  The sole image change is moved
#       to step 13.  The clock FIRES there, so a gate that classifies steps by
#       `fires e md.clock` alone accepts it: same final image, one change.  But
#       step 13 has pred 0 (phase 1) and effEn 0 -- it cannot commit.
def c6(ln):
    k = step_of(ln)
    if k is not None:
        return set_next(ln, COMMITTED if k >= 13 else SUP)
    return ln

# c7 -- THE SECOND COUNTEREXAMPLE.  The sole change is moved to step 0, which
#       also fires but has origEn 0 (the design's own enable is low).
def c7(ln):
    k = step_of(ln)
    if k is not None:
        return set_next(ln, COMMITTED)
    return ln

# c8 -- the driver could not resolve the slots, so the per-step values cannot
#       be attributed to the write gate at all.
def c8(ln):
    if "TRACE slots " in ln:
        return "  TRACE slots write UNRESOLVED"
    k = step_of(ln)
    if k is not None:
        return set_next(ln, COMMITTED if k >= 15 else SUP)
    return ln

# c9 -- the diagnostics are read off a DIFFERENT node than the audited gate.
def c9(ln):
    if "TRACE slots " in ln:
        return ln.replace("pred 334", "pred 999")
    return ln

for name, fn in (("c8_slots_unresolved.log", c8), ("c9_slots_not_audited.log", c9),
                 ("c1_missing_steps.log", c1), ("c2_truncated_image.log", c2),
                 ("c3_wrong_word.log", c3), ("c3b_no_transition.log", c3b),
                 ("c4_transient_quiet_write.log", c4), ("c5_transient_default.log", c5),
                 ("c6_change_at_disabled_step13.log", c6),
                 ("c7_change_at_disabled_step0.log", c7)):
    emit(name, fn)
