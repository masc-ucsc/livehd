#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# v2v_verilator_diff — simulate the ORIGINAL Verilog and LiveHD's REGENERATED
# Verilog side by side under Verilator and diff their output traces.
#
# WHY. `lhd lec` proves the two equal, but on a design the size of minion it is
# slow, per-module, and answers UNKNOWN often enough that "no refutation" is not
# the same as "no bug". This is the cheap complement: identical stimulus into two
# models, one line of outputs per cycle, `diff`. It cannot prove equivalence — it
# is a bug FINDER — but when it fires it hands you a cycle number and a signal
# name instead of a solver verdict, and it runs in seconds.
#
# WHAT IT ISOLATES. The round trip is
#     original .sv --inou.slang--> lg --inou.cgen.verilog--> generated .v
# and NOTHING in that path touches inou/prp. So a mismatch here is a reader or
# writer bug, and a MATCH here says the compile core is faithful and any
# remaining difference lives in the Pyrope leg (`--via-pyrope` runs that longer
# path — original -> lg -> Pyrope -> lg -> Verilog — so the two runs together
# bracket exactly which leg is at fault).
#
# HOW THE DRIVER IS BUILT. Ports are parsed from the GENERATED Verilog, which is
# flat and regular (`input signed [7:0] d`), rather than from the original, which
# may use packed structs, parameters and interfaces. Verilator flattens a packed
# struct port to a plain vector of the same width, so one driver compiles against
# both — and if it does NOT, the port lists disagree, which is itself the finding
# (reported as an interface mismatch rather than a trace mismatch).
#
# Inputs are driven from a deterministic LFSR, so both models see bit-identical
# stimulus without needing a hand-written testbench per design. Clock and reset
# are recognized by name and driven as a clock and a reset instead.
#
# Usage:
#   scripts/v2v_verilator_diff.py --top minion_top \
#       --orig-flags "-F /path/minion/verilog/filelist.f -DSYNTHESIS" \
#       --gen out.v --cycles 200
#
#   scripts/v2v_verilator_diff.py --top adder --orig adder.v --gen gen.v

import argparse
import os
import re
import shutil
import subprocess
import sys

# Names driven as a CLOCK rather than as data. Everything else that looks like a
# clock but is not the one we toggle stays at 0, which is the honest default: a
# second clock domain is not something a single-clock differential can drive.
CLOCK_NAMES = ('clk', 'clock', 'clk_i', 'clock_i')
# Reset names, and whether they are ACTIVE LOW.
RESET_RE = re.compile(r'(^|_)(rst|reset)(_|$)|^rst|^reset')
# ACTIVE-LOW spellings. A literal `_n`/`_ni` test alone missed the most common
# forms (`rstn`, `resetn`, `rst_n_i`, `rst_b`, `nrst`), and getting the polarity
# backwards holds the DUT IN reset for the whole run: both models then emit the
# same reset-constant line every cycle, the trace diff finds nothing, and the
# tool prints MATCH on a design it never actually exercised.
_LEADING_N_RE = re.compile(r'(?:^|_)n(?:rst|reset)(?:_|$)')  # nrst, n_reset
_TRAILING_N_RE = re.compile(r'(?:rst|reset)n$')              # rstn, resetn, aresetn


def reset_is_active_low(name):
    """True when `name` spells an active-LOW reset."""
    n = name.lower()
    while True:  # drop DIRECTION suffixes first, so `rst_n_i` reads like `rst_n`
        for suf in ('_i', '_o', '_in', '_out'):
            if n.endswith(suf) and len(n) > len(suf):
                n = n[: -len(suf)]
                break
        else:
            break
    return bool(_LEADING_N_RE.search(n)) or n.endswith(('_n', '_ni', '_b', '_l')) or bool(_TRAILING_N_RE.search(n))


