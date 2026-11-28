#!/usr/bin/env python3
"""Stimulus, edge schedule and a Verilog testbench for a vref design, from its _io.json.
usage: make_inputs.py <io.json> <cycles> <seed> <outdir> [--zero-div] [--top NAME]  (default top: allops)
Cycle k: inputs set, outputs sampled before the edge (cert_trace's STEP k), then each clock
that fires at k (edges v1) has one rising edge. rst_n is low on cycles 0-1 and 37-38 (an async
reset asserted and released mid-run). The testbench starts rst_n high and drops it at cycle 0, so
the reset has a falling edge (a simulator fires no negedge for a signal that starts low).
Divisors are never 0 unless --zero-div (Verilog's x/0 is X)."""
import json, random, sys
io = json.load(open(sys.argv[1])); N = int(sys.argv[2]); seed = int(sys.argv[3]); out = sys.argv[4]
zero_div = "--zero-div" in sys.argv
top = sys.argv[sys.argv.index("--top") + 1] if "--top" in sys.argv else "allops"
random.seed(seed)
ins = sorted(io["inputs"], key=lambda p: p["ordinal"])
outs = sorted(io["outputs"], key=lambda p: p["ordinal"])
clocks = sorted(io["clocks"], key=lambda c: c["ordinal"])
clock_names = [c["name"] for c in clocks]
def edge_values(w):
    m = (1 << w) - 1
    return [0, 1, m, 1 << (w - 1), (1 << (w - 1)) - 1, m - 1, 2, 3]
vals = []
for k in range(N):
    row = {}
    for p in ins:
        n, w = p["name"], p["width"]
        if n in clock_names: v = 0
        elif n == "rst_n": v = 0 if k in (0, 1, 37, 38) else 1
        elif k < 8 and w > 1: v = edge_values(w)[(k + p["ordinal"]) % 8]
        else: v = random.getrandbits(w)
        if not zero_div and n.startswith("b") and v == 0: v = 1
        if n == "sh" and k >= 8: v = random.choice([0, 1, 7, 8, 9, 31, 32, 33, 63, 64, 65, 127, 128, 200, random.getrandbits(8)])
        row[n] = v
    vals.append(row)
with open(f"{out}/stim.txt", "w") as f:
    f.write("stim v1\n")
    for k, row in enumerate(vals):
        for p in ins:
            if row[p["name"]]: f.write(f"at {k} in {p['ordinal']} {p['width']} {row[p['name']]}\n")
fires = []
with open(f"{out}/edges.txt", "w") as f:
    f.write("edges v1\n")
    for k in range(N):
        b = [random.random() < 0.75 for _ in clocks] if k >= 2 else [True] * len(clocks)
        fires.append(b)
        f.write(f"at {k} {' '.join('1' if x else '0' for x in b)}\n")
L = ["`timescale 1ns/1ps", "module tb;"]
for p in ins: L.append(f"  reg [{p['width']-1}:0] {p['name']};")
for p in outs: L.append(f"  wire [{p['width']-1}:0] {p['name']};")
L.append(f"  {top} dut(" + ", ".join(f".{p['name']}({p['name']})" for p in ins + outs) + ");")
L.append("  initial begin")
for p in ins: L.append(f"    {p['name']} = {1 if p['name'] == 'rst_n' else 0};")
L.append("    #1;")
for k, row in enumerate(vals):
    for p in ins:
        if p["name"] not in clock_names: L.append(f"    {p['name']} = {p['width']}'d{row[p['name']]};")
    L.append("    #1;")
    L.append(f'    $display("STEP {k} ' + " ".join("%0d" for _ in outs) + '", ' + ", ".join(p["name"] for p in outs) + ");")
    for c, f_ in zip(clock_names, fires[k]):
        if f_: L.append(f"    {c} = 1;")
    L.append("    #1;")
    for c in clock_names: L.append(f"    {c} = 0;")
    L.append("    #1;")
L += ["    $finish;", "  end", "endmodule"]
open(f"{out}/tb.v", "w").write("\n".join(L) + "\n")
print(f"{len(ins)} inputs, {len(outs)} outputs, {len(clocks)} clocks, {N} cycles")
