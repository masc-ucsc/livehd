#!/usr/bin/env python3
"""mk_product.py -- the product model for a bounded search from reset (phase 2 of the judge).

  mk_product.py <file.refmap> <d0.v> <d1.v> <out.sv> [<d0 top> <d1 top>]   (default: uc_b03 uc_b03)

<d0.v> and <d1.v> are the two designs' bound netlists (one flat module each, with its observers,
as the certificates were exported from). The product `prod` holds both:
- every implementation (D1) input is a product input of the same name; D1 gets all of them;
- D0 gets, for the channel's `resp:` inputs, the slot's `resp:` words (in order), every other D0
  input 0, and its clock: `clk_i` when `maxstall` is 0, otherwise `clk_i` gated by the slot
  (D0 steps exactly when the slot fires: the .refmap's own stall choice), through a clock gate
  whose enable latch is open while the clock is low (the glitch-free gate single_edge retimes);
- output `ok`: in a step where the slot fires, every observed pair is equal (the slot's word, a
  slice of a D1 port read as 0 unless its guard bit is set, against the channel's D0 output); in a
  step where it does not, no `xfer` formula holds; and no more than `maxstall` periods in a row
  without a step;
- output `env_ok`: D1's `asm` output, and, with `period 2`, the real clock's input timing: in a
  step of phase 0 (a rising-edge step) after the first period, every input equals its value of the
  step before (inputs change only between a rising-edge step and the falling-edge step after it).

A guard on a 1-bit port (`?out:v.0`) reads the port itself (a bit-select of a 1-bit wire is not
legal Verilog).

`out:<name>@next` (D1's output one step later, in the slot's `<bool>`, `resp:`, `obs:` words and
guards, or an `xfer`) makes the product check judge step n at cycle n+1, with D0 one step behind
D1: `in:<name>` and `out:<name>` read registers that hold the value of the cycle before,
`out:<name>@next` reads D1's output now, D0's clock is gated by the slot from cycle 1 on (D0 does
not step in cycle 0), and `ok` holds in cycle 0. `env_ok` stays D1's `asm` now, so a
counterexample keeps `asm` on every row. A counterexample at cycle m is then a judge NO MATCH at
step m-1 (its TSV has rows 0..m: the judge reads row m for the `@next` words). Not with
`period 2`: one step there is half a clock; use delay registers in D0's observers instead.

`sym_verify prod.dcert ok asm:env_ok base k:<K>` then searches K steps from reset; its
counterexample TSV is a run for `sym_refmap judge ... trace:`. The product decides nothing: with
the slot's stall choice a failure is "this run breaks the contract or the stall choice", and the
judge decides which.

  mk_product.py --dump <file.refmap> <d0.v> <d1.v> [<d0 top> <d1 top>]

prints the contract as this script reads it (the chan's response inputs and observed outputs, the
slot, every xfer, maxstall, period, asm) in the canonical form of
`pass/lean/tools/tests/refmap_contract_dump.lean`, which prints the judge's reading of the same
file (`HDLProof.Kit.RefMapFile.buildJudge`); judge_selftest.sh compares the two on its contracts.
The grammar is the judge's: a Boolean is `!`, `&`, `|` and parentheses over atoms `<port>`,
`<port>==<n>`, `1`, `0` (`|` binds loosest); a word is `<port>[<lo>:<len>]?<port>.<bit>`, the
slice and the guard optional.
"""
import re, sys


def fail(msg):
    raise SystemExit(msg)


def parse_refmap(path):
    lines = []
    for raw in open(path):
        line = raw.split('#', 1)[0].replace('\t', ' ').strip()
        if line:
            k, *a = line.split()
            lines.append((k, a))
    return lines


def arg(args, k):
    for a in args:
        if a.startswith(k + ':'):
            return a[len(k) + 1:]
    return None


def lst(args, k):
    v = arg(args, k)
    return [x for x in v.split(',') if x] if v else []


def nat(s):
    return int(s) if re.fullmatch(r'[0-9]+', s) else None


# ---- the contract, parsed as the judge parses it (HDLProof/Kit/RefMapFile.lean)

def parse_port(tok):
    """('in', name) or ('out', name, next): `in:<name>`, `out:<name>`, `out:<name>@next`."""
    if tok.startswith('in:'):
        return ('in', tok[3:])
    if tok.startswith('out:'):
        n = tok[4:]
        nxt = n.endswith('@next')
        return ('out', n[:-len('@next')] if nxt else n, nxt)
    fail(f'a port is out:<name>, out:<name>@next or in:<name>, got {tok}')