def parse_ports(vpath, top):
    """[(dir, name, width, signed)] for `module <top>(...)` in `vpath`.

    Deliberately a small regex parser over LiveHD's own emitter output, whose
    port syntax is one declaration per line in a known shape. It is NOT a
    Verilog parser and must never be pointed at arbitrary source.
    """
    with open(vpath) as f:
        text = f.read()
    m = re.search(r'\bmodule\s+\\?' + re.escape(top) + r'\b(.*?);', text, re.S)
    if not m:
        return None
    body = m.group(1)
    ports = []
    for line in body.splitlines():
        line = line.strip().lstrip(',').strip()
        pm = re.match(r'(input|output)\s+(reg\s+|wire\s+)?(signed\s+)?(\[(\d+):(\d+)\]\s*)?(\\?[A-Za-z_][\w$.]*)', line)
        if not pm:
            continue
        direction = pm.group(1)
        signed = pm.group(3) is not None
        width = 1
        if pm.group(4):
            width = int(pm.group(5)) - int(pm.group(6)) + 1
        name = pm.group(7).lstrip('\\')
        ports.append((direction, name, width, signed))
    return ports


def cpp_member_write(name, width, expr_fn):
    """Poke a port whose C++ representation depends on its width.

    Verilator maps <=32 bits to a scalar, 33..64 to a 64-bit scalar, and wider to
    a VlWide word array. Getting this wrong is a compile error rather than a
    silent bug, which is why it is written out explicitly per width class.
    """
    if width <= 64:
        return '  dut->{} = {};\n'.format(name, expr_fn())
    words = (width + 31) // 32
    out = ''
    for w in range(words):
        out += '  dut->{}[{}] = (uint32_t)({});\n'.format(name, w, expr_fn())
    return out


def cpp_member_read(name, width):
    """Print a port's value as hex, widest word first, so two runs diff cleanly."""
    if width <= 64:
        return '  std::printf(" {}=%llx", (unsigned long long)dut->{});\n'.format(name, name)
    words = (width + 31) // 32
    out = '  std::printf(" {}=", );\n'.replace(', ', '')  # placeholder replaced below
    out = '  std::printf(" {}=");\n'.format(name)
    for w in range(words - 1, -1, -1):
        out += '  std::printf("%08x", (unsigned)dut->{}[{}]);\n'.format(name, w)
    return out


def gen_driver(ports, top, cycles, reset_cycles):
    clocks = [p for p in ports if p[0] == 'input' and p[1] in CLOCK_NAMES]
    if not clocks:
        # No conventionally named clock: fall back to the first 1-bit input, and
        # SAY SO — a differential that silently never toggled a clock would
        # "match" on two dead models.
        cand = [p for p in ports if p[0] == 'input' and p[2] == 1]
        clocks = cand[:1]
    clock_names = [p[1] for p in clocks]
    resets = [p for p in ports if p[0] == 'input' and p[1] not in clock_names and RESET_RE.search(p[1])]
    reset_names = [(p[1], reset_is_active_low(p[1])) for p in resets]
    data_in = [p for p in ports if p[0] == 'input' and p[1] not in clock_names
               and p[1] not in [r[0] for r in reset_names]]
    outs = [p for p in ports if p[0] == 'output']

    src = ['// GENERATED by scripts/v2v_verilator_diff.py -- do not edit.',
           '// Identical stimulus into two models; one line of outputs per cycle.',
           '#include <cstdint>', '#include <cstdio>',
           '#include "V{}.h"'.format(top), '#include "verilated.h"', '',
           '// xorshift64*: deterministic, identical in both runs, and cheap.',
           'static uint64_t s = 0x9E3779B97F4A7C15ull;',
           'static uint64_t rnd() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }',
           '',
           'int main(int argc, char** argv) {',
           '  Verilated::commandArgs(argc, argv);',
           '  V{}* dut = new V{};'.format(top, top),
           '  const long cycles = {};'.format(cycles),
           '  const long reset_cycles = {};'.format(reset_cycles), '']
    for n in clock_names:
        src.append('  dut->{} = 0;'.format(n))
    src.append('')
    src.append('  for (long c = 0; c < cycles; ++c) {')
    src.append('    const bool in_reset = (c < reset_cycles);')
    for n, active_low in reset_names:
        src.append('    dut->{} = in_reset ? {} : {};'.format(n, 0 if active_low else 1, 1 if active_low else 0))
    src.append('    // stimulus: identical in both models by construction')
    for _d, n, w, _s in data_in:
        src.append(cpp_member_write(n, w, lambda: 'rnd()').rstrip('\n').lstrip())
    src.append('    dut->eval();')
    for n in clock_names:
        src.append('    dut->{} = 1;'.format(n))
    src.append('    dut->eval();   // the RISE')
    for n in clock_names:
        src.append('    dut->{} = 0;'.format(n))
    src.append('    dut->eval();   // the FALL')
    src.append('    std::printf("c%ld", c);')
    for _d, n, w, _s in outs:
        src.append(cpp_member_read(n, w).rstrip('\n').lstrip())
    src.append('    std::printf("\\n");')
    src.append('  }')
    src.append('  dut->final();')
    src.append('  delete dut;')
    src.append('  return 0;')
    src.append('}')
    return '\n'.join(src) + '\n', clock_names, [r[0] for r in reset_names], len(data_in), len(outs)


