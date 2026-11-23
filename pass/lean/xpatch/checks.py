#!/usr/bin/env python3
"""checks.py -- the comparisons run.sh grades, each one COUNTING what it compared (a comparison
that compares nothing exits 3, never 0).

  ckpt <a.ckpt> <b.ckpt>
      two lhdcert-ckpt v1 files, element by element: design hash, cycle, every flop by ordinal,
      every memory word below the depth (defaults expanded). Exit 0 same, 1 different.
  steps <a.trace> <b.trace> <lo> <hi>
      cert_trace's `STEP <k> <outputs...>` lines for k = lo..hi. Exit 0 same, 1 different/missing.
  ok <trace> <lo> <hi>
      the product's one output on STEP lines lo..hi: how many cycles read ok = 1 and ok = 0.
      Exit 0 when every line reads 1.
  product <product _io.json> <product states> <A _io.json> <A states> <B _io.json> <B states>
          <transport.json> <lo> <hi>
      the product design's run, split by name into its A part (a_*), B part (b_*) and
      monitor (m_*), at every cycle lo..hi, against A's run, B's run and transport.py's monitor
      values. Exit 0 when all agree.
  thm <theorem .lean> <product .dcert> <product _io.json> <other .dcert> <out dir>
      sym_verify's emitted theorem file, read as text: its embedded design against the product's
      certificate (byte for byte), its spec against the product's _io.json, and its text outside
      the three literals against sym_verify's template, character for character. Writes three
      corrupted copies for run.sh to give Lean (each must be refused): badproof (the step proof
      cut to half its bytes), wrongdesign (the other certificate's text in place of the product's),
      baddesign (text that is not a certificate). Exit 0 when every check holds.
  selftest
      every comparison above, on deliberately corrupted inputs, must report the corruption."""
import hashlib, json, os, re, sys, tempfile

# ---------------------------------------------------------------- lhdcert-ckpt v1 and traces
def parse_ckpt(path):
    lines = open(path).read().split('\n')
    if lines[0] != 'lhdcert-ckpt v1':
        raise SystemExit(f'{path}: not lhdcert-ckpt v1')
    out = {'flops': [], 'mems': []}
    i = 1
    while i < len(lines):
        t = lines[i].split()
        if not t:
            i += 1
            continue
        if t[0] in ('design', 'cycle'):
            out[t[0]] = t[1]
        elif t[0] == 'flops':
            n = int(t[1])
            out['flops'] = [tuple(lines[i + 1 + k].split()) for k in range(n)]
            i += n
        elif t[0] == 'mem':
            aw, dw, depth, dflt, k = t[1], t[2], int(t[3]), int(t[4], 16), int(t[5])
            words = {}
            for r in range(k):
                a, v = lines[i + 1 + r].split()
                words[int(a, 16)] = int(v, 16)
            out['mems'].append({'shape': (aw, dw, depth), 'img': [words.get(a, dflt) for a in range(depth)]})
            i += k
        elif t[0] == 'digest':
            break
        i += 1
    return out

def cmp_ckpt(a, b):
    A, B = parse_ckpt(a), parse_ckpt(b)
    first = None
    fc = fd = wc = wd = 0
    for k in range(max(len(A['flops']), len(B['flops']))):
        x = A['flops'][k] if k < len(A['flops']) else None
        y = B['flops'][k] if k < len(B['flops']) else None
        fc += 1
        if x != y:
            fd += 1
            first = first or f'flop {k}: {x} vs {y}'
    for m in range(max(len(A['mems']), len(B['mems']))):
        x = A['mems'][m] if m < len(A['mems']) else None
        y = B['mems'][m] if m < len(B['mems']) else None
        if x is None or y is None or x['shape'] != y['shape']:
            wd += 1
            first = first or f'memory {m}: shape {x and x["shape"]} vs {y and y["shape"]}'
            continue
        for adr, (p, q) in enumerate(zip(x['img'], y['img'])):
            wc += 1
            if p != q:
                wd += 1
                first = first or f'memory {m} word {adr}: {p:#x} vs {q:#x}'
    ds = 'same' if A.get('design') == B.get('design') else 'differ'
    cs = 'same' if A.get('cycle') == B.get('cycle') else 'differ'
    if ds == 'differ':
        first = first or f"design {A.get('design')} vs {B.get('design')}"
    if cs == 'differ':
        first = first or f"cycle {A.get('cycle')} vs {B.get('cycle')}"
    msg = (f"design {ds}, cycle {cs} ({A.get('cycle')}), flops {fc}/{fd}, words {wc}/{wd}, "
           f"first difference {first or 'none'}")
    rc = 3 if fc + wc == 0 else (0 if (fd + wd == 0 and ds == 'same' and cs == 'same') else 1)
    return rc, msg

