#!/usr/bin/env bash
# Controls for MODULE-PROOF RESULT ADMISSION in d3_sweep.py.
#
# WHAT THIS GUARDS. The runner credited `module_proof=1` on one test --
# `res["status"] == "proved"` -- and nothing else, so three different wrong
# results were credited, each reproduced against this CLI:
#
#   * a worker that printed a proved result and then exited 17;
#   * a STALE result.json from an earlier attempt, this run producing none;
#   * a result naming a DIFFERENT certificate: the row carried the correct
#     cert_sha256 beside an all-zero module_cert_sha256 and still said
#     module_proof=1.
#
# The driver's own checks (gate line, rc, allowed axioms, olean drift) are
# unchanged and are still what `status == "proved"` means. What is tested here
# is the boundary around them: the worker must have SUCCEEDED, and the result
# must be THIS run's, about THIS certificate.
#
# Every leg drives the ACTUAL CLI with a stub worker in place of
# d3_module_proof.py. These stubs are HARNESS evidence; no Lean proof is run
# and no leg here says anything about a theorem. Each refused leg also checks
# that the executable verdict (cert..agree) the run genuinely earned is
# PRESERVED -- a refused proof must not retract an agreement.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SWEEP="$HERE/d3_sweep.py"
WORKER="$HERE/d3_module_proof.py"
SRC="${D3_CONTROL_CERT:-$ROOT/generated/master_port/d3_pilot/certs/tiny_sync_ram_Lgraph.lean}"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
for f in "$SWEEP" "$WORKER"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done
[[ -s "$SRC" ]] || { echo "SKIP: no control certificate at $SRC" >&2; exit 3; }

W="$ROOT/generated/d3_module_admit_controls"
rm -rf "$W"; mkdir -p "$W/certs"
cp "$SRC" "$W/certs/"
CERT="$W/certs/$(basename "$SRC")"
CSHA="$(sha256sum < "$CERT" | cut -d' ' -f1)"

# The real worker is restored whatever happens.
cp "$WORKER" "$W/worker_real.py"
trap 'cp -f "$W/worker_real.py" "$WORKER" 2>/dev/null' EXIT INT TERM

# mk_worker <mode>; writes a stub in place of d3_module_proof.py.
mk_worker() {
  cat > "$WORKER" <<STUB
#!/usr/bin/env python3
"""Controls-only stub. NOT a prover: it writes a result and exits."""
import argparse, json, pathlib, sys
a = argparse.ArgumentParser(add_help=False)
a.add_argument("--cert"); a.add_argument("--module"); a.add_argument("--bindings")
a.add_argument("--chunk-size"); a.add_argument("--timeout"); a.add_argument("--out-dir")
n, _ = a.parse_known_args()
d = pathlib.Path(n.out_dir); d.mkdir(parents=True, exist_ok=True)
mode = "$1"
res = {"status": "proved", "cert_sha256": "$CSHA", "chunk_size": 8, "chunks": 1,
       "groups": [], "final_max_rss_kb": 1, "final_wall_s": 0.1,
       "tool_digest": "stub", "olean_digest": "stub"}
if mode == "wrong_cert":
    res["cert_sha256"] = "0" * 64
if mode == "driver_timeout":
    res["status"] = "timeout"
if mode == "rss_killed":
    res["status"] = "rss_killed"
if mode == "malformed":
    (d / "result.json").write_text("this is not json")
elif mode in ("no_output", "stale"):
    pass
else:
    (d / "result.json").write_text(json.dumps(res))
sys.exit(17 if mode == "nonzero" else 0)
STUB
  chmod +x "$WORKER"
}

run() { # run <tag>
  nice -n19 ionice -c3 timeout 1800 python3 -I "$SWEEP" --certs "$W/certs" \
    --out "$W/$1.tsv" --reifier chunked --prove --proof-backend module \
    --chunk-size 8 --jobs 1 --timeout 900 --force > "$W/$1.log" 2>&1
  echo $?
}
f() { awk -F'\t' -v c="$2" 'NR==1{for(i=1;i<=NF;i++) if($i==c) k=i; next} {print $k; exit}' "$W/$1.tsv"; }

check() { # check <tag> <want_proof> <want_admit_substr> <desc>
  local tag="$1" wp="$2" wa="$3" desc="$4"
  local gp ga gv
  gp="$(f "$tag" module_proof)"; ga="$(f "$tag" module_admit)"; gv="$(f "$tag" verdict)"
  if [[ "$gp" != "$wp" ]]; then
    bad "$desc: module_proof=$gp, expected $wp"
  elif [[ -n "$wa" ]] && ! grep -qF "$wa" <<<"$ga"; then
    bad "$desc: admit='$ga' does not name '$wa'"
  elif [[ "$gv" != "agree" ]]; then
    bad "$desc: the earned executable verdict was retracted (verdict='$gv')"
  else
    ok "$desc"
  fi
}

mk_worker proved;         run positive  > /dev/null
check positive 1 "ok" "a fresh proved result from a worker that exited 0 is credited"

mk_worker nonzero;        run nonzero   > /dev/null
check nonzero 0 "worker exit 17" "a proved result behind exit 17 is refused"

# STALE: the positive leg left a result.json in ITS own directory, so copy that
# directory into place and then run a worker that produces nothing.
mk_worker proved;         run seed      > /dev/null
SEED_DIR="$(f seed module_log_dir)"
mk_worker no_output
mkdir -p "$W/staledir"; cp "$SEED_DIR/result.json" "$W/staledir/" 2>/dev/null
# Put the stale file where THIS run's worker would write it.
NEXT_DIR="$(dirname "$SEED_DIR")"
run stale > /dev/null
STALE_DIR="$(f stale module_log_dir)"
mkdir -p "$STALE_DIR"; cp "$W/staledir/result.json" "$STALE_DIR/" 2>/dev/null
run stale2 > /dev/null
check stale2 0 "no result.json from this run" "a stale result from an earlier attempt is refused"

mk_worker wrong_cert;     run wrongcert > /dev/null
check wrongcert 0 "names certificate" "a result naming a different certificate is refused"

mk_worker no_output;      run missing   > /dev/null
check missing 0 "no result.json" "a missing result is refused"

mk_worker malformed;      run malformed > /dev/null
check malformed 0 "no result.json" "a malformed result is refused"

mk_worker driver_timeout; run dtimeout  > /dev/null
check dtimeout 0 "driver status timeout" "a driver timeout is refused and named, not failed"

# A MEMORY STOP IS NOT A DISPROOF. `d3_guarded_build.py` returns 137 when the
# cgroup OOM-killed the composition, and that used to land in `not_proved` --
# which says the gate was evaluated and disagreed, when it was never reached.
mk_worker rss_killed;     run rsskill   > /dev/null
check rsskill 0 "driver status rss_killed" "a composition killed at its memory cap is named as a resource stop"

cp -f "$W/worker_real.py" "$WORKER"
if [[ "$(sha256sum < "$WORKER")" == "$(sha256sum < "$W/worker_real.py")" ]]; then
  ok "d3_module_proof.py is byte-identical after the controls"
else
  bad "d3_module_proof.py was left modified"
fi

if [[ $fails -eq 0 ]]; then echo "D3-MODULE-ADMIT CONTROLS OK"; else echo "D3-MODULE-ADMIT CONTROLS FAILED ($fails)"; fi
exit $fails