def verilate(vbin, top, sources, extra, driver, mdir, log):
    cmd = [vbin, '--cc', '--exe', '--build', '-Wno-fatal', '-O1', '--Mdir', mdir,
           '--top-module', top, '-CFLAGS', '-std=c++17 -O1'] + extra + sources + [driver]
    with open(log, 'w') as f:
        rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT).returncode
    return rc, cmd


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--top', required=True)
    ap.add_argument('--orig', action='append', default=[], help='original source file (repeatable)')
    ap.add_argument('--orig-flags', default='', help='extra verilator flags for the ORIGINAL (e.g. "-F filelist.f -DSYNTHESIS")')
    ap.add_argument('--gen', required=True, help='LiveHD-regenerated Verilog')
    ap.add_argument('--cycles', type=int, default=200)
    ap.add_argument('--reset-cycles', type=int, default=4)
    ap.add_argument('--work', default='/tmp/v2v_diff')
    ap.add_argument('--gen-incdir', action='append', default=[],
                    help="include dir for the GENERATED side; LiveHD's emission `\u0060include`s its own "
                         "memory library, so this normally needs <livehd>/ware/rtl (repeatable)")
    args = ap.parse_args()

    vbin = os.environ.get('VERILATOR') or shutil.which('verilator')
    if not vbin:
        print('SKIP: verilator not installed (brew/apt install verilator, or export VERILATOR=<path>)')
        return 0

    if not args.gen_incdir:
        default_rtl = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'ware', 'rtl')
        if os.path.isdir(default_rtl):
            args.gen_incdir = [default_rtl]

    os.makedirs(args.work, exist_ok=True)
    ports = parse_ports(args.gen, args.top)
    if not ports:
        print('FAIL: no `module {}` found in {}'.format(args.top, args.gen))
        return 2

    # A STRUCT-LEAF port (`\bus.field`) means the two sides do not share an
    # interface: LiveHD flattens a packed struct port into one port per leaf,
    # while Verilator packs the original's struct into a single wide vector. One
    # driver cannot poke both without a design-specific pack/unpack layer, so say
    # so instead of emitting C++ that will not compile.
    dotted = [p[1] for p in ports if '.' in p[1]]
    if dotted:
        print('SKIP: `{}` has {} packed-struct leaf port(s) (e.g. {}) — LiveHD flattens these '
              'and Verilator does not, so the two models have different interfaces. Run the '
              'differential on the flat-port modules below it instead.'
              .format(args.top, len(dotted), dotted[0]))
        return 77

    driver_src, clocks, resets, n_in, n_out = gen_driver(ports, args.top, args.cycles, args.reset_cycles)
    if not outs_ok(n_out):
        print('FAIL: `{}` declares no outputs — nothing to compare'.format(args.top))
        return 2
    driver = os.path.join(args.work, 'v2v_driver.cpp')
    with open(driver, 'w') as f:
        f.write(driver_src)
    print('driver: {} port(s) -> clock={} reset={} data-in={} out={}'.format(
        len(ports), clocks, resets, n_in, n_out))

    runs = {}
    sigs = {}
    for tag, sources, extra in (
            ('orig', list(args.orig), args.orig_flags.split()),
            ('gen', [os.path.abspath(args.gen)], ['-I' + os.path.abspath(d) for d in args.gen_incdir])):
        if not sources and not extra:
            print('FAIL: nothing to verilate for `{}` (pass --orig and/or --orig-flags)'.format(tag))
            return 2
        mdir = os.path.join(args.work, 'obj_' + tag)
        log = os.path.join(args.work, tag + '.verilate.log')
        rc, cmd = verilate(vbin, args.top, sources, extra, driver, mdir, log)
        if rc != 0:
            # DISTINCT exit codes, because "verilator would not build this" and
            # "the two models computed different values" are completely
            # different results and reporting a build failure as a MISMATCH is
            # how a differential manufactures false alarms. Measured: the first
            # version of this script reported 17 MISMATCHes on minion's prim_*
            # modules, every one of them a build failure.
            #   78 = the GENERATED model would not build. Usually a submodule
            #        LiveHD instantiates but did not emit (its own
            #        ware/rtl/cgen_memory_*.v library).
            #   79 = the ORIGINAL would not build STANDALONE. Usually a
            #        PARAMETERIZED module: LiveHD emits it post-elaboration with
            #        resolved widths, while the original at its default
            #        parameters has different ports. Not comparable this way; the
            #        comparison has to happen at an elaborated instance.
            code = 78 if tag == 'gen' else 79
            why = ('the GENERATED model' if tag == 'gen' else
                   'the ORIGINAL standalone (parameterized module? LiveHD emits post-elaboration)')
            print('SKIP: verilator would not build {} (rc={})'.format(why, rc))
            with open(log) as f:
                tail = [l for l in f.read().splitlines() if l.startswith('%Error')][:3]
            for l in tail:
                print('  ' + l[:200])
            return code
        # PORT-WIDTH GUARD. Verilator's generated header declares every port with
        # its width (`VL_IN8(&name,7,0)`). Comparing them is the only cheap way to
        # know the two models share an interface: LiveHD emits a module
        # POST-ELABORATION, so a PARAMETERIZED original built at its DEFAULT
        # parameters can have different port widths, and diffing traces across
        # that is comparing two different circuits. Reporting it as a value
        # mismatch would be a false alarm of exactly the kind this guard exists
        # to prevent.
        hdr = os.path.join(mdir, 'V' + args.top + '.h')
        sigs[tag] = sorted(re.findall(r'\bVL_(?:IN|OUT|INOUT)\w*\(&?(\w+)\s*,\s*(\d+)\s*,\s*(\d+)', 
                                      open(hdr).read())) if os.path.exists(hdr) else []

        exe = os.path.join(mdir, 'V' + args.top)
        out = os.path.join(args.work, tag + '.trace')
        with open(out, 'w') as f:
            subprocess.run([exe], stdout=f, stderr=subprocess.STDOUT)
        runs[tag] = out
        unopt = 'UNOPTFLAT' in open(log).read()
        print('{}: {} cycles traced{}'.format(tag, args.cycles, '  (UNOPTFLAT: verilator sees a settle loop)' if unopt else ''))

    if sigs.get('orig') and sigs.get('gen') and sigs['orig'] != sigs['gen']:
        only_o = [x for x in sigs['orig'] if x not in sigs['gen']]
        only_g = [x for x in sigs['gen'] if x not in sigs['orig']]
        print('SKIP: the two models do not share an interface — '
              '{} port(s) only in the original, {} only in the regenerated. '
              'LiveHD emits POST-ELABORATION, so a parameterized module built standalone at its '
              'default parameters is a different circuit; compare it at an elaborated instance instead.'
              .format(len(only_o), len(only_g)))
        for x in (only_o[:3] + only_g[:3]):
            print('  {} [{}:{}]'.format(*x))
        return 80

    with open(runs['orig']) as f:
        a = f.read().splitlines()
    with open(runs['gen']) as f:
        b = f.read().splitlines()
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            print('\nMISMATCH at cycle {}:'.format(i))
            print('  orig: {}'.format(x[:400]))
            print('  gen : {}'.format(y[:400]))
            for tok_a, tok_b in zip(x.split(), y.split()):
                if tok_a != tok_b:
                    print('  first differing signal: {}  vs  {}'.format(tok_a, tok_b))
                    break
            print('\ntraces: {}  {}'.format(runs['orig'], runs['gen']))
            return 1
    if len(a) != len(b):
        print('\nMISMATCH: trace lengths differ ({} vs {})'.format(len(a), len(b)))
        return 1
    print('\nMATCH: {} cycles, original and regenerated Verilog agree on every output'.format(len(a)))
    return 0


def outs_ok(n):
    return n > 0


if __name__ == '__main__':
    sys.exit(main())
