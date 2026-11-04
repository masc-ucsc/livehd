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

`sym_verify prod.dcert ok asm:env_ok base k:<K>` then searches K steps from reset; its
counterexample TSV is a run for `sym_refmap judge ... trace:`. The product decides nothing: with
the slot's stall choice a failure is "this run breaks the contract or the stall choice", and the
judge decides which.
"""
import re, sys



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


def port_expr(tok, ins, outs, pre):
    """Verilog for a .refmap port token over D1 (in:<n> -> the product input, out:<n> -> D1's output wire)."""
    if tok.startswith('in:'):
        n = tok[3:]
        assert n in ins, f'no implementation input {n}'
        return n, ins[n]
    if tok.startswith('out:'):
        n = tok[4:]
        assert not n.endswith('@next'), '@next words are not supported by the product'
        assert n in outs, f'no implementation output {n}'
        return f'{pre}{n}', outs[n]
    raise SystemExit(f'a port is out:<name> or in:<name>, got {tok}')


def bit_expr(s, ins, outs):
    """A .refmap Boolean (`!`, `&`, `|`, parentheses; atoms <port>, <port>==<n>, 1, 0)."""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c in '!&|()':
            out.append({'!': '!', '&': ' && ', '|': ' || ', '(': '(', ')': ')'}[c]); i += 1; continue
        j = i
        while j < len(s) and s[j] not in '!&|()':
            j += 1
        atom = s[i:j]; i = j
        if atom in ('1', '0'):
            out.append("1'b" + atom); continue
        if '==' in atom:
            p, v = atom.split('==')
            e, _ = port_expr(p, ins, outs, 'd1_')
            out.append(f'({e} == {int(v)})'); continue
        e, _ = port_expr(atom, ins, outs, 'd1_')
        out.append(f'(|{e})')
    return ''.join(out)


def obs_expr(tok, ins, outs):
    body, guard = (tok.split('?') + [None])[:2]
    if '[' in body:
        p, sl = body.split('[', 1)
        lo, ln = map(int, sl.rstrip(']').split(':'))
    else:
        p, lo, ln = body, 0, None
    e, w = port_expr(p, ins, outs, 'd1_')
    ln = w if ln is None else ln
    v = f'{e}[{lo + ln - 1}:{lo}]' if (lo, ln) != (0, w) else e
    if guard:
        gp, gb = guard.split('.')
        ge, _ = port_expr(gp, ins, outs, 'd1_')
        v = f'({ge}[{int(gb)}] ? {v} : {ln}\'d0)'
    return v, ln


def main(refmap, d0p, d1p, outp, top0='uc_b03', top1='uc_b03'):
    L = parse_refmap(refmap)
    get = lambda k: [a for (kk, a) in L if kk == k]
    chans, slots = get('chan'), get('slot')
    assert len(chans) == 1 and len(slots) == 1, 'exactly one chan and one slot'
    ch, sl = chans[0], slots[0]
    xfers = [' '.join(a) for a in get('xfer')]
    R = int((get('maxstall') or [['0']])[0][0])
    P = int((get('period') or [['1']])[0][0])
    asm = (get('asm') or [[None]])[0][0]
    s0, p0 = ports_of(d0p, top0)
    s1, p1 = ports_of(d1p, top1)
    ins = {n: w for (d, n, w) in p1 if d == 'input'}
    outs = {n: w for (d, n, w) in p1 if d == 'output'}
    in0 = {n: w for (d, n, w) in p0 if d == 'input'}
    out0 = {n: w for (d, n, w) in p0 if d == 'output'}
    clk = next((c for c in ('clk_i', 'clk') if c in ins), None)
    assert clk and clk in in0, 'both designs need the clock input clk_i (or clk)'
    resp_in, resp_w = lst(ch[1:], 'resp'), lst(sl[2:], 'resp')
    assert len(resp_in) == len(resp_w), 'chan resp: and slot resp: differ in length'
    d0_in = {n: f"{w}'d0" for n, w in in0.items()}
    for di, tok in zip(resp_in, resp_w):
        assert di in in0, f'no spec input {di}'
        d0_in[di] = port_expr(tok, ins, outs, 'd1_')[0]
    obs0, obs1 = lst(ch[1:], 'obs'), lst(sl[2:], 'obs')
    assert len(obs0) == len(obs1), 'chan obs: and slot obs: differ in length'
    fire = bit_expr(sl[1], ins, outs)
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
    T.append(f'  wire fire = {fire};')
    if R == 0:
        T.append(f'  wire d0_clk = {clk};')
    else:
        T.append('  reg en_l;  // the gate\'s enable latch: open while the clock is low')
        T.append(f'  always @* if (!{clk}) en_l = fire;')
        T.append(f'  wire d0_clk = {clk} & en_l;')
    conn0 = [f'.{clk}(d0_clk)'] + [f'.{n}({d0_in[n]})' for n in in0 if n != clk] + [f'.{n}(d0_{n})' for n in out0]
    T.append('  d0_core u_d0(' + ', '.join(conn0) + ');')
    eqs = []
    for i, (o0, w1) in enumerate(zip(obs0, obs1)):
        assert o0 in out0, f'no spec output {o0}'
        v, ln = obs_expr(w1, ins, outs)
        T.append(f'  wire eq_{i} = ({v}) == ({max(ln, out0[o0])}\'d0 | d0_{o0});')
        eqs.append(f'eq_{i}')
    T.append(f'  wire all_eq = {" && ".join(eqs) if eqs else "1" + chr(39) + "b1"};')
    xf = ' || '.join(f'({bit_expr(x, ins, outs)})' for x in xfers) or "1'b0"
    T.append(f'  wire any_xfer = {xf};')
    T.append('  reg [15:0] holds = 16\'d0;  // periods in a row without a D0 step')
    T.append(f'  always @(posedge {clk}) holds <= fire ? 16\'d0 : holds + 16\'d1;')
    T.append(f'  assign ok = (fire ? all_eq : !any_xfer) && (holds <= 16\'d{R});')
    env = [f'd1_{asm}' if asm else "1'b1"]
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
    print(f'{outp}: {len(ins)} inputs, {len(eqs)} observed pairs, {len(xfers)} xfer, maxstall {R}, period {P}')


if __name__ == '__main__':
    main(*sys.argv[1:7])