def steps(path):
    d = {}
    for l in open(path):
        t = l.split()
        if len(t) >= 2 and t[0] == 'STEP':
            d[int(t[1])] = t[2:]
    return d

def cmp_steps(a, b, lo, hi):
    A, B = steps(a), steps(b)
    ks = range(lo, hi + 1)
    both = [k for k in ks if k in A and k in B]
    diff = [k for k in both if A[k] != B[k]]
    missing = [k for k in ks if k not in A or k not in B]
    msg = (f"{len(both)} lines compared, {len(diff)} differ, {len(missing)} missing"
           + (f", first difference at step {diff[0]}: {A[diff[0]]} vs {B[diff[0]]}" if diff else ""))
    rc = 3 if not both else (0 if not diff and not missing else 1)
    return rc, msg

def ok_trace(path, lo, hi):
    S = steps(path)
    ks = [k for k in range(lo, hi + 1) if k in S]
    ones = [k for k in ks if S[k][0] == '1']
    zeros = [k for k in ks if S[k][0] == '0']
    other = [k for k in ks if S[k][0] not in ('0', '1')]
    msg = (f"{len(ks)} STEP lines read, ok = 1 on {len(ones)}, ok = 0 on {len(zeros)}"
           + (f" (first at step {zeros[0]})" if zeros else "") + (f", unreadable {len(other)}" if other else ""))
    rc = 3 if not ks else (0 if len(ones) == len(ks) == hi - lo + 1 else 1)
    return rc, msg

# ---------------------------------------------------------------- the product's run, split
def named(io_path, ck_path):
    io = json.load(open(io_path))
    c = parse_ckpt(ck_path)
    fl = {f['name']: int(c['flops'][f['ordinal']][1], 16) for f in io['flops']}
    mems = {m['inst']: c['mems'][m['ordinal']]['img'] for m in io['memories']}
    return fl, mems

def cmp_product(p_io, p_dir, a_io, a_dir, b_io, b_dir, tj, lo, hi):
    """Every flop and memory of the product is a_<x> (x of A), b_<x> (x of B) or m_<x> (a
    monitor value transport.py wrote); anything else counts as a difference."""
    mon = {t['cycle']: t['h'] for t in json.load(open(tj))['monitor_trace']}
    n = d = 0
    first = None
    cycles = 0
    for j in range(lo, hi + 1):
        pf, pm = named(p_io, os.path.join(p_dir, f'{j}.ckpt'))
        af, am = named(a_io, os.path.join(a_dir, f'{j}.ckpt'))
        bf, bm = named(b_io, os.path.join(b_dir, f'{j}.ckpt'))
        pairs = []
        for pre, fl, mm, who in (('a_', af, am, 'A'), ('b_', bf, bm, 'B')):
            pairs += [(f'{pre}{k}', pf.get(f'{pre}{k}'), f'{who}.{k}', v) for k, v in fl.items()]
            for k, img in mm.items():
                pimg = pm.get(f'{pre}{k}', [])
                pairs += [(f'{pre}{k}[{w}]', pimg[w] if w < len(pimg) else None, f'{who}.{k}[{w}]', v)
                          for w, v in enumerate(img)]
        mh = mon[j]
        pairs += [(k, pf.get(k), f'transport.py {k}', v) for k, v in sorted(mh.items())]
        seen = {p[0] for p in pairs}
        pairs += [(k, pf[k], 'nothing', None) for k in sorted(pf) if k not in seen]
        pairs += [(k, 'memory', 'nothing', None) for k in sorted(pm) if not any(s.startswith(k + '[') for s in seen)]
        cycles += 1
        for pn, pv, on, ov in pairs:
            n += 1
            if pv is None or ov is None or pv != ov:
                d += 1
                first = first or f'cycle {j}: product {pn} = {pv} vs {on} = {ov}'
    msg = f"{cycles} cycles, {n} values compared, {d} differ, first difference {first or 'none'}"
    return (3 if n == 0 else (0 if d == 0 else 1)), msg

