#!/usr/bin/env bash
# ACTUAL-CLI negative controls for the module-proof stage of d3_sweep.py.
#
# These drive the real command line, through the real worker/process boundary,
# and assert on the ROW the run writes -- including that the executable
# `cert..agree` the run earned is retained whenever proof admission refuses.
# `d3_module_admit_controls.sh` exercises the same decisions at the helper
# level and is kept for the cases that cannot be staged through a process; this
# file is the one that proves the boundary itself behaves.
#
# ISOLATION.
#   * A PRIVATE project context: its own `scripts/` (a copy of the sweep and a
#     stub worker), its own certificate copy, its own `--out`. No live worker,
#     library or certificate is written to.
#   * `D3_LEAN_ROOT` points the copy at the real Lean tree -- a copied sweep
#     resolves it perfectly well when told where it is.
#   * `D3_RUN_ID` is set PER CASE, so the run directory is stable and known in
#     advance. That is what makes the stale and failed-unlink legs stageable
#     at the exact path the run will read, rather than at a guess.
#
# ONE LAKE WRITER. The stub worker never invokes Lake; the sweep's own sim and
# typecheck stages do, in `D3_LEAN_ROOT`, so this suite must not run beside
# another build in the same tree.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# <ROOT>/pass/lean/Translation_Validation/scripts/<this file>: four levels down.
ROOT="$(cd "$HERE/../../../.." && pwd)"
SRC_CERT="${D3_CONTROL_CERT:-$ROOT/generated/master_port/d3_pilot/certs/tiny_sync_ram_Lgraph.lean}"
LEAN_ROOT="${D3_LEAN_ROOT:-$ROOT/formal/lean}"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
[[ -s "$SRC_CERT" ]] || { echo "SKIP: no control certificate at $SRC_CERT" >&2; exit 3; }
[[ -d "$LEAN_ROOT/.lake/build" ]] || { echo "SKIP: no Lean build at $LEAN_ROOT" >&2; exit 3; }

W="$ROOT/generated/d3_module_cli_controls"
rm -rf "$W"; mkdir -p "$W"
LIVE_WORKER_SHA="$(sha256sum < "$HERE/d3_module_proof.py")"
LIVE_SWEEP_SHA="$(sha256sum < "$HERE/d3_sweep.py")"
SRC_CERT_SHA="$(sha256sum < "$SRC_CERT")"

ctx() { # ctx <tag> <stub-mode> [alloc_mb]
  local tag="$1"
  local mode="$2"
  local alloc="${3:-0}"
  local d="$W/$tag"
  rm -rf "$d"; mkdir -p "$d/scripts" "$d/certs"
  cp "$HERE/d3_sweep.py" "$HERE/d3_manifest.py" "$HERE/d3_guarded_build.py" \
     "$HERE/d3_gen_modules.py" "$d/scripts/" 2>/dev/null
  cp "$SRC_CERT" "$d/certs/"
  local cert="$d/certs/$(basename "$SRC_CERT")"
  sha256sum < "$cert" | cut -d' ' -f1 > "$d/cert.sha"
  cat > "$d/scripts/d3_module_proof.py" <<STUB
#!/usr/bin/env python3
"""Controls-only stub worker in a PRIVATE context. Never invokes Lean."""
import argparse, hashlib, json, pathlib, sys, time
a = argparse.ArgumentParser(add_help=False)
for o in ("--cert","--module","--bindings","--chunk-size","--timeout","--out-dir"):
    a.add_argument(o)
n, _ = a.parse_known_args()
d = pathlib.Path(n.out_dir); d.mkdir(parents=True, exist_ok=True)
cert = pathlib.Path(n.cert)
mode, alloc_mb = "$mode", int("$alloc")
sha = pathlib.Path("$d/cert.sha").read_text().strip()
if alloc_mb:
    # A bounded allocation, held long enough for a 0.05 s sampler to see it.
    blob = bytearray(alloc_mb * 1024 * 1024)
    for i in range(0, len(blob), 4096):
        blob[i] = 1
    time.sleep(8)
if mode == "mutate_cert":
    with cert.open("a") as fh:
        fh.write("\n-- control: the worker changed its own input\n")
    sha = hashlib.sha256(cert.read_bytes()).hexdigest()
if mode == "hang":
    time.sleep(3600)
res = {"status": "proved", "cert_sha256": sha, "chunk_size": 8, "chunks": 1,
       "groups": 0, "final_max_rss_kb": 1, "final_wall_s": 0.1,
       "final_oom_kill": 0, "tool_digest": "stub", "olean_digest": "stub"}
out = d / "result.json"
if mode == "no_output":     pass
elif mode == "malformed":   out.write_text("not json at all")
elif mode == "schema_list": out.write_text("[1,2,3]")
else:                       out.write_text(json.dumps(res))
sys.exit(17 if mode == "nonzero" else 0)
STUB
  chmod +x "$d/scripts/d3_module_proof.py"
}

