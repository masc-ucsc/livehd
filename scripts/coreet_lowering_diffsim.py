#!/usr/bin/env python3
"""Randomized + directed DIFFERENTIAL SIMULATION of a candidate LiveHD lowering.

THIS IS NOT EQUIVALENCE CHECKING.  It runs a finite vector set through two
implementations and compares outputs.  A pass is reported as

    DIFFSIM-PASS seed=<s> vectors=<n>

and means exactly that: no disagreement was observed on those vectors.  It is
not a proof, and nothing here should be described as LEC, proven or equivalent.

WHY NOT `lhd lec`.  For the modules this exists for, the natural gate
(`--impl lg:<candidate> --ref verilog:<raw RTL>`) CANNOT run: its reference side
elaborates the RTL through the shipped `proc -ifx`, which is the lowering that
leaves the word-level cycle, so the solver refuses the REFERENCE --

    ref encode failed: operand of 'mux_440' has no encodable driver
    root: WORD-LEVEL CYCLE through: mux_440 -> mux_536 -> mux_532 -> mux_536

and `--reader slang` cannot elaborate this RTL at all.  The reference used here
is therefore an INDEPENDENT SIMULATOR on the original sources (verilator, which
is CORE-ET's own DV flow), sharing no lowering with the thing under test.

DOMAIN.  Defined 2-state inputs only.  That is the honest limit: the lowering
difference under test is about what a case selector being X does, and this says
nothing about that.

VECTORS.  Directed first, then random, because random vectors alone can miss
exactly the branch-coverage question at issue: all-zero, all-one, one-hot and
one-cold over the whole input word, and every value of each narrow input port
(these are where case selectors live) against both a zero and an all-ones
background.  Only then the pseudorandom fill.

Usage:
  coreet_lowering_diffsim.py --top M --impl impl_M.v
                             (--filelist M.f | --ref-netlist other_M.v)
                             --out DIR [--vectors 2000] [--seed 1]
"""
import argparse
import hashlib
import itertools
import json
import os
import random
import re
import subprocess
import sys

# The cgen memory blackboxes an EMITTED netlist `include`s by BARE NAME
# (`\`include "cgen_memory_1rd_1wr.v"`).  cgen writes the include but not the
# file: the implementations are tracked sources under ware/rtl/.
#
# Resolved from THIS FILE, never from the caller's cwd.  A cwd-relative path
# would appear to work from the repo root and nowhere else, and that is exactly
# how it failed: scripts/coreet_equiv7.sh runs from wherever the operator
# invoked it while the netlist lives under generated/, so verilator searched
# only its own directory and reported "Cannot find include file".
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WARE_RTL = os.path.join(REPO_ROOT, 'ware', 'rtl')

INCLUDE_RE = re.compile(r'^\s*`include\s+"([^"]+)"')

PORT_RE = re.compile(
    r'^\s*[,(]?\s*(input|output)\s+(?:reg\s+|wire\s+|logic\s+)?(?:signed\s+)?'
    r'(?:\[\s*(\d+)\s*:\s*(\d+)\s*\]\s*)?([A-Za-z_]\w*)\s*$')
NARROW = 6  # a port this wide or narrower is enumerated exhaustively


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b''):
            h.update(chunk)
    return h.hexdigest()[:16]


def support_hashes(netlist):
    """{included name: sha} for every `include a netlist names, resolved in
    ware/rtl.

    Recorded because hashing the emitted file alone does NOT identify what was
    simulated: the memory model that implements its storage lives in a separate
    tracked file, and two runs over the same netlist with different ware/rtl
    would otherwise carry identical provenance."""
    out = {}
    try:
        with open(netlist) as fh:
            for line in fh:
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                p = os.path.join(WARE_RTL, m.group(1))
                out[m.group(1)] = sha(p) if os.path.exists(p) else 'MISSING'
    except OSError as e:
        return {'<unreadable>': str(e)}
    return out