# ---------------------------------------------------------------- the emitted theorem file
def lean_str_at(text, start):
    """parse the Lean string literal whose opening quote is text[start]; leanStr (sym_verify.lean)
    escapes exactly ", \\ and newline. Returns (value, index after the closing quote)."""
    assert text[start] == '"'
    out, i = [], start + 1
    while True:
        c = text[i]
        if c == '"':
            return ''.join(out), i + 1
        if c == '\\':
            e = text[i + 1]
            if e not in 'n"\\':
                raise ValueError(f'unexpected escape \\{e} at {i}')
            out.append({'n': '\n', '"': '"', '\\': '\\'}[e])
            i += 2
        else:
            out.append(c)
            i += 1

def lean_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

# The whole file outside its three literals must be exactly sym_verify's emitF2 template
# (pass/lean/tools/drivers/sym_verify.lean) for sim:fp with init:reset's s0, no extra imports or
# definitions: then nothing in it can weaken the statement or bypass the check.
TEMPLATE = """/- Generated by sym_verify (sim:fp). The certificate is the DCERT2 text below;
the LRAT proofs came from an untrusted solver and are checked by `runInvKF`, which
`native_decide` evaluates (trusted-native: one `_native` axiom). -/
import HDLProof.Sym.Closed
import LeanSemanticPrimitives.Compiler.CertIOCodec
open Compiler Projection HDLProof HDLProof.Sym

namespace SymF2.@NAME@

def certText : String := "<design>"

def D : DesignCert := match CertIO.readCertChecked certText.toUTF8 with
  | .ok d => d
  | .error _ => default

def s0 : StateM := Projection.HwMem.initStateOf D

def spec : InvSpec := @SPEC@

def pfB : ByteArray := <pfB>
def pfS : ByteArray := <pfS>

theorem verdict : runInvKF @FUEL@ D spec s0 pfB pfS = true := by native_decide

theorem invariant (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = spec.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) spec.asmOut spec.asmIn = true) →
      propBit (refOut D (StateM.denote s0) ins n) spec.prop = true :=
  runInvKF_sound verdict ins hins

#print axioms invariant

end SymF2.@NAME@
"""

def template_match(skeleton):
    rx = re.escape(TEMPLATE)
    for ph, g in (('@NAME@', r'(?P<name>[A-Za-z0-9_]+)'), ('@SPEC@', r'(?P<spec>\{[^\n]*\})'),
                  ('@FUEL@', r'(?P<fuel>\d+ \d+)')):
        rx = rx.replace(re.escape(ph), g, 1).replace(re.escape(ph), r'(?P=name)')
    return re.fullmatch(rx, skeleton)

