#!/usr/bin/env bash
# Controls for MODULE-PROOF RESULT ADMISSION in d3_sweep.py.
#
# WHAT THIS GUARDS, every case reproduced against the real code path:
#
#   * a worker that printed a proved result and then exited nonzero;
#   * a STALE result.json from an earlier attempt;
#   * a preseeded result in a directory whose mode BLOCKS the removal, with an
#     exit-0 worker that creates nothing -- the old file is then the run's only
#     output and was credited;
#   * a result naming a DIFFERENT certificate;
#   * a worker that APPENDS to the certificate and submits the new digest --
#     the result matched, the row named the old input, and the proof was
#     credited over byte drift;
#   * a result.json that is a list, a scalar, or carries a non-string
#     cert_sha256 -- these RAISED out of the proof stage and erased the
#     cert..agree the executable stage had already earned;
#   * a driver timeout and a composition stopped at its memory cap, which must
#     be refused and NAMED rather than merged into not_proved.
#
# HOW, AND WHY NOT THROUGH A FULL CLI RUN.
#
# `run_one` reaches this decision through `clear_stale_result`,
# `read_module_result` and `admit_module_result`. Those are what this drives,
# in the LIVE module -- imported, never copied or written to -- against REAL
# filesystem state: actual files, an actual 0555 directory, an actual mutated
# certificate copy.
#
# Two alternatives were tried and rejected. Swapping the live
# `d3_module_proof.py` for a stub works but writes to a tracked worker, and a
# crash leaves the tree modified. Copying the sweep into a scratch context
# fails outright: it derives its Lean root from its own location, so a copy
# looks for `generated/formal/lean`. Adding a worker-path seam to production
# code for a test is what `d3_sweep.py`'s own `rollback_pair` note argues
# against -- "rather than add an injection seam to production code so an
# in-process test can do the same, the decision logic is a function and the
# test drives it directly". That is the shape used here.
#
# END-TO-END CLI EVIDENCE EXISTS SEPARATELY and is not duplicated here: the
# RAM fixture through the real module backend gives `module_proof=1
# module_admit=ok`, and `txfma_f4` gives `module_proof_status=rss_killed` with
# the proof refused and `verdict=agree` retained (PORT_LEDGER 29.2, 29.7).
#
# No Lean proof runs here; no leg says anything about a theorem.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# <ROOT>/pass/lean/Translation_Validation/scripts/<this file>: four levels down.
ROOT="$(cd "$HERE/../../../.." && pwd)"
SRC_CERT="${D3_CONTROL_CERT:-$ROOT/generated/master_port/d3_pilot/certs/tiny_sync_ram_Lgraph.lean}"

[[ -r "$HERE/d3_sweep.py" ]] || { echo "FATAL: missing d3_sweep.py" >&2; exit 2; }
[[ -s "$SRC_CERT" ]] || { echo "SKIP: no control certificate at $SRC_CERT" >&2; exit 3; }

W="$ROOT/generated/d3_module_admit_controls"
rm -rf "$W"; mkdir -p "$W"
SWEEP_SHA="$(sha256sum < "$HERE/d3_sweep.py")"
WORKER_SHA="$(sha256sum < "$HERE/d3_module_proof.py")"
CERT_SHA="$(sha256sum < "$SRC_CERT")"

python3 -I - "$HERE/d3_sweep.py" "$SRC_CERT" "$W" <<'PYEOF'
import hashlib, importlib.util, json, os, pathlib, shutil, sys
sweep_path, src_cert, workdir = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
spec = importlib.util.spec_from_file_location("sw", sweep_path)
sw = importlib.util.module_from_spec(spec); spec.loader.exec_module(sw)

fails = []
def chk(name, cond, note):
    print(("  ok   " if cond else "  FAIL ") + f"{name:<34} {note}")
    if not cond:
        fails.append(name)

def case(tag):
    """One isolated context: its own directory and its own copy of the cert."""
    d = workdir / tag
    if d.exists():
        shutil.rmtree(d)
    (d / "certs").mkdir(parents=True)
    c = d / "certs" / src_cert.name
    shutil.copy2(src_cert, c)
    return d, c, hashlib.sha256(c.read_bytes()).hexdigest()

def admit(res_path, *, rc=0, want=None, after=None, blocked=""):
    res, err = sw.read_module_result(res_path)
    return sw.admit_module_result(res, err, rc, want, after if after is not None else want, blocked)

def write(p, obj):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(obj if isinstance(obj, str) else json.dumps(obj))
    return p

# ---- positive: a fresh, correct, matching result IS credited ---------------
d, c, sha = case("positive")
r = write(d / "out" / "result.json", {"status": "proved", "cert_sha256": sha})
chk("positive", admit(r, want=sha) == [], "a fresh proved result from an exit-0 worker is credited")