def parse_bool(s):
    """A Boolean as a tree: ('or'|'and', a, b), ('not', a), ('paren', a), ('tt',), ('ff',),
    ('nz', port), ('eq', port, n). `|` binds loosest, then `&`, then `!`."""
    def expr(i):
        a, i = term(i)
        while i < len(s) and s[i] == '|':
            b, i = term(i + 1)
            a = ('or', a, b)
        return a, i

    def term(i):
        a, i = factor(i)
        while i < len(s) and s[i] == '&':
            b, i = factor(i + 1)
            a = ('and', a, b)
        return a, i

    def factor(i):
        if i < len(s) and s[i] == '!':
            a, j = factor(i + 1)
            return ('not', a), j
        if i < len(s) and s[i] == '(':
            a, j = expr(i + 1)
            if not (j < len(s) and s[j] == ')'):
                fail(f"{s!r}: missing ')'")
            return ('paren', a), j + 1
        j = i
        while j < len(s) and s[j] not in '&|()!':
            j += 1
        atom = s[i:j]
        if atom == '1':
            return ('tt',), j
        if atom == '0':
            return ('ff',), j
        parts = atom.split('==')
        if len(parts) == 1:
            return ('nz', parse_port(atom)), j
        if len(parts) == 2:
            v = nat(parts[1])
            if v is None:
                fail(f'{s!r}: bad constant in {atom!r}')
            return ('eq', parse_port(parts[0]), v), j
        fail(f'{s!r}: cannot read {atom!r}')

    e, j = expr(0)
    if j != len(s):
        fail(f'trailing text in {s!r}')
    return e


def parse_word(tok):
    """An observed word: (port, lo, len or None for the whole port, guard (port, bit) or None)."""
    parts = tok.split('?')
    body, guard = (parts[0], parts[1]) if len(parts) == 2 else (tok, None)
    bp = body.split('[')
    ptxt, sl = (bp[0], bp[1][:-1]) if len(bp) == 2 else (body, None)
    port = parse_port(ptxt)
    lo, ln = 0, None
    if sl is not None:
        ab = sl.split(':')
        if len(ab) != 2:
            fail(f'a slice is [lo:len], got {tok}')
        lo, ln = nat(ab[0]), nat(ab[1])
        if lo is None or ln is None:
            fail(f'bad slice in {tok}')
    g = None
    if guard is not None:
        gp = guard.split('.')
        if len(gp) != 2:
            fail(f'a guard is <port>.<bit>, got {tok}')
        b = nat(gp[1])
        if b is None:
            fail(f'bad guard bit in {tok}')
        g = (parse_port(gp[0]), b)
    return (port, lo, ln, g)


def one_nat(L, k, d):
    """`<k> <n>` (the first such line), or `d` when there is none."""
    vs = [a for (kk, a) in L if kk == k]
    if not vs:
        return d
    if len(vs[0]) != 1 or nat(vs[0][0]) is None:
        fail(f'{k} <n>, got {" ".join(vs[0])!r}')
    return nat(vs[0][0])


class Contract:
    """The one chan, the one slot, xfer, maxstall, period and asm of a .refmap."""

    def __init__(self, L):
        get = lambda k: [a for (kk, a) in L if kk == k]
        chans, slots = get('chan'), get('slot')
        if not (len(chans) == 1 and len(slots) == 1):
            fail(f'judge: exactly one chan and one slot (got {len(chans)} and {len(slots)})')
        ch, sl = chans[0], slots[0]
        if not ch:
            fail('chan <name> allow:<out> ...')
        if len(sl) < 2:
            fail('slot <chan> <kind> [resp:..] [obs:..]')
        self.chan = ch[0]
        if sl[0] != self.chan:
            fail(f'slot: no channel {sl[0]!r}')
        self.resp_in, self.obs0 = lst(ch[1:], 'resp'), lst(ch[1:], 'obs')
        self.kind = parse_bool(sl[1])
        self.resp = [parse_port(t) for t in lst(sl[2:], 'resp')]
        self.obs_txt = lst(sl[2:], 'obs')
        self.obs = [parse_word(t) for t in self.obs_txt]
        self.xfer_txt = [' '.join(a) for a in get('xfer')]
        self.xfer = [parse_bool(x) for x in self.xfer_txt]
        self.maxstall = one_nat(L, 'maxstall', 0)
        self.period = int((get('period') or [['1']])[0][0])
        asm = get('asm')
        self.asm = None
        if asm:
            if len(asm[0]) != 1:
                fail('asm <implementation-output>')
            t = asm[0][0]
            self.asm = (t[1:], False) if t.startswith('!') else (t, True)
        late = lambda p: p[0] == 'out' and p[2]
        self.late = (any(late(p) for p in ports_of_bool(self.kind)) or any(late(p) for p in self.resp)
                     or any(late(w[0]) or (w[3] is not None and late(w[3][0])) for w in self.obs)
                     or any(late(p) for x in self.xfer for p in ports_of_bool(x)))