def check_thm(thm, dcert, p_io, other, outdir):
    text = open(thm, encoding='utf-8').read()
    res, bad = [], []
    m = re.search(r'^def certText : String := ', text, re.M)
    lit0 = m.end()
    cert, lit1 = lean_str_at(text, lit0)
    want = open(dcert, encoding='utf-8').read()
    h = lambda s: hashlib.sha256(s.encode()).hexdigest()[:16]
    same = cert == want
    res.append(f"embedded design {len(cert)} bytes sha256 {h(cert)} vs the product's certificate "
               f"{len(want)} bytes sha256 {h(want)}: {'identical' if same else 'DIFFERENT'}")
    same or bad.append('embedded design')
    lits = {}
    for nm in ('pfB', 'pfS'):
        mm = re.search(rf'^def {nm} : ByteArray := ⟨#\[([0-9, ]*)\]⟩$', text, re.M)
        lits[nm] = mm
    skeleton = text[:lit0] + '"<design>"' + text[lit1:]
    for nm in ('pfB', 'pfS'):
        skeleton = skeleton.replace(lits[nm].group(0), f'def {nm} : ByteArray := <{nm}>')
    nb = {nm: len(lits[nm].group(1).split(',')) for nm in lits}
    res.append(f"proof literals: pfB {nb['pfB']} bytes, pfS {nb['pfS']} bytes")
    tm = template_match(skeleton)
    checks = {'text outside the three literals = sym_verify\'s template, exactly': tm is not None}
    if tm is None:
        for k, v in checks.items():
            res.append(f"{k}: NO")
        return 1, '; '.join(res) + "; FAILED: ['template']"
    sp = tm.group('spec')
    res.append(f"theorem SymF2.{tm.group('name')}.invariant, runInvKF fuel {tm.group('fuel')}")
    io = json.load(open(p_io))
    out0 = [o['name'] for o in io['outputs'] if o['ordinal'] == 0]
    inw = [p['width'] for p in sorted(io['inputs'], key=lambda p: p['ordinal'])]
    fws = [f['width'] for f in sorted(io['flops'], key=lambda f: f['ordinal'])]
    mds = [(mm['aw'], mm['dw']) for mm in sorted(io['memories'], key=lambda mm: mm['ordinal'])]
    def num_list(key):
        mm = re.search(rf'{key} := \[([0-9, ]*)\]', sp)
        return [int(x) for x in mm.group(1).split(',')] if mm and mm.group(1).strip() else []
    checks.update({
        f"property = output 0 nonzero, and output 0 is {out0}": 'prop := { out := 0, nz := true }' in sp and out0 == ['ok'],
        'no assumptions (asmOut none, asmIn [])': 'asmOut := none, asmIn := []' in sp,
        'k = 1': ', k := 1,' in sp,
        f'input widths {inw} = spec.inW': num_list('inW') == inw,
        f'flop widths ({len(fws)}) = spec fws': num_list('fws') == fws,
        f'memory shapes {mds} = spec mds': re.search(r'mds := \[(.*?)\] \}', sp).group(1).replace(' ', '') ==
                                             ','.join(f'({a},{d})' for a, d in mds),
    })
    for k, v in checks.items():
        res.append(f"{k}: {'yes' if v else 'NO'}")
        v or bad.append(k)
    # the corrupted copies (each must be refused by Lean)
    os.makedirs(outdir, exist_ok=True)
    s = lits['pfS'].group(1).split(', ')
    cut = f"def pfS : ByteArray := ⟨#[{', '.join(s[:len(s) // 2])}]⟩"
    mut = {
        'badproof': text.replace(lits['pfS'].group(0), cut),
        'wrongdesign': text[:lit0] + lean_str(open(other, encoding='utf-8').read()) + text[lit1:],
        'baddesign': text[:lit0] + lean_str('not a certificate\n') + text[lit1:],
    }
    for k, t in mut.items():
        assert t != text
        p = os.path.join(outdir, f'{k}.lean')
        open(p, 'w', encoding='utf-8').write(t)
        res.append(f"corrupted copy {k}: {p} sha256 {hashlib.sha256(t.encode()).hexdigest()[:16]}")
    msg = '; '.join(res)
    return (0 if not bad else 1), msg + (f"; FAILED: {bad}" if bad else '')