def parse_ports(path):
    ports, started = [], False
    with open(path) as fh:
        for line in fh:
            if not started:
                if re.match(r'^\s*module\b', line):
                    started = True
                continue
            if re.match(r'^\s*\);', line):
                break
            m = PORT_RE.match(line.rstrip())
            if m:
                d, hi, lo, n = m.groups()
                ports.append((d, 1 if hi is None else int(hi) - int(lo) + 1, n))
    if not ports:
        sys.exit(f"FATAL: parsed no ports from {path}")
    return ports


def directed_vectors(fields, in_bits):
    """fields: [(name, width, shift)] low-to-high, as packed into the vector."""
    out, seen = [], set()

    def add(v):
        v &= (1 << in_bits) - 1
        if v not in seen:
            seen.add(v)
            out.append(v)

    add(0)
    add((1 << in_bits) - 1)
    for b in range(in_bits):          # one-hot and one-cold over the whole word
        add(1 << b)
        add(((1 << in_bits) - 1) ^ (1 << b))
    narrow = [(n, w, s) for n, w, s in fields if w <= NARROW]
    for _, w, s in narrow:            # every value of each narrow port, both backgrounds
        for v in range(1 << w):
            add(v << s)
            add(((1 << in_bits) - 1) & ~(((1 << w) - 1) << s) | (v << s))
    # Pairwise over the two narrowest ports: a selector guarding another
    # selector is the shape a single-port sweep cannot reach.
    narrow.sort(key=lambda t: t[1])
    for (n1, w1, s1), (n2, w2, s2) in itertools.islice(itertools.combinations(narrow, 2), 8):
        if (1 << (w1 + w2)) > 4096:
            continue
        for v1 in range(1 << w1):
            for v2 in range(1 << w2):
                add((v1 << s1) | (v2 << s2))
    return out


