#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# gen_equiv_tb — differential Verilog<->Pyrope simulation testbench generator.
#
# For each equiv pair <name>.v (golden) + <name>.prp (DUT under test) this emits
# two testbenches that drive the SAME constrained-random stimulus for 1000 cycles
# and fold every cycle's outputs into one 62-bit rolling signature:
#
#   <name>_tb.v    an Icarus-Verilog testbench instantiating the golden module
#   <name>_tb.prp  an `lhd sim` `:type: simulation` test that imports the DUT
#                  (`pub` lambda) and drives it via the instance/step model
#
# Both sides run a bit-identical 64-bit LCG (`s = s*A + C`, mod 2^64) to make the
# stimulus, and a bit-identical rolling hash (`sig = sig*P + out`). Only `+ * &`
# are used so the arithmetic matches between Verilog `reg [63:0]` (unsigned) and
# the driver's `long` (two's-complement) exactly -- `>>` is avoided (arith vs
# logical shift differ); each input field gets its own LCG draw and is masked to
# its width, and the exposed signature is masked to 62 bits so it is a clean
# non-negative decimal on both sides.
#
# TWO PHASES (see README):
#   setup (--setup, needs iverilog): (re)emit both _tb files, run the Verilog
#          golden, and bake the resulting signature into the _tb.prp assert.
#          Only needed when a pair is added or its logic/interface changes.
#   run   (the regression): `lhd sim <name>_tb.prp` recompiles the DUT to C++
#          and checks the baked golden. Owned by prp-simeq-<name> bazel targets.

import argparse
import glob
import os
import re
import subprocess
import sys

import pubify_equiv

# ---- shared stimulus/hash constants (MUST stay identical in .v and .prp) ------
A     = 6364136223846793005          # PCG/Knuth LCG multiplier (< 2^63)
C     = 1442695040888963407          # LCG increment (< 2^63)
P     = 1099511628211                # rolling-hash multiplier (FNV prime)
SEED  = 0x1234567890ABCDEF           # LCG seed (< 2^63)
MASK  = 0x3FFFFFFFFFFFFFFF           # expose only 62 bits -> non-negative both sides
NCYC  = 1000                         # total simulated cycles
NRST  = 4                            # reset-asserted + hash-skip window (seq w/ reset)
FILL  = 32                           # hash-skip window for a seq DUT WITHOUT a reset
                                     # port (let the pipe flush its power-on state,
                                     # X in Verilog / init in the driver, so the two
                                     # agree on pure f(inputs) from cycle FILL on)

CLK_NAMES = ("clock",)               # LiveHD cgen clock port name
RST_NAMES = ("reset",)               # LiveHD cgen implicit-reset port name


class GenError(Exception):
    pass


# ---- Verilog module-header parsing ------------------------------------------
def _strip_comments(src):
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    return "\n".join(re.sub(r'//.*', '', ln) for ln in src.splitlines())


def _width_of(decl):
    # sum of packed dimensions: [hi:lo] -> |hi-lo|+1 ; [a:b][c:d] -> product.
    w = 1
    dims = re.findall(r'\[\s*([0-9]+)\s*:\s*([0-9]+)\s*\]', decl)
    if not dims:
        # a `[NAME:0]` parameterised width or none -> treat as scalar unless it
        # clearly has a bracket we could not parse (then it is unsupported).
        if '[' in decl:
            raise GenError("non-numeric port width: " + decl.strip())
        return 1
    w = 1
    for hi, lo in dims:
        w *= abs(int(hi) - int(lo)) + 1
    return w


def _prp_header(ppath, key):
    pat = re.compile(r"^\s*:%s:\s*(.+)" % re.escape(key))
    try:
        for line in open(ppath):
            m = pat.match(line)
            if m:
                return m.group(1).strip()
    except OSError:
        pass
    return None


