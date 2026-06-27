#!/usr/bin/env python3
# Differential co-sim: inou.cgen.sim (Slop C++) vs Icarus Verilog, X-masked.
# Handles combinational AND clocked sequential designs.
#   Usage: diff_test.py <prp> <top> [nvec]
# Protocol per cycle (both sides): set data inputs -> let comb settle -> sample
# outputs -> pulse the clock (commit). Implicit `clock` is driven by the harness;
# `reset` is held high for a short warmup then released. Where iverilog shows
# x/z (e.g. pre-reset flops) the bit is skipped; Slop has no unknowns.
import os, re, sys, subprocess, random, tempfile

prp, top = sys.argv[1], sys.argv[2]
nvec = int(sys.argv[3]) if len(sys.argv) > 3 else 64
WARMUP = 3  # reset-high cycles before stimulus

SP = os.path.dirname(os.path.abspath(__file__))
LIVEHD = os.path.abspath(os.path.join(SP, "../../../.."))  # repo root
# Generated artifacts (tb.v, a.vvp, the sim/ module + its nested bazel build)
# live in an OS temp dir, never next to this script -- a repo must not accumulate
# auto-generated code. Override with $LHD_DIFFTEST_DIR to inspect the output.
work = os.environ.get("LHD_DIFFTEST_DIR") or os.path.join(tempfile.gettempdir(), f"lhd_difftest_{top}")
os.system(f"rm -rf {work}; mkdir -p {work}")
LHD = subprocess.check_output(["bazel", "info", "bazel-bin"], cwd=LIVEHD).decode().strip() + "/lhd/lhd"

vfile, simdir = f"{work}/{top}.v", f"{work}/sim"
r = subprocess.run([LHD, "compile", prp, "--top", top,
                    "--emit", f"verilog:{vfile}", "--emit-dir", f"sim:{simdir}"],
                   capture_output=True, text=True)
if not os.path.exists(vfile):
    print("lhd compile failed:\n", r.stdout, r.stderr); sys.exit(1)

hpps = [f for f in os.listdir(simdir) if f.endswith(".hpp")]
# pick the TOP module's header (graph name's last dotted component == top)
hdr = next((f for f in hpps if f[:-4].split(".")[-1] == top), hpps[0])
htxt = open(f"{simdir}/{hdr}").read()
struct = re.search(r"struct (\w+) \{", htxt).group(1)
gname = hdr[:-4]
vmod = "\\" + gname + " "

def fields(block):
    res, inb = [], False
    for line in htxt.splitlines():
        if ("struct %s {" % block) in line:
            inb = True; continue
        if inb and line.strip().startswith("};"):
            break
        if inb:
            m = re.search(r"Slop<(\d+)>\s+(\w+)\{\}", line)
            if m:
                res.append((m.group(2), int(m.group(1))))
    return res

ins, outs = fields("In"), fields("Out")
has_clock = any(n == "clock" for n, _ in ins)
data_ins = [(n, b) for n, b in ins if n != "clock"]  # everything the harness assigns per cycle

random.seed(1)
def mkvec(i):
    v = {}
    for n, b in data_ins:
        if n == "reset":
            v[n] = 1 if i < WARMUP else 0
        else:
            v[n] = random.randint(0, (1 << b) - 1)
    return v
vecs = [mkvec(i) for i in range(nvec)]

# ---- Verilog testbench ----
tb = ["`timescale 1ns/1ps", "module tb;"]
for n, b in ins:  tb.append(f"  reg  [{b-1}:0] {n};")
for n, b in outs: tb.append(f"  wire [{b-1}:0] {n};")
ports = ", ".join(f".{n}({n})" for n, _ in ins + outs)
tb.append(f"  {vmod}dut({ports});")
tb.append("  initial begin")
if has_clock: tb.append("    clock = 0;")
for v in vecs:
    for n, b in data_ins:
        tb.append(f"    {n} = {b}'d{v[n]};")
    tb.append("    #1;")
    tb.append('    $display("' + " ".join(["%b"] * len(outs)) + '", ' + ", ".join(n for n, _ in outs) + ");")
    if has_clock: tb.append("    clock = 1; #1; clock = 0; #1;")
tb.append("    $finish;\n  end\nendmodule")
open(f"{work}/tb.v", "w").write("\n".join(tb) + "\n")

iv = subprocess.run(f"iverilog -g2012 -I{LIVEHD}/ware/rtl -o {work}/a.vvp {vfile} {work}/tb.v && vvp {work}/a.vvp",
                    shell=True, capture_output=True, text=True)
iv_lines = [l for l in iv.stdout.splitlines() if re.fullmatch(r"[01xz ]+", l)]

# ---- Slop C++ driver ----
drv = ['#include "%s"' % hdr, "#include <cstdio>", "int main(){", f"  {struct} dut; dut.reset_cycle();"]
for v in vecs:
    drv.append(f"  {{ {struct}::In in;")
    for n, b in data_ins:
        drv.append(f"    in.{n} = Slop<{b}>::create_integer({v[n]}LL);")
    drv.append("    auto o = dut.cycle(in);")
    for n, b in outs:
        drv.append(f'    for(int i={b-1};i>=0;--i) putchar(o.{n}.bit_test(i)?\'1\':\'0\');  putchar(\' \');')
    drv.append("    putchar('\\n'); }")
drv.append("  return 0;\n}")
open(f"{simdir}/drv.cpp", "w").write("\n".join(drv) + "\n")
with open(f"{simdir}/BUILD", "a") as f:
    f.write('\nload("@rules_cc//cc:defs.bzl", "cc_binary")\n'
            'cc_binary(name="drv", srcs=["drv.cpp"], copts=["-std=c++23"], deps=[":sim"])\n')
bz = subprocess.run("bazel run -c opt //:drv 2>/dev/null", shell=True, cwd=simdir, capture_output=True, text=True)
slop_lines = [l for l in bz.stdout.splitlines() if re.fullmatch(r"[01 ]+", l)]

# ---- compare, X-masked ----
if len(iv_lines) != nvec or len(slop_lines) != nvec:
    print(f"line count mismatch: iv={len(iv_lines)} slop={len(slop_lines)} expected {nvec}")
    print("IV sample:", iv_lines[:2]); print("SLOP sample:", slop_lines[:2])
    if not slop_lines: print("driver build/run stderr:", bz.stderr[-2000:])
    sys.exit(1)

bad = 0
for i, (a, b) in enumerate(zip(iv_lines, slop_lines)):
    for j, (ca, cb) in enumerate(zip(a.replace(" ", ""), b.replace(" ", ""))):
        if ca in "xz":
            continue
        if ca != cb:
            if bad < 6:
                print(f"cycle {i} bit {j}: iv={ca} slop={cb}\n  in={vecs[i]}\n  iv  ={a}\n  slop={b}")
            bad += 1
            break
print(f"{'PASS' if bad==0 else 'FAIL'}: {nvec-bad}/{nvec} cycles match (X-masked){' [seq]' if has_clock else ' [comb]'}")
sys.exit(0 if bad == 0 else 1)