def ports_of_bool(e):
    if e[0] in ('nz', 'eq'):
        return [e[1]]
    return [p for a in e[1:] if isinstance(a, tuple) for p in ports_of_bool(a)]


# ---- the canonical form (refmap_contract_dump.lean prints the judge's reading the same way)

def canon_port(p):
    return f'in:{p[1]}' if p[0] == 'in' else f'out:{p[1]}{"@next" if p[2] else ""}'


def canon_bool(e):
    t = e[0]
    if t == 'tt':
        return 'tt'
    if t == 'ff':
        return 'ff'
    if t == 'nz':
        return f'nz({canon_port(e[1])})'
    if t == 'eq':
        return f'eq({canon_port(e[1])},{e[2]})'
    if t == 'not':
        return f'not({canon_bool(e[1])})'
    if t == 'paren':
        return canon_bool(e[1])
    return f'{t}({canon_bool(e[1])},{canon_bool(e[2])})'


def dump(C, P, in0, out0):
    def width(p):
        return port_expr(p, P)[1]
    def word(w):
        port, lo, ln, g = w
        s = f'{canon_port(port)}[{lo}:{width(port) if ln is None else ln}]'
        if g is not None:
            width(g[0])
            s += f'?{canon_port(g[0])}.{g[1]}'
        return s
    for d in C.resp_in:
        assert d in in0, f'no spec input {d}'
    for o in C.obs0:
        assert o in out0, f'no spec output {o}'
    for p in C.resp:
        width(p)
    for p in ports_of_bool(C.kind) + [p for x in C.xfer for p in ports_of_bool(x)]:
        width(p)
    if C.asm is not None:
        assert C.asm[0] in P.outs, f'no implementation output {C.asm[0]}'
    print(f'chan {C.chan} resp={",".join(C.resp_in)} obs={",".join(C.obs0)}')
    print(f'slot {C.chan} kind={canon_bool(C.kind)} resp={",".join(canon_port(p) for p in C.resp)} '
          f'obs={";".join(word(w) for w in C.obs)}')
    for x in C.xfer:
        print(f'xfer {canon_bool(x)}')
    print(f'maxstall {C.maxstall}')
    print(f'period {C.period}')
    print(f'asm {"-" if C.asm is None else ("" if C.asm[1] else "!") + C.asm[0]}')


# ---- the product

def ports_of(path, top):
    """(module source, [(dir, name, width)]) of the bound netlist's top module (a non-ANSI header,
    as yosys's write_verilog writes it)."""
    src = open(path).read()
    m = re.search(r'^module\s+' + top + r'\s*\(([^;]*)\);', src, re.M)
    assert m, f'{path}: no module {top}(...);'
    order = [x.strip() for x in m.group(1).split(',') if x.strip()]
    decl = {}
    body = src[m.end():]
    for d in re.finditer(r'(?:^|;)\s*(input|output)\s+(?:wire\s+|reg\s+|logic\s+)?(?:\[(\d+):(\d+)\]\s*)?([^\s;,=]+)\s*(?=[;=])', body, re.M):
        nm = d.group(4)
        w = int(d.group(2)) - int(d.group(3)) + 1 if d.group(2) else 1
        decl[nm.rstrip(';')] = (d.group(1), w)
    out = []
    for nm in order:
        assert nm in decl, f'{path}: port {nm} has no declaration'
        out.append((decl[nm][0], nm, decl[nm][1]))
    return src, out


class Ports:
    """D1's ports as the product reads them. `late` (the .refmap reads `@next`): the product checks
    judge step n at cycle n+1, so `in:<n>` and `out:<n>` read a register that holds the value of
    the cycle before (`prev_<n>`, `prev_d1_<n>`) and `out:<n>@next` reads D1's output now."""

    def __init__(self, ins, outs, late):
        self.ins, self.outs, self.late = ins, outs, late
        self.regs = {}  # register -> (the signal it holds one cycle, width), in first-use order

    def prev(self, sig, w):
        r = f'prev_{sig}'
        self.regs.setdefault(r, (sig, w))
        return r