run() { # run <tag> <runid-suffix> [extra sweep args...]
  local tag="$1"; shift
  local rid="$1"; shift
  local d="$W/$tag"
  D3_LEAN_ROOT="$LEAN_ROOT" D3_RUN_ID="ctl-$tag-$rid" \
    nice -n19 ionice -c3 timeout 1800 python3 -I "$d/scripts/d3_sweep.py" \
      --certs "$d/certs" --out "$d/rows.tsv" --reifier chunked --prove \
      --proof-backend module --chunk-size 8 --jobs 1 --timeout 900 "$@" \
      > "$d/run-$rid.log" 2>&1
  echo $?
}
f() { awk -F'\t' -v c="$2" 'NR==1{for(i=1;i<=NF;i++) if($i==c) k=i; next} {print $k; exit}' "$W/$1/rows.tsv"; }

row_check() { # row_check <tag> <proof> <admit-substr> <desc> [run_status]
  local tag="$1" wp="$2" wa="$3" desc="$4" wrs="${5:-}"
  local gp ga gv grs
  gp="$(f "$tag" module_proof)"; ga="$(f "$tag" module_admit)"
  gv="$(f "$tag" verdict)"; grs="$(f "$tag" run_status)"
  if [[ "$gp" != "$wp" ]]; then bad "$desc: module_proof='$gp' expected '$wp'"
  elif [[ -n "$wa" ]] && ! grep -qF "$wa" <<<"$ga"; then
    bad "$desc: admit='${ga:0:100}' does not name '$wa'"
  elif [[ "$gv" != "agree" ]]; then
    bad "$desc: the earned executable verdict was retracted (verdict='$gv')"
  elif [[ -n "$wrs" && "$grs" != "$wrs" ]]; then
    bad "$desc: run_status='$grs' expected '$wrs'"
  else ok "$desc"; fi
}

# ---- 1. positive: the real boundary, a real row ----------------------------
ctx positive proved; rc="$(run positive a --force)"
[[ "$rc" -eq 0 ]] && ok "positive: the CLI exits 0" || bad "positive: CLI exit $rc"
row_check positive 1 "ok" "a proved result from an exit-0 worker is credited" "done"

# ---- 2. nonzero after a proved result --------------------------------------
ctx nonzero nonzero; run nonzero a --force > /dev/null
row_check nonzero 0 "worker exit 17" "a proved result behind exit 17 is refused" "done"

# ---- 3. stale: the SAME run id, so the path is known and assertable --------
ctx stale proved; run stale a --force > /dev/null
SDIR="$(f stale module_log_dir)"
[[ -s "$SDIR/result.json" ]] && ok "the seeding run left a result.json to reuse" \
                             || bad "no seed at $SDIR/result.json"
cp "$SDIR/result.json" "$W/stale/seed.json"
sed -i 's/^mode, alloc_mb = "proved"/mode, alloc_mb = "no_output"/' "$W/stale/scripts/d3_module_proof.py"
cp "$W/stale/seed.json" "$SDIR/result.json"
[[ -s "$SDIR/result.json" ]] && ok "the stale result is staged at the exact path the run reads" \
                             || bad "could not stage the stale result"
run stale a --force > /dev/null
row_check stale 0 "no usable result" "a stale result from an earlier attempt is refused" "done"