# ---- the worker exited nonzero --------------------------------------------
d, c, sha = case("nonzero")
r = write(d / "out" / "result.json", {"status": "proved", "cert_sha256": sha})
chk("nonzero_after_proved", any("worker exit 17" in x for x in admit(r, rc=17, want=sha)),
    "a proved result behind exit 17 is refused")

# ---- freshness: a removable stale file is cleared --------------------------
d, c, sha = case("stale")
r = write(d / "out" / "result.json", {"status": "proved", "cert_sha256": sha})
why = sw.clear_stale_result(r)
chk("stale_cleared", why == "" and not r.exists(), "a removable stale result.json is cleared")
chk("stale_not_credited", any("no usable result" in x for x in admit(r, want=sha, blocked=why)),
    "and with it cleared there is nothing to credit")

# ---- freshness: the removal FAILS, so freshness is unknown -----------------
d, c, sha = case("blocked")
r = write(d / "out" / "result.json", {"status": "proved", "cert_sha256": sha})
os.chmod(r.parent, 0o555)
why = sw.clear_stale_result(r)
still = r.exists()
os.chmod(r.parent, 0o755)
chk("blocked_unlink_detected", bool(why) and still,
    f"a result.json that cannot be removed is reported ({why.split(':')[0]})")
chk("blocked_unlink_refused", any("could not clear" in x for x in admit(r, want=sha, blocked=why)),
    "and the preseeded result is refused on freshness, not credited")

# ---- the result names a different certificate ------------------------------
d, c, sha = case("wrongcert")
r = write(d / "out" / "result.json", {"status": "proved", "cert_sha256": "0" * 64})
chk("wrong_certificate", any("names certificate" in x for x in admit(r, want=sha)),
    "a result naming a different certificate is refused")

# ---- the worker rewrote its own input --------------------------------------
d, c, sha = case("mutate")
with c.open("a") as fh:
    fh.write("\n-- control: the worker changed its own input\n")
after = hashlib.sha256(c.read_bytes()).hexdigest()
r = write(d / "out" / "result.json", {"status": "proved", "cert_sha256": after})
chk("input_mutated", any("certificate changed" in x for x in admit(r, want=sha, after=after)),
    "a worker that appends to the certificate and submits the new digest is refused")

# ---- malformed shapes refuse the proof and RAISE NOTHING -------------------
for tag, blob, needle in (("json_list", "[1, 2, 3]", "not an object"),
                          ("json_scalar", "42", "not an object"),
                          ("json_broken", "this is not json", "no usable result"),
                          ("sha_type", json.dumps({"status": "proved", "cert_sha256": 12345}),
                           "non-string")):
    d, c, sha = case(tag)
    r = write(d / "out" / "result.json", blob)
    try:
        got = admit(r, want=sha)
    except Exception as e:  # noqa: BLE001
        chk(tag, False, f"RAISED {e.__class__.__name__}, which would erase the earned agree")
        continue
    chk(tag, any(needle in x for x in got), f"refused and named ({needle})")

# ---- missing result ---------------------------------------------------------
d, c, sha = case("missing")
chk("missing_result", any("no usable result" in x for x in admit(d / "out" / "result.json", want=sha)),
    "a missing result is refused")

# ---- the driver's own non-proved verdicts are named, not merged ------------
for tag, status in (("timeout", "timeout"), ("rss_killed", "rss_killed"),
                    ("not_proved", "not_proved")):
    d, c, sha = case("status_" + tag)
    r = write(d / "out" / "result.json", {"status": status, "cert_sha256": sha})
    got = admit(r, want=sha)
    chk("status_" + tag, any(f"driver status {status}" in x for x in got),
        f"a driver status of {status} is refused and named")

sys.exit(1 if fails else 0)
PYEOF
rc=$?

if [[ "$(sha256sum < "$HERE/d3_sweep.py")" != "$SWEEP_SHA" ]]; then
  echo "  FAIL the live d3_sweep.py changed"; rc=1
else
  echo "  ok   the live d3_sweep.py is byte-identical (imported, never written)"
fi
if [[ "$(sha256sum < "$HERE/d3_module_proof.py")" != "$WORKER_SHA" ]]; then
  echo "  FAIL the live d3_module_proof.py changed"; rc=1
else
  echo "  ok   the live d3_module_proof.py is byte-identical (never swapped)"
fi
if [[ "$(sha256sum < "$SRC_CERT")" != "$CERT_SHA" ]]; then
  echo "  FAIL the source certificate was modified by a control"; rc=1
else
  echo "  ok   the source certificate is unchanged (the mutation leg used a copy)"
fi

if [[ $rc -eq 0 ]]; then echo "D3-MODULE-ADMIT CONTROLS OK"; else echo "D3-MODULE-ADMIT CONTROLS FAILED"; fi
exit $rc