def port_expr(p, P):
    """(Verilog, width) for a parsed port over D1 (in:<n> -> the product input, out:<n> -> D1's
    output wire, out:<n>@next -> D1's output one step later)."""
    if p[0] == 'in':
        n = p[1]
        assert n in P.ins, f'no implementation input {n}'
        return (P.prev(n, P.ins[n]) if P.late else n), P.ins[n]
    n, nxt = p[1], p[2]
    assert n in P.outs, f'no implementation output {n}'
    if nxt:
        assert P.late
        return f'd1_{n}', P.outs[n]
    return (P.prev(f'd1_{n}', P.outs[n]) if P.late else f'd1_{n}'), P.outs[n]


def bit_expr(e, P):
    """Verilog for a parsed Boolean: the operators and parentheses as written."""
    t = e[0]
    if t in ('tt', 'ff'):
        return "1'b" + ('1' if t == 'tt' else '0')
    if t == 'eq':
        return f'({port_expr(e[1], P)[0]} == {e[2]})'
    if t == 'nz':
        return f'(|{port_expr(e[1], P)[0]})'
    if t == 'not':
        return '!' + bit_expr(e[1], P)
    if t == 'paren':
        return '(' + bit_expr(e[1], P) + ')'
    return bit_expr(e[1], P) + (' && ' if t == 'and' else ' || ') + bit_expr(e[2], P)


def obs_expr(w, P):
    port, lo, ln, guard = w
    e, wd = port_expr(port, P)
    ln = wd if ln is None else ln
    v = f'{e}[{lo + ln - 1}:{lo}]' if (lo, ln) != (0, wd) else e
    if guard:
        ge, gw = port_expr(guard[0], P)
        b = guard[1]
        assert b < gw, f'guard {canon_port(guard[0])}.{b}: bit {b} of a {gw}-bit port'
        v = f'({ge if gw == 1 else f"{ge}[{b}]"} ? {v} : {ln}\'d0)'
    return v, ln