# ---------------------------------------------------------------- selftest
def selftest():
    ok = fail = 0
    d = tempfile.mkdtemp(prefix='xpatch_selftest_')
    def w(name, s):
        p = os.path.join(d, name)
        open(p, 'w').write(s)
        return p
    base = "lhdcert-ckpt v1\ndesign 1111\ncycle 10\nflops 2\n8 23\n8 a\nmems 1\nmem 4 8 12 0 1\n3 3\nexts 0\ndigest 0\n"
    a = w('a.ckpt', base)
    cases = [
        ('ckpt same', cmp_ckpt(a, w('b.ckpt', base))[0] == 0),
        ('ckpt flop changed', cmp_ckpt(a, w('c.ckpt', base.replace('8 23', '8 24')))[0] == 1),
        ('ckpt memory word changed', cmp_ckpt(a, w('d.ckpt', base.replace('\n3 3\n', '\n3 4\n')))[0] == 1),
        ('ckpt cycle changed', cmp_ckpt(a, w('e.ckpt', base.replace('cycle 10', 'cycle 11')))[0] == 1),
        ('ckpt empty refused', cmp_ckpt(w('f.ckpt', "lhdcert-ckpt v1\ndesign 1\ncycle 0\nflops 0\nmems 0\n"),
                                         w('g.ckpt', "lhdcert-ckpt v1\ndesign 1\ncycle 0\nflops 0\nmems 0\n"))[0] == 3),
        ('steps same', cmp_steps(w('t1', 'STEP 0 1 2\nSTEP 1 1 3\n'), w('t2', 'STEP 0 1 2\nSTEP 1 1 3\n'), 0, 1)[0] == 0),
        ('steps changed', cmp_steps(w('t3', 'STEP 0 1 2\nSTEP 1 1 3\n'), w('t4', 'STEP 0 1 2\nSTEP 1 1 4\n'), 0, 1)[0] == 1),
        ('steps missing', cmp_steps(w('t5', 'STEP 0 1 2\n'), w('t6', 'STEP 0 1 2\nSTEP 1 1 3\n'), 0, 1)[0] == 1),
        ('steps none refused', cmp_steps(w('t7', ''), w('t8', ''), 0, 1)[0] == 3),
        ('ok all ones', ok_trace(w('o1', 'STEP 0 1\nSTEP 1 1\n'), 0, 1)[0] == 0),
        ('ok one zero', ok_trace(w('o2', 'STEP 0 1\nSTEP 1 0\n'), 0, 1)[0] == 1),
        ('ok line missing', ok_trace(w('o3', 'STEP 0 1\n'), 0, 1)[0] == 1),
        ('lean string round trip', lean_str_at(lean_str('a"b\\c\nd'), 0)[0] == 'a"b\\c\nd'),
    ]
    # the product split: a tiny product (a_x, b_x, m_y, memories a_mem and b_mem) and its parts
    def io(path, flops, mems):
        return w(path, json.dumps({'flops': [{'name': f, 'ordinal': k} for k, f in enumerate(flops)],
                                   'memories': [{'inst': m, 'ordinal': k} for k, m in enumerate(mems)]}))
    def ck(d, j, vals, mems):
        os.makedirs(os.path.join(d, ''), exist_ok=True)
        body = f"lhdcert-ckpt v1\ndesign 1\ncycle {j}\nflops {len(vals)}\n" + ''.join(f'8 {v:x}\n' for v in vals)
        body += f"mems {len(mems)}\n" + ''.join(f"mem 1 8 2 0 2\n0 {a:x}\n1 {b:x}\n" for a, b in mems)
        open(os.path.join(d, f'{j}.ckpt'), 'w').write(body + "exts 0\ndigest 0\n")
    p_io, a_io, b_io = io('p.json', ['a_x', 'b_x', 'm_y'], ['a_mem', 'b_mem']), io('a.json', ['x'], ['mem']), io('b.json', ['x'], ['mem'])
    for sub, pv in (('good', [3, 4, 9]), ('bad', [3, 5, 9])):
        ck(os.path.join(d, sub, 'p'), 0, pv, [(1, 2), (6, 7)])
    ck(os.path.join(d, 'a'), 0, [3], [(1, 2)]); ck(os.path.join(d, 'b'), 0, [4], [(6, 7)])
    tj = w('t.json', json.dumps({'monitor_trace': [{'cycle': 0, 'h': {'m_y': 9}}]}))
    x_io = io('x.json', ['a_x', 'b_x', 'm_y', 'z'], ['a_mem', 'b_mem'])
    ck(os.path.join(d, 'extra', 'p'), 0, [3, 4, 9, 0], [(1, 2), (6, 7)])
    split = lambda sub, pio=p_io: cmp_product(pio, os.path.join(d, sub, 'p'), a_io, os.path.join(d, 'a'), b_io,
                                              os.path.join(d, 'b'), tj, 0, 0)
    cases += [
        ('product split equal', split('good')[0] == 0 and '7 values compared, 0 differ' in split('good')[1]),
        ('product split one value changed', split('bad')[0] == 1 and '1 differ' in split('bad')[1]),
        ('product split with an unaccounted flop', split('extra', x_io)[0] == 1 and 'product z' in split('extra', x_io)[1]),
    ]
    inst = TEMPLATE.replace('@NAME@', 'T').replace('@SPEC@', '{ k := 1 }').replace('@FUEL@', '1 2')
    cases += [
        ('template instance matches', template_match(inst) is not None),
        ('template + an extra axiom refused', template_match(inst.replace('#print axioms invariant',
                                                    'axiom cheat : False\n#print axioms invariant')) is None),
        ('template with a weaker statement refused', template_match(inst.replace('∀ n, (∀ j', '∀ n, n < 5 → (∀ j')) is None),
        ('template with another start state refused', template_match(inst.replace('initStateOf D', 'initStateOf D |>.zero')) is None),
    ]
    for name, good in cases:
        if good:
            ok += 1
        else:
            fail += 1
            print(f'selftest case FAILED: {name}')
    print(f"checks selftest: {ok} ok / {fail} FAIL (cases examined: {len(cases)})")
    return 0 if fail == 0 and ok == len(cases) else 1

if __name__ == '__main__':
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'selftest':
        sys.exit(selftest())
    fn = {'ckpt': lambda: cmp_ckpt(*args),
          'steps': lambda: cmp_steps(args[0], args[1], int(args[2]), int(args[3])),
          'ok': lambda: ok_trace(args[0], int(args[1]), int(args[2])),
          'product': lambda: cmp_product(*args[:7], int(args[7]), int(args[8])),
          'thm': lambda: check_thm(*args)}[cmd]
    rc, msg = fn()
    print(f"{cmd}: {msg}")
    sys.exit(rc)