def parse_verilog_ports(vpath, want=None):
    src = _strip_comments(open(vpath).read())
    if want:
        # Pick the NAMED module (a multi-module golden lists sub-modules first, so
        # the top is not module #1); the .v id may be `\`-escaped.
        m = re.search(r'\bmodule\b\s+(\\?%s)\s*(#\s*\([^)]*\)\s*)?\(' % re.escape(want.lstrip('\\')), src)
    else:
        m = re.search(r'\bmodule\b\s+(\\\S+|\w+)\s*(#\s*\([^)]*\)\s*)?\(', src)
    if not m:
        raise GenError("no module declaration found" + ((" for " + want) if want else ""))
    modname = m.group(1)
    # capture the ANSI port list: from the '(' after the module name to its match
    i = m.end() - 1
    depth = 0
    j = i
    while j < len(src):
        if src[j] == '(':
            depth += 1
        elif src[j] == ')':
            depth -= 1
            if depth == 0:
                break
        j += 1
    portblob = src[i + 1:j]
    if '{' in portblob or 'struct' in portblob:
        raise GenError("struct/packed port list not supported")
    ports = []                                   # (dir, width, name) in order
    last_dir = None
    for chunk in portblob.split(','):
        c = chunk.strip()
        if not c:
            continue
        mdir = re.match(r'(input|output|inout)\b', c)
        if mdir:
            last_dir = mdir.group(1)
            c = c[mdir.end():].strip()
        d = last_dir
        if d is None:
            raise GenError("non-ANSI port list (direction not inline): " + c)
        # drop net/var/sign keywords, keep the width bracket + trailing name
        c = re.sub(r'\b(wire|reg|logic|signed|unsigned|var)\b', ' ', c).strip()
        wm = re.match(r'((?:\[[^\]]*\]\s*)+)?\s*(\w+)\s*$', c)
        if not wm:
            raise GenError("unparseable port decl: " + chunk.strip())
        width = _width_of(wm.group(1) or '')
        name = wm.group(2)
        ports.append((d, width, name))
    return modname, ports


def classify(ports):
    clk = rst = None
    data_in = []
    outs = []
    for d, w, n in ports:
        if d == 'input' and n in CLK_NAMES and clk is None:
            clk = n
        elif d == 'input' and n in RST_NAMES and rst is None:
            rst = n
        elif d == 'input':
            data_in.append((n, w))
        elif d == 'output':
            outs.append((n, w))
        else:
            raise GenError("inout port not supported: " + n)
    return clk, rst, data_in, outs


def lambda_of(modname):
    # `\bitset_reg.foo` / `bitset_reg.foo` -> `foo`
    nm = modname.lstrip('\\')
    return nm.rsplit('.', 1)[-1] if '.' in nm else nm


def pyrope_lambda_of(ppath, modname, default_lam):
    # `lg="<modname>"` renames the generated lgraph/Verilog module, but the
    # IMPORTABLE pub entry keeps its SOURCE name (e.g. `pub comb my_top::[lg=
    # "chip_top"]`). The _tb must import that source lambda, not the renamed
    # module. Recover it from the .prp; fall back to the Verilog-derived name.
    try:
        txt = open(ppath).read()
    except OSError:
        return default_lam
    m = re.search(r'pub\s+(?:comb|mod|pipe)\s+([A-Za-z_]\w*)\s*::\s*\[[^\]]*lg\s*=\s*"%s"'
                  % re.escape(modname.lstrip('\\')), txt)
    return m.group(1) if m else default_lam


# ---- emit the two testbenches -----------------------------------------------
def mask_lit_v(w):
    return "64'd" + str((1 << w) - 1) if w < 63 else "~64'd0"


def uniq(name, taken):
    while name in taken:
        name += "_"
    return name


# Pyrope keywords that derail the parser when used as a plain binding name
# (`const pipe = import(...)` reads as the start of a `pipe` lambda, not a
# declaration). The DUT's tail name routinely collides (`pub mod pipe` is a
# common test idiom); the binding gets a `_` suffix, the import STRING keeps
# the real module path.
PRP_KEYWORDS = {
    "comb", "mod", "pipe", "test", "if", "elif", "else", "for", "while",
    "match", "import", "const", "mut", "reg", "wire", "pub", "step", "tick",
    "clock", "reset", "in", "ref", "type", "true", "false", "nil", "and",
    "or", "not", "xor", "assert", "cassert", "return", "break", "continue",
    "enum",
}


def hash_start_of(clk, rst):
    if clk is None:
        return 0                      # comb: every cycle independent
    return NRST if rst else FILL       # seq: reset window, or pipe-fill window