def build_tb(top, ports, nvec, vecfile):
    ins = [(w, n) for d, w, n in ports if d == 'input']
    outs = [(w, n) for d, w, n in ports if d == 'output']
    if not outs:
        sys.exit("FATAL: module has no outputs; nothing to compare")
    clocks = [n for _, n in ins if re.search(r'(^|_)(clk|clock)(_|$)', n)]
    data_ins = [(w, n) for w, n in ins if n not in clocks]
    in_bits = sum(w for w, _ in data_ins)

    decl = "".join(f"  logic {'' if w == 1 else f'[{w-1}:0] '}{n};\n" for _, w, n in ports)
    conn = ", ".join(f".{n}({n})" for _, _, n in ports)
    unpack, bit = [], in_bits
    for w, n in data_ins:
        bit -= w
        unpack.append(f"    {n} = vec[{bit + w - 1}:{bit}];" if w > 1 else f"    {n} = vec[{bit}];")
    cat = "{" + ", ".join(n for _, n in outs) + "}"
    ow = (sum(w for w, _ in outs) + 3) // 4

    if clocks:
        clkdrive = "\n".join(f"  initial {c} = 1'b0;\n  always #5 {c} = ~{c};" for c in clocks)
        sample = f"      @(negedge {clocks[0]});"
    else:
        clkdrive = ""
        sample = "      #1;"

    return f"""// GENERATED by scripts/coreet_lowering_diffsim.py -- do not edit.
// Drives identical DEFINED input vectors into `{top}` and prints the packed
// outputs, one line per vector. Run once per implementation; the two stdouts
// must match line for line, and each must have exactly {nvec} lines.
module tb;
{decl}  logic [{max(in_bits,1)-1}:0] vec;
  integer f, code, i;
{clkdrive}
  {top} dut({conn});
  initial begin
    f = $fopen("{vecfile}", "r");
    if (f == 0) begin $display("TB-FATAL: cannot open vectors"); $fatal(1); end
    for (i = 0; i < {nvec}; i = i + 1) begin
      code = $fscanf(f, "%h\\n", vec);
      if (code != 1) begin $display("TB-FATAL: short vector file at %0d", i); $fatal(1); end
{chr(10).join(unpack)}
{sample}
      $display("%0{ow}h", {cat});
    end
    $finish;
  end
endmodule
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--top', required=True)
    ap.add_argument('--impl', required=True)
    ap.add_argument('--filelist', help='CORE-ET .f for the original RTL (reference)')
    ap.add_argument('--ref-netlist', help='compare against another netlist instead of the RTL')
    ap.add_argument('--out', required=True)
    ap.add_argument('--vectors', type=int, default=2000, help='RANDOM vectors, added after the directed set')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    if not a.filelist and not a.ref_netlist:
        sys.exit("FATAL: one of --filelist or --ref-netlist is required")
    os.makedirs(a.out, exist_ok=True)

    ports = parse_ports(a.impl)
    ins = [(w, n) for d, w, n in ports if d == 'input']
    clocks = [n for _, n in ins if re.search(r'(^|_)(clk|clock)(_|$)', n)]
    data_ins = [(w, n) for w, n in ins if n not in clocks]
    in_bits = sum(w for w, _ in data_ins)
    fields, bit = [], in_bits
    for w, n in data_ins:
        bit -= w
        fields.append((n, w, bit))

    directed = directed_vectors(fields, in_bits)
    rng = random.Random(a.seed)
    vecs = directed + [rng.getrandbits(in_bits) for _ in range(a.vectors)]
    nvec = len(vecs)
    digits = (in_bits + 3) // 4
    vecfile = os.path.join(a.out, 'vectors.hex')
    with open(vecfile, 'w') as fh:
        for v in vecs:
            fh.write(f"{v:0{digits}x}\n")
    print(f"{a.top}: {len(ports)} ports, {in_bits} driven input bits, "
          f"{len(clocks)} clock(s){' ' + ','.join(clocks) if clocks else ''}")
    print(f"  vectors: {len(directed)} directed + {a.vectors} random (seed {a.seed}) = {nvec}")

    tb = os.path.join(a.out, 'tb.sv')
    with open(tb, 'w') as fh:
        fh.write(build_tb(a.top, ports, nvec, 'vectors.hex'))

    srcs, incs = [], []
    if a.filelist:
        with open(a.filelist) as fh:
            for line in fh:
                line = line.strip()
                if line.startswith('+incdir+'):
                    incs.append('-I' + line[len('+incdir+'):])
                elif line and not line.startswith('//'):
                    srcs.append(line)

    # ware/rtl goes to every EMITTED-netlist side -- always the impl, and the
    # reference too when it is a netlist (--ref-netlist).  The RTL reference is
    # the original sources, which include nothing from cgen, so it keeps
    # exactly the +incdir+ set its own filelist declares and nothing else.
    netlist_inc = ['-I' + WARE_RTL]
    ref_sources = [os.path.abspath(a.ref_netlist)] if a.ref_netlist else srcs
    ref_extra = netlist_inc if a.ref_netlist else incs
    sides = (('ref', ref_sources, ref_extra),
             ('impl', [os.path.abspath(a.impl)], netlist_inc))

    meta = {
        'top': a.top, 'seed': a.seed, 'random_vectors': a.vectors,
        'directed_vectors': len(directed), 'total_vectors': nvec,
        'input_bits': in_bits, 'clocks': clocks,
        'impl_sha256_16': sha(a.impl),
        'reference_kind': 'netlist' if a.ref_netlist else 'rtl-via-verilator',
        'reference_sha256_16': (sha(a.ref_netlist) if a.ref_netlist
                                else hashlib.sha256(
                                    ''.join(f"{s}:{sha(s)}" for s in sorted(srcs)
                                            if os.path.exists(s)).encode()).hexdigest()[:16]),
        'verilator': subprocess.run(['verilator', '--version'], capture_output=True,
                                    text=True).stdout.strip(),
        'ware_rtl': WARE_RTL,
        'impl_support_sha256_16': support_hashes(a.impl),
        'reference_support_sha256_16': (support_hashes(a.ref_netlist)
                                        if a.ref_netlist else {}),
    }

    results = {}
    for tag, sources, extra in sides:
        d = os.path.join(a.out, tag)
        os.makedirs(d, exist_ok=True)
        # --x-assign/--x-initial 0: these designs have no reset, so uninitialised
        # state would otherwise be filled by a per-build randomisation and the
        # two sides would diverge for a reason unrelated to the lowering.
        cmd = (['verilator', '--binary', '--timing', '-j', '4', '-Wno-fatal',
                '--x-assign', '0', '--x-initial', '0',
                '--top-module', 'tb', '-o', 'sim', '--Mdir', d, os.path.abspath(tb)]
               + extra + sources)
        with open(os.path.join(a.out, f'{tag}_build.log'), 'w') as fh:
            brc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT).returncode
        if brc != 0:
            print(f"  {tag}: BUILD FAILED rc={brc} (see {a.out}/{tag}_build.log)")
            results[tag] = None
            continue
        binary = os.path.abspath(os.path.join(d, 'sim'))
        if not os.path.exists(binary):
            print(f"  {tag}: build reported success but produced no binary")
            results[tag] = None
            continue
        outf = os.path.join(a.out, f'{tag}.out')
        with open(outf, 'w') as fh:
            rc = subprocess.run([binary], stdout=fh, stderr=subprocess.STDOUT,
                                cwd=a.out).returncode
        # A NONZERO simulation exit must not be read as a result. The testbench
        # $fatal()s on a short vector file, and a side that aborted partway
        # would otherwise "agree" on the prefix it managed to print.
        if rc != 0:
            print(f"  {tag}: SIMULATION FAILED rc={rc} (see {outf})")
            results[tag] = None
            continue
        results[tag] = outf
        print(f"  {tag}: ok, rc=0")

    verdict, detail = None, {}
    if not results.get('ref') or not results.get('impl'):
        verdict = 'NOT-MEASURED (a side failed to build or run)'
    else:
        ref = [l for l in open(results['ref']) if re.fullmatch(r'[0-9a-fA-F]+\n', l)]
        imp = [l for l in open(results['impl']) if re.fullmatch(r'[0-9a-fA-F]+\n', l)]
        # EXACTLY nvec on each side. Equal-but-short counts mean both sides
        # aborted after the same prefix, which is not agreement.
        if len(ref) != nvec or len(imp) != nvec:
            verdict = (f'NOT-MEASURED (expected {nvec} output lines per side, '
                       f'got ref={len(ref)} impl={len(imp)})')
        else:
            bad = [i for i in range(nvec) if ref[i] != imp[i]]
            detail['mismatches'] = len(bad)
            detail['first_mismatches'] = [
                {'vector': i, 'ref': ref[i].strip(), 'impl': imp[i].strip()} for i in bad[:5]]
            if bad:
                print(f"  compared {nvec} vectors; {len(bad)} mismatch(es)")
                for i in bad[:5]:
                    print(f"    vec {i}: ref={ref[i].strip()} impl={imp[i].strip()}")
                verdict = f'DIFFSIM-MISMATCH seed={a.seed} vectors={nvec} mismatches={len(bad)}'
            else:
                verdict = f'DIFFSIM-PASS seed={a.seed} vectors={nvec}'

    meta['verdict'] = verdict
    meta.update(detail)
    with open(os.path.join(a.out, 'diffsim.json'), 'w') as fh:
        json.dump(meta, fh, indent=2)
    print(f"VERDICT: {verdict}")
    return 0 if verdict.startswith('DIFFSIM-PASS') else (1 if 'MISMATCH' in verdict else 2)


if __name__ == '__main__':
    sys.exit(main())
