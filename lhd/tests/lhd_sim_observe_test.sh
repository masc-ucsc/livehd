#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Observe native checkpoint intervals, including a boundary in a second tick.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_observe_$$}"
mkdir -p "$W"

cat > "$W/observe.prp" <<'PRP'
mod lane(en:Bool) -> (value:U8@[0]) {
  reg count:U8 = 0
  value = count
  if en { wrap count += 1 }
}
mod dut(en:Bool, addr:U2, data:U17) -> (out:U8@[0], nested:U8@[0], rd:U17@[0]) {
  reg acc:U8 = 0
  reg bank:[4]U17 = 0
  if en {
    wrap acc += 1
    bank[addr] = data
  }
  out = acc
  nested = lane(en=en)
  rd = bank[addr]
}
test dut.run(n:U8) {
  mut d = dut
  mut n2 = n + 1
  tick 6 {
    d.en = true
    d.addr = U2(clock)
    d.data = U17(0x10100 + clock)
    step
  }
  tick n2 {
    d.addr = U2(clock)
    d.data = U17(0x10120 + clock)
    step
    if clock == 2 { assert(d.out < 8, "anchor") }
  }
}
PRP

# Listing needs neither the required +n parameter nor the trace object.
"$LHD" sim "$W/observe.prp" --list-signals --workdir "$W/list" --result-json "$W/list.json" -q
[ ! -e "$W/list/sim/drv.tb.trace.o" ]

cat > "$W/query.json" <<'JSON'
{"schema_version":1,"kind":"sim_query","queries":[
  {"id":"before","op":"value","signal":"d.acc","at":{"event":"fail","offset":-1}},
  {"id":"output","op":"value","signal":"d.out","at":{"cycle":8}},
  {"id":"memory","op":"value","signal":"d.bank[2]","at":{"cycle":8}},
  {"id":"packed","op":"value","signal":"d.bank","index":3,"at":{"cycle":9}},
  {"id":"next","op":"next_change","signal":"d.acc","after":{"cycle":7}},
  {"id":"count","op":"changes","signal":"d.acc","from":{"cycle":2},"to":{"cycle":12},"count_only":true},
  {"id":"first","op":"find","from":{"cycle":8},"expr":{"all":[{"sig":"d.acc","cmp":"changed"},{"sig":"d.acc","cmp":">=","value":9}]},"sample":["d.acc"]},
  {"id":"last","op":"find","dir":"backward","to":{"cycle":10},"expr":{"sig":"d.acc","cmp":">=","value":9}},
  {"id":"diff","op":"diff","signal":"d.acc","from":{"cycle":3},"to":{"cycle":12}},
  {"id":"snapshot","op":"snapshot","scope":"d","at":{"cycle":8}},
  {"id":"bad","op":"value","signal":"d.missing","at":{"cycle":8}}
]}
JSON

common=("$W/observe.prp" dut.run +n=6 --workdir "$W/run"
        --probe "d.acc, d.__in.en" --probe-from 6 --probe-to 10 --break-when "d.acc >= 9"
        --vcd-from 0 --vcd-to 12 --set sim.checkpoint_every=4 --set sim.checkpoint_max=0 -q)
run() {
  local tag=$1 rc=0
  shift
  "$LHD" sim "${common[@]}" --result-json "$W/$tag.json" "$@" >"$W/$tag.out" 2>"$W/$tag.err" || rc=$?
  if [ "$rc" -ne 11 ]; then
    cat "$W/$tag.out" "$W/$tag.err" >&2
    echo "expected the recorded test assertion to determine the exit code, got $rc" >&2
    exit 1
  fi
  cp "$W/run/dut.run.vcd" "$W/$tag.vcd"
}
run serial --query "$W/query.json" --set lhd.jobs=1
run parallel --run-only --query - --set lhd.jobs=3 <"$W/query.json"
run cold --run-only --query "$(cat "$W/query.json")" --set sim.checkpoint=false
run restart --run-only --query "$W/query.json" --restart-cycle 12
cmp "$W/serial.vcd" "$W/parallel.vcd"
cmp "$W/serial.vcd" "$W/cold.vcd"
cmp "$W/serial.vcd" "$W/restart.vcd"

python3 - "$W" <<'PY'
import json, pathlib, sys

w = pathlib.Path(sys.argv[1])
def load(tag):
    return json.loads((w / f"{tag}.json").read_text())

listing = {s["name"]: s for s in load("list")["debug"]["signals"]}
assert listing["d.acc"]["kind"] == "flop", listing
assert listing["d.__in.en"]["bits"] == 1, listing
assert any(name.endswith(".count") for name in listing), listing
assert listing["d.bank"]["size"] == 4, listing

serial = load("serial")
for tag in ("parallel", "cold", "restart"):
    other = load(tag)
    assert other["debug"] == serial["debug"], other["debug"]
    assert other["query"]["results"] == serial["query"]["results"], other["query"]
assert serial["tests"][0]["cycle"] == 8
assert serial["tests"][0]["sim_cycles"] == 13
assert serial["query"]["run"]["fail_cycle"] == 8
assert serial["query"]["run"]["checkpoints_used"] == [4, 8, 12], serial["query"]["run"]
assert load("cold")["query"]["run"]["checkpoints_used"] == []
assert (w / "run/ckpt/dut_run/c8/complete").is_file()
assert serial["debug"]["probe"]["rows"] == [
    {"cycle": c, "d.acc": c + 1, "d.__in.en": 1} for c in range(6, 11)
]
assert serial["debug"]["break"]["cycle"] == 8
assert serial["debug"]["break"]["state"]["d.acc"] == 9

results = {q["id"]: q for q in serial["query"]["results"]}
assert results["before"]["value"]["dec"] == "8", results
assert results["output"]["value"]["dec"] == "8", results
assert results["output"]["value"]["sampled"] == "during_period"
assert results["memory"]["value"]["hex"] == "10122", results
assert results["packed"]["value"]["hex"] == "10123", results
assert results["next"]["changes"][0]["cycle"] == 8, results
assert results["count"]["count"] == 11 and not results["count"]["changes"], results
assert results["first"]["found"] and results["first"]["at"]["cycle"] == 8, results
assert results["first"]["sample"][0]["value"]["dec"] == "9"
assert results["last"]["at"]["cycle"] == 10, results
assert results["diff"]["diff"][0]["to_value"]["dec"] == "13", results
assert {v["signal"] for v in results["snapshot"]["values"]} >= {"d.acc", "d.en", "d.out"}, results
assert results["bad"]["error"]["class"] == "unknown_signal", results
timestamps = [int(line[1:]) for line in (w / "serial.vcd").read_text().splitlines() if line.startswith("#")]
assert timestamps == list(range(0, 130, 5)), timestamps
PY

# Ambiguous selectors, boolean operators and time coordinates must be diagnosed.
for bad in \
  '{"id":"bad","op":"find","expr":{"all":[{"sig":"d.acc","cmp":"changed"}],"any":[]}}' \
  '{"id":"bad","op":"value","signal":"d.bank[1]","index":2,"at":{"cycle":0}}' \
  '{"id":"bad","op":"value","signal":"d.acc","at":{"cycle":2,"offset":3}}'; do
  query='{"schema_version":1,"kind":"sim_query","queries":['"$bad"']}'
  rc=0
  "$LHD" sim "$W/observe.prp" dut.run +n=6 --run-only --workdir "$W/run" --query "$query" >"$W/invalid.log" 2>&1 || rc=$?
  [ "$rc" -eq 2 ] || { cat "$W/invalid.log" >&2; exit 1; }
done
