#!/usr/bin/env bash
# Controls for sweep_manifest.py, through its real CLI on isolated fixtures.
#
# The manifest links an export/proof disposition to a file. It must therefore
# compare the digest the EXPORT recorded with the bytes on disk NOW -- the
# first version hashed the file and wrote that down, never comparing, so a
# changed artifact kept its PROVEN record and was labelled sweepable.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GEN="$ROOT/scripts/sweep_manifest.py"
W="$ROOT/generated/master_port/sweep_manifest_controls"
rc=0
rm -rf "$W"; mkdir -p "$W/results" "$W/art"

ART="$W/art/Ctl_Lgraph.lean"
cat > "$ART" <<'EOF'
def Ctl_designCert : DesignCert := { sources := #[], nodes := #[], outputs := #[] }
EOF
printf '{"kind":"lean_design_io"}\n' > "$W/art/Ctl_io.json"
REC_A=$(python3 -I -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest()[:16])" "$ART")
REC_S=$(python3 -I -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest()[:16])" "$W/art/Ctl_io.json")

printf '{"configurations":{},"jobs":[{"family":"CTL","case":"Ctl","top":"Ctl","mode":"verified_compiler"}]}\n' > "$W/inv.json"
mk_record() { # <artifact-digest> <sidecar-digest-or-empty>
  python3 -I -c "
import json, os, sys
rec = {'family':'CTL','case':'Ctl','top':'Ctl','mode':'verified_compiler',
       'export_state':'EXPORTED','proof_state':'PROVEN',
       'artifact': os.path.relpath(sys.argv[3], sys.argv[4]),
       'artifact_sha256': sys.argv[1]}
if sys.argv[2]: rec['sidecar_sha256'] = sys.argv[2]
json.dump(rec, open(sys.argv[5],'w'), indent=1)" "$1" "${2:-}" "$ART" "$ROOT" \
    "$W/results/CTL__Ctl__verified_compiler.json"
}
build() { python3 -I "$GEN" --out "$W/m.json" --inventory "$W/inv.json" --results "$W/results" \
            --fingerprint /nonexistent > "$W/build.log" 2>&1; }
cls() { python3 -I -c "
import json
m=json.load(open('$W/m.json'))
e=[x for x in m['entries'] if x['case']=='Ctl'][0]
print(e['classification'], '|', e.get('why',''))"; }

mk_record "$REC_A" "$REC_S"; build
case "$(cls)" in sweepable*) echo "ok: matching digests -> sweepable" ;;
  *) echo "MISS: matching digests -> $(cls)"; rc=1 ;; esac

# THE DEFECT: the file changes, the record does not.
cp "$ART" "$W/art/.orig"
printf '\n-- one more line\n' >> "$ART"
build
case "$(cls)" in provenance_mismatch*) echo "ok: changed artifact -> provenance_mismatch" ;;
  *) echo "MISS: changed artifact -> $(cls)"; rc=1 ;; esac
python3 -I -c "
import json
m=json.load(open('$W/m.json'))
import sys
sys.exit(0 if not m['to_run'] else 1)" \
  && echo "ok: a mismatched payload is not in to_run" \
  || { echo "MISS: a mismatched payload was queued to run"; rc=1; }
cp "$W/art/.orig" "$ART"

# A record with no recorded digest is UNRESOLVED, never minted from the file.
mk_record "" ""; build
case "$(cls)" in provenance_unresolved*) echo "ok: no recorded digest -> provenance_unresolved" ;;
  *) echo "MISS: no recorded digest -> $(cls)"; rc=1 ;; esac

# A recorded sidecar digest that disagrees is a mismatch, not merely a note.
mk_record "$REC_A" "0000000000000000"; build
case "$(cls)" in provenance_mismatch*) echo "ok: changed sidecar -> provenance_mismatch" ;;
  *) echo "MISS: changed sidecar -> $(cls)"; rc=1 ;; esac

[ "$rc" -eq 0 ] && echo "PASS: a disposition is linked only to the bytes the record describes" \
                || echo "FAIL: sweep_manifest controls missed"
exit $rc
