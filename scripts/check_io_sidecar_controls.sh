#!/usr/bin/env bash
set -uo pipefail
# Controls for scripts/check_io_sidecar.py, driven through its ACTUAL CLI.
#
# A validator is only as good as the documents it REJECTS, and the first version
# of this one accepted a negative arity, negative widths, a boolean ordinal,
# outputs with no name or width, clocks with no name, and a duplicated `top`
# key -- then crashed on a port that was not an object. Each of those is a case
# below.
#
# Usage: check_io_sidecar_controls.sh [<a-real-sidecar.json> ...]

R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK="$R/scripts/check_io_sidecar.py"
T="$R/generated/pass_lean_runtime_tmp/sidecar_controls"
rm -rf "$T"; mkdir -p "$T"
rc=0

ok_doc() {  # a valid document, as a base to corrupt
  cat <<'JSON'
{"schema_version": 1, "kind": "lean_design_io", "top": "t",
 "runtime_input_arity": 1,
 "inputs":  [{"name": "a", "ordinal": 0, "width": 8, "used_by_model": true}],
 "outputs": [{"name": "z", "ordinal": 0, "width": 8}],
 "clocks":  [{"name": "clk", "ordinal": 0, "implicit": false}]}
JSON
}
reject() {  # <name> <json-text>
  printf '%s\n' "$2" > "$T/$1.json"
  if python3 -I "$CHECK" "$T/$1.json" >/dev/null 2>&1; then
    echo "FAIL: '$1' was ACCEPTED"; rc=1
  else
    echo "ok: $1 rejected"
  fi
}

ok_doc > "$T/good.json"
if python3 -I "$CHECK" "$T/good.json" >/dev/null 2>&1; then
  echo "ok: a valid sidecar is accepted"
else
  echo "FAIL: the valid sidecar was REJECTED -- the validator is too strict"; rc=1
  python3 -I "$CHECK" "$T/good.json" | sed 's/^/     /'
fi

reject wrong_kind        "$(ok_doc | sed 's/lean_design_io/something_else/')"
reject neg_arity         "$(ok_doc | sed 's/"runtime_input_arity": 1/"runtime_input_arity": -5/')"
reject arity_too_large   "$(ok_doc | sed 's/"runtime_input_arity": 1/"runtime_input_arity": 9/')"
reject neg_width         "$(ok_doc | sed 's/"width": 8, "used_by_model"/"width": -8, "used_by_model"/')"
reject zero_width        "$(ok_doc | sed 's/"width": 8, "used_by_model"/"width": 0, "used_by_model"/')"
# isinstance(True, int) is True in Python; a JSON bool is not an ordinal here.
reject bool_ordinal      "$(ok_doc | sed 's/"ordinal": 0, "width": 8, "used_by_model"/"ordinal": false, "width": 8, "used_by_model"/')"
reject shuffled_ordinal  "$(ok_doc | sed 's/"ordinal": 0, "width": 8, "used_by_model"/"ordinal": 3, "width": 8, "used_by_model"/')"
reject missing_out_name  "$(ok_doc | sed 's/{"name": "z", "ordinal": 0, "width": 8}/{"ordinal": 0, "width": 8}/')"
reject missing_out_width "$(ok_doc | sed 's/{"name": "z", "ordinal": 0, "width": 8}/{"name": "z", "ordinal": 0}/')"
reject missing_clk_name  "$(ok_doc | sed 's/{"name": "clk", "ordinal": 0, "implicit": false}/{"ordinal": 0, "implicit": false}/')"
reject empty_clocks      "$(ok_doc | sed 's/\[{"name": "clk", "ordinal": 0, "implicit": false}\]/[]/')"
reject nonobject_port    "$(ok_doc | sed 's/{"name": "a", "ordinal": 0, "width": 8, "used_by_model": true}/"oops"/')"
# json.load keeps the LAST duplicate silently, so the document says two things.
reject duplicate_top     "$(ok_doc | sed 's/"top": "t",/"top": "t", "top": "other",/')"
reject not_json          'this is not json at all'
reject top_level_array   '[1, 2, 3]'

# Any real sidecars passed on the command line must pass.
for f in "$@"; do
  if python3 -I "$CHECK" "$f" >/dev/null 2>&1; then
    echo "ok: real sidecar accepted ($(basename "$f"))"
  else
    echo "FAIL: a real emitted sidecar was REJECTED: $f"; rc=1
    python3 -I "$CHECK" "$f" | sed 's/^/     /'
  fi
done

[[ $rc -eq 0 ]] && echo "PASS: the validator rejects what it should and accepts what it should"
exit $rc