def emit_verilog(base, modname, clk, rst, data_in, outs):
    seq = clk is not None
    hs = hash_start_of(clk, rst)
    taken = {n for n, _ in data_in} | {n for n, _ in outs} | {"clock", "reset"}
    S, SIG, K = uniq("s", taken), uniq("sig", taken), uniq("k", taken)
    # The instance name must not collide with a golden PORT name (a golden with a
    # port literally named `dut` would otherwise emit `dut dut(...)`). uniq() is a
    # no-op when there is no collision, so existing goldens regenerate identically.
    DUT = uniq("dut", taken)
    L = []
    a = L.append
    a("`timescale 1ns/1ps")
    a("// AUTO-GENERATED by gen_equiv_tb.py -- differential sim golden driver.")
    a("module %s_tb;" % cpp(base))
    a("  localparam [63:0] A=64'd%d, C=64'd%d, P=64'd%d;" % (A, C, P))
    a("  localparam [63:0] SEED=64'h%X, MASK=64'h%X;" % (SEED, MASK))
    a("  localparam integer NCYC=%d;" % NCYC)
    for n, w in data_in:
        a("  reg  [%d:0] %s;" % (w - 1, n))
    if seq:
        a("  reg clock;")
    if rst:
        a("  reg reset;")
    for n, w in outs:
        a("  wire [%d:0] %s;" % (w - 1, n))
    conns = ["." + n + "(" + n + ")" for n, _ in data_in]
    if seq:
        conns.append(".clock(clock)")
    if rst:
        conns.append(".reset(reset)")
    conns += ["." + n + "(" + n + ")" for n, _ in outs]
    a("  %s %s(%s);" % (esc(modname), DUT, ", ".join(conns)))
    a("  reg [63:0] %s, %s; integer %s;" % (S, SIG, K))
    a("  initial begin")
    a("    %s=SEED; %s=0;%s" % (S, SIG, "" if not seq else " clock=0;"))
    a("    for (%s=0;%s<NCYC;%s=%s+1) begin" % (K, K, K, K))
    for n, w in data_in:
        a("      %s=%s*A+C; %s = %s & %s;" % (S, S, n, S, mask_lit_v(w)))
    if rst:
        a("      reset = (%s<%d) ? 1'b1 : 1'b0;" % (K, NRST))
    if seq:
        # Full period (posedge then negedge) before sampling: a negedge-clocked
        # flop (posclk=false) only updates on the falling half, so sampling right
        # after the rising edge would read its STALE (previous-cycle) value. The
        # `lhd sim` `step`/peek model observes the settled end-of-cycle value (one
        # step == one full period), so the golden must match that same point.
        # Posedge-clocked outputs are unaffected: nothing else changes between the
        # two edges (inputs are held for the whole cycle), so they already read
        # their final value by the time the negedge half completes.
        a("      #1 clock=1; #1;")
        a("      #1 clock=0; #1;")
    else:
        a("      #1;")
    a("      if (%s>=%d) begin" % (K, hs))
    for n, w in outs:
        # fold the UNSIGNED low-w bits so a signed output hashes the same on both
        # sides (the driver peeks a signed value; the Verilog wire is unsigned).
        a("        %s = %s*P + (%s & %s);" % (SIG, SIG, n, mask_lit_v(w)))
    a("      end")
    a("    end")
    a('    $display("SIG %%0d", %s & MASK);' % SIG)
    a("    $finish;")
    a("  end")
    a("endmodule")
    return "\n".join(L) + "\n"


def mask_lit_p(w):
    return hex((1 << w) - 1) if w < 63 else None


def emit_pyrope(base, lam, clk, rst, data_in, outs, golden):
    seq = clk is not None
    hs = hash_start_of(clk, rst)
    bind = uniq(lam, PRP_KEYWORDS | {n for n, _ in data_in} | {n for n, _ in outs})
    taken = {n for n, _ in data_in} | {n for n, _ in outs} | {bind, "clock", "reset", "step", "tick"}
    S, SIG, ACC = uniq("s", taken), uniq("sig", taken), uniq("acc", taken)
    ind = "      " if seq else "    "        # comb hashes unconditionally (no guard)
    L = []
    a = L.append
    a("/*")
    a(":name: %s_tb" % base)
    a(":type: simulation")
    a("*/")
    a("// AUTO-GENERATED by gen_equiv_tb.py -- differential sim regression.")
    a("// Drives `%s` with the same LCG stimulus as %s_tb.v for %d cycles and" % (lam, base, NCYC))
    a("// checks the output signature against the Icarus-Verilog golden.")
    a('const %s = import("%s.%s")' % (bind, base, lam))
    a("")
    a("test %s.sig {" % base)
    a("  mut %s = %s" % (ACC, bind))
    a("  mut %s   = 0x%X" % (S, SEED))
    a("  mut %s = 0" % SIG)
    a("  tick %d {" % NCYC)
    for n, w in data_in:
        a("    %s = %s * %d + %d" % (S, S, A, C))
        ml = mask_lit_p(w)
        a("    %s.%s = %s%s" % (ACC, n, S, "" if ml is None else " & " + ml))
    if rst:
        a("    %s.reset = clock < %d" % (ACC, NRST))
    a("    step")
    if seq:
        a("    if clock >= %d {" % hs)
    for n, w in outs:
        ml = mask_lit_p(w)
        val = "%s.%s" % (ACC, n) if ml is None else "(%s.%s & %s)" % (ACC, n, ml)
        a("%s%s = %s * %d + %s" % (ind, SIG, SIG, P, val))
    if seq:
        a("    }")
    a("  }")
    a('  assert((%s & 0x%X) == %s, "signature mismatch vs iverilog golden %s")' % (SIG, MASK, golden, base))
    a("}")
    return "\n".join(L) + "\n"