def main(refmap, d0p, d1p, outp, top0='uc_b03', top1='uc_b03', show=False):
    L = parse_refmap(refmap)
    C = Contract(L)
    R, P, asm = C.maxstall, C.period, C.asm
    s0, p0 = ports_of(d0p, top0)
    s1, p1 = ports_of(d1p, top1)
    ins = {n: w for (d, n, w) in p1 if d == 'input'}
    outs = {n: w for (d, n, w) in p1 if d == 'output'}
    in0 = {n: w for (d, n, w) in p0 if d == 'input'}
    out0 = {n: w for (d, n, w) in p0 if d == 'output'}
    if show:
        dump(C, Ports(ins, outs, C.late), in0, out0)
        return
    clk = next((c for c in ('clk_i', 'clk') if c in ins), None)
    assert clk and clk in in0, 'both designs need the clock input clk_i (or clk)'
    late = C.late
    if late and P != 1:
        raise SystemExit(f'@next with period {P}: one step is not one clock there; read the channel '
                         'late through delay registers in D0\'s observers instead')
    PX = Ports(ins, outs, late)
    resp_in, resp_w = C.resp_in, C.resp
    assert len(resp_in) == len(resp_w), 'chan resp: and slot resp: differ in length'
    d0_in = {n: f"{w}'d0" for n, w in in0.items()}
    for di, p in zip(resp_in, resp_w):
        assert di in in0, f'no spec input {di}'
        d0_in[di] = port_expr(p, PX)[0]
    obs0, obs1 = C.obs0, C.obs
    assert len(obs0) == len(obs1), 'chan obs: and slot obs: differ in length'
    fire = bit_expr(C.kind, PX)
    words = []
    for o0, w1 in zip(obs0, obs1):
        assert o0 in out0, f'no spec output {o0}'
        words.append((o0,) + obs_expr(w1, PX))
    xfers = C.xfer_txt
    xf = ' || '.join(f'({bit_expr(x, PX)})' for x in C.xfer) or "1'b0"
    T = []
    T.append('// Generated by mk_product.py: the product of D0 and D1 for a bounded search.')
    T.append(re.sub(r'^module\s+' + top0 + r'\b', 'module d0_core', s0, count=1, flags=re.M))
    T.append(re.sub(r'^module\s+' + top1 + r'\b', 'module d1_core', s1, count=1, flags=re.M))
    hdr = [clk] + [n for n in ins if n != clk] + ['ok', 'env_ok']
    T.append(f'module prod({", ".join(hdr)});')
    for n, w in ins.items():
        T.append(f'  input {"[%d:0] " % (w - 1) if w > 1 else ""}{n};')
    T.append('  output ok; output env_ok;')
    for n, w in outs.items():
        T.append(f'  wire {"[%d:0] " % (w - 1) if w > 1 else ""}d1_{n};')
    for n, w in out0.items():
        T.append(f'  wire {"[%d:0] " % (w - 1) if w > 1 else ""}d0_{n};')
    T.append('  d1_core u_d1(' + ', '.join([f'.{n}({n})' for n in ins] + [f'.{n}(d1_{n})' for n in outs]) + ');')
    if late:
        T.append('  // @next: cycle m checks judge step m-1; prev_* hold the ports of the cycle before')
        for r, (sig, w) in PX.regs.items():
            T.append(f'  reg {"[%d:0] " % (w - 1) if w > 1 else ""}{r} = {w}\'d0;')
        T.append('  reg started = 1\'b0;  // 0 in cycle 0 only')
        T.append(f'  always @(posedge {clk}) begin')
        T.append('    started <= 1\'b1;')
        for r, (sig, w) in PX.regs.items():
            T.append(f'    {r} <= {sig};')
        T.append('  end')
    T.append(f'  wire fire = {fire};')
    if R == 0 and not late:
        T.append(f'  wire d0_clk = {clk};')
    else:
        T.append('  reg en_l;  // the gate\'s enable latch: open while the clock is low')
        T.append(f'  always @* if (!{clk}) en_l = {"started & fire" if late else "fire"};')
        T.append(f'  wire d0_clk = {clk} & en_l;')
    conn0 = [f'.{clk}(d0_clk)'] + [f'.{n}({d0_in[n]})' for n in in0 if n != clk] + [f'.{n}(d0_{n})' for n in out0]
    T.append('  d0_core u_d0(' + ', '.join(conn0) + ');')
    eqs = []
    for i, (o0, v, ln) in enumerate(words):
        T.append(f'  wire eq_{i} = ({v}) == ({max(ln, out0[o0])}\'d0 | d0_{o0});')
        eqs.append(f'eq_{i}')
    T.append(f'  wire all_eq = {" && ".join(eqs) if eqs else "1" + chr(39) + "b1"};')
    T.append(f'  wire any_xfer = {xf};')
    T.append('  reg [15:0] holds = 16\'d0;  // periods in a row without a D0 step')
    if late:
        T.append(f'  always @(posedge {clk}) if (started) holds <= fire ? 16\'d0 : holds + 16\'d1;')
        T.append(f'  assign ok = !started || ((fire ? all_eq : !any_xfer) && (holds <= 16\'d{R}));')
    else:
        T.append(f'  always @(posedge {clk}) holds <= fire ? 16\'d0 : holds + 16\'d1;')
        T.append(f'  assign ok = (fire ? all_eq : !any_xfer) && (holds <= 16\'d{R});')
    if asm is not None:
        assert asm[0] in outs, f'no implementation output {asm[0]}'
    env = [(f'd1_{asm[0]}' if asm[1] else f'!d1_{asm[0]}') if asm else "1'b1"]
    if P == 2:
        allin = '{' + ', '.join(n for n in ins if n != clk) + '}'
        win = sum(w for n, w in ins.items() if n != clk)
        T.append('  // the real clock\'s input timing: phase-0 steps are those with pa == pb')
        T.append('  reg pa = 1\'b0; reg pb = 1\'b0; reg seen = 1\'b0;')
        T.append(f'  always @(posedge {clk}) pa <= ~pa;')
        T.append(f'  always @(negedge {clk}) begin pb <= pa; seen <= 1\'b1; end')
        T.append(f'  reg [{win - 1}:0] in_n = {win}\'d0;')
        T.append(f'  always @(negedge {clk}) in_n <= {allin};')
        env.append(f'(!(seen && pa == pb) || {allin} == in_n)')
    elif P not in (1,):
        raise SystemExit(f'period {P}: only 1 or 2 are supported')
    T.append(f'  assign env_ok = {" && ".join(env)};')
    T.append('endmodule')
    open(outp, 'w').write('\n'.join(T) + '\n')
    lag = ('; @next: D0 one step behind, a counterexample at cycle m is judge step m-1'
           if late else '')
    print(f'{outp}: {len(ins)} inputs, {len(eqs)} observed pairs, {len(xfers)} xfer, maxstall {R}, period {P}{lag}')


if __name__ == '__main__':
    if sys.argv[1:2] == ['--dump']:
        a = sys.argv[2:]
        main(a[0], a[1], a[2], None, *a[3:5], show=True)
    else:
        main(*sys.argv[1:7])