# ---- 4. failed unlink: the same path, made unremovable ---------------------
ctx blocked proved; run blocked a --force > /dev/null
BDIR="$(f blocked module_log_dir)"
cp "$BDIR/result.json" "$W/blocked/seed.json"
sed -i 's/^mode, alloc_mb = "proved"/mode, alloc_mb = "no_output"/' "$W/blocked/scripts/d3_module_proof.py"
cp "$W/blocked/seed.json" "$BDIR/result.json"
chmod 0555 "$BDIR"
run blocked a --force > /dev/null
chmod 0755 "$BDIR" 2>/dev/null
row_check blocked 0 "could not clear a previous result" \
          "a preseeded result that cannot be removed is refused" "done"

# ---- 5. the worker rewrote its own input -----------------------------------
ctx mutate mutate_cert; run mutate a --force > /dev/null
row_check mutate 0 "certificate changed" "a worker that rewrites its input is refused" "done"

# ---- 6/7. missing and malformed schema -------------------------------------
ctx missing no_output; run missing a --force > /dev/null
row_check missing 0 "no usable result" "a missing result is refused" "done"
ctx malformed malformed; run malformed a --force > /dev/null
row_check malformed 0 "no usable result" "a malformed result is refused" "done"
ctx schemalist schema_list; run schemalist a --force > /dev/null
row_check schemalist 0 "not an object" "a result.json that is a LIST is refused, not raised" "done"

# ---- 8. a real timeout: the worker hangs, the outer bound stops it ---------
ctx hang hang; run hang a --force --module-proof-timeout 5 > /dev/null
row_check hang 0 "worker exit 124" "a hung worker is stopped by the outer bound" "done"

# ---- 9. the sampled cap stops the module worker, and the row says so -------
# SIZED TO THE MODULE STAGE. The stages are SEQUENTIAL, so the aggregate
# during the module stage is the worker alone -- the sim probe's memory is
# already freed. A cap below the sim peak therefore trips at the SIM stage and
# never reaches the module worker at all: measured at 90,000 kB, the row came
# back `rss_killed` from the sim probe at 132,728 kB, which is correct
# behaviour and the wrong stage. The worker must out-allocate the sim probe
# (~1,520,000 kB here) for the cap to catch it where this leg is aimed.
ctx overcap proved 2048
run overcap a --force --kill-over-rss-kb 1800000 --rss-sample-seconds 0.05 > /dev/null
row_check overcap 0 "aggregate RSS limit" \
          "a module worker over the sampled cap is stopped and refused" "module_stopped"
[[ "$(f overcap module_proof_status)" == "sampled_stop" ]] \
  && ok "the stop has explicit provenance (module_proof_status=sampled_stop)" \
  || bad "module_proof_status='$(f overcap module_proof_status)', expected sampled_stop"
# ...and it is RESUMABLE: a nonterminal row is run again rather than reused.
run overcap b --resume --dry-run > /dev/null
if grep -q "tiny_sync_ram" "$W/overcap/run-b.log"; then
  ok "the stopped row is nonterminal, so --resume runs it again"
else
  bad "--resume did not re-select the stopped row"
  tail -3 "$W/overcap/run-b.log" | sed 's/^/       /'
fi

# ---- nothing live was touched ----------------------------------------------
[[ "$(sha256sum < "$HERE/d3_module_proof.py")" == "$LIVE_WORKER_SHA" ]] \
  && ok "the live d3_module_proof.py is byte-identical" || bad "the live worker changed"
[[ "$(sha256sum < "$HERE/d3_sweep.py")" == "$LIVE_SWEEP_SHA" ]] \
  && ok "the live d3_sweep.py is byte-identical" || bad "the live sweep changed"
[[ "$(sha256sum < "$SRC_CERT")" == "$SRC_CERT_SHA" ]] \
  && ok "the source certificate is unchanged" || bad "the source certificate changed"

if [[ $fails -eq 0 ]]; then echo "D3-MODULE-CLI CONTROLS OK"; else echo "D3-MODULE-CLI CONTROLS FAILED ($fails)"; fi
exit $fails