def cpp(s):
    return re.sub(r'\W', '_', s)


def esc(modname):
    # a `\`-escaped Verilog id must be re-escaped with a trailing space at use.
    return (modname + " ") if modname.startswith('\\') else modname


# ---- golden capture (iverilog) ----------------------------------------------
def run_iverilog(base, vpath, tbpath, workdir):
    vvp = os.path.join(workdir, base + "_tb.vvp")
    cp = subprocess.run(["iverilog", "-g2012", "-o", vvp, vpath, tbpath],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if cp.returncode != 0:
        raise GenError("iverilog failed:\n" + cp.stdout.decode('utf-8', 'ignore'))
    cp = subprocess.run(["vvp", vvp], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = cp.stdout.decode('utf-8', 'ignore')
    m = re.search(r'^SIG\s+(\d+)\s*$', out, re.M)
    if not m:
        # X-contaminated or crashed: SIG line missing or non-numeric.
        raise GenError("no numeric SIG from vvp (X/dontcare?):\n" + out)
    return m.group(1)


# ---- per-pair driver --------------------------------------------------------
def process(base, equiv_dir, workdir, do_setup):
    vpath = os.path.join(equiv_dir, base + ".v")
    ppath = os.path.join(equiv_dir, base + ".prp")
    if not os.path.exists(vpath) or not os.path.exists(ppath):
        raise GenError("missing .v or .prp")
    # Prefer the authoritative :verilog_top: / :pyrope_top: headers (they name the
    # real top, which for a multi-module golden is NOT the first module). Fall back
    # to the first .v module + its lambda for header-less legacy pairs.
    vtop = _prp_header(ppath, "verilog_top")
    ptop = _prp_header(ppath, "pyrope_top")
    modname, ports = parse_verilog_ports(vpath, want=vtop)
    # :pyrope_top: may name the lg="..." RENAMED module (what the lec harnesses
    # need); the IMPORTABLE pub entry keeps its source name. Accept the header
    # tail only when it really is a pub entry; otherwise recover the source
    # lambda from the lg= attribute (e.g. pyrope_top sim_sub_stateful_feedback_top
    # -> pub mod `top`).
    lam = None
    if ptop:
        cand = ptop.rsplit(".", 1)[-1]
        try:
            _ptxt = open(ppath).read()
        except OSError:
            _ptxt = ""
        if re.search(r'pub\s+(?:comb|mod|pipe)\s+%s\b' % re.escape(cand), _ptxt):
            lam = cand
        else:
            lam = pyrope_lambda_of(ppath, cand, None)
    lam = lam or pyrope_lambda_of(ppath, modname, lambda_of(modname))
    clk, rst, data_in, outs = classify(ports)
    if not outs:
        raise GenError("no output ports to hash")
    if not data_in:
        raise GenError("no data input ports to drive")

    tbv = os.path.join(equiv_dir, base + "_tb.v")
    tbp = os.path.join(equiv_dir, base + "_tb.prp")

    if do_setup:
        open(tbv, "w").write(emit_verilog(base, modname, clk, rst, data_in, outs))
        golden = run_iverilog(base, vpath, tbv, workdir)
        open(tbp, "w").write(emit_pyrope(base, lam, clk, rst, data_in, outs, golden))
        pubify_equiv.pubify(ppath)          # the _tb.prp imports the DUT -> it must be `pub`
        return ("setup-ok", golden)
    else:
        if not os.path.exists(tbp):
            raise GenError("no _tb.prp (run --setup first)")
        return ("exists", None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--equiv-dir", default=os.path.dirname(os.path.abspath(__file__)) + "/equiv")
    ap.add_argument("--workdir", default="/tmp/gen_equiv_tb")
    ap.add_argument("--name", help="single base name (no extension); default: all")
    ap.add_argument("--setup", action="store_true", help="emit _tb files + capture golden (needs iverilog)")
    args = ap.parse_args()
    os.makedirs(args.workdir, exist_ok=True)

    if args.name:
        bases = [args.name]
    else:
        bases = sorted(os.path.basename(f)[:-2] for f in glob.glob(args.equiv_dir + "/*.v"))
        bases = [b for b in bases if not b.endswith("_tb")]

    ok = 0
    fail = []
    for base in bases:
        try:
            status, golden = process(base, args.equiv_dir, args.workdir, args.setup)
            ok += 1
            print("OK   %-28s %s %s" % (base, status, golden or ""))
        except GenError as e:
            fail.append((base, str(e).splitlines()[0]))
            print("GATE %-28s %s" % (base, str(e).splitlines()[0]))
    print("\n%d ok, %d gated (of %d)" % (ok, len(fail), len(bases)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
