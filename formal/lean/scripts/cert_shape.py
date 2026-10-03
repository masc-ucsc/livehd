#!/usr/bin/env python3
"""Static shape of a DCERT1 certificate -- no Lean, no specialization.

Answers the question the residual profile cannot: for a given design, how DEEP
are its slot reads?  `I_hw` keeps the slot environment as a cons chain and
reads slot `s` at depth `envlen - 1 - s`, so the sum of those depths is the
number of `tl` steps a NON-collapsing specializer would have to emit.  Printing
it next to the measured residual size says whether slot depth can account for
the residual at all.

This is a PREDICTOR, not a measurement of the specializer.  A specializer that
peels the chain structurally emits none of these; the point of the number is
that it bounds what the chain could possibly contribute.

Field layout is read off Compiler/CertIO.lean's reader (readSource/readNode).

    python3 scripts/cert_shape.py FILE.dcert [...]
"""
import sys, collections

SRC_TAGS = {0: ('input', 2), 1: ('const', 2), 2: ('flopQ', 2),
            3: ('flopQAsync', 5), 4: ('memImg', 3)}


def parse(path):
    toks = open(path).read().split()
    if not toks or toks[0] != 'DCERT1':
        raise SystemExit(f'{path}: bad magic')
    p = [1]

    def nxt():
        v = int(toks[p[0]]); p[0] += 1; return v

    srcs = []
    for _ in range(nxt()):
        t = nxt()
        if t == 5:                       # memInit: aw dw n then n values
            aw, dw, n = nxt(), nxt(), nxt()
            for _ in range(n): nxt()
            srcs.append('memInit')
        elif t in SRC_TAGS:
            kind, nf = SRC_TAGS[t]
            for _ in range(nf): nxt()
            srcs.append(kind)
        else:
            raise SystemExit(f'{path}: unknown source tag {t}')
    nodes = []
    for _ in range(nxt()):
        c, _arg, _w = nxt(), nxt(), nxt()
        deps = [nxt() for _ in range(nxt())]
        nxt()                            # origin
        nodes.append((c, deps))
    outs = [(nxt(), nxt()) for _ in range(nxt())]
    return srcs, nodes, outs


def report(path):
    srcs, nodes, outs = parse(path)
    ns, nn = len(srcs), len(nodes)
    print(f'{path}')
    print(f'  sources {ns}  nodes {nn}  outputs {len(outs)}')
    print(f'  source kinds: {dict(collections.Counter(srcs))}')

    tgt = collections.Counter()
    total = 0
    dmax = 0
    ndeps = 0
    for i, (_c, deps) in enumerate(nodes):
        envlen = ns + i                  # entries pushed before this node
        for d in deps:
            tgt['node' if d >= ns else srcs[d]] += 1
            k = envlen - 1 - d
            total += k
            dmax = max(dmax, k)
            ndeps += 1
    print(f'  dep targets: {dict(tgt)}')
    if ndeps:
        print(f'  slot reads {ndeps}   sum of nthD depths {total:,}'
              f'   mean {total/ndeps:.1f}   max {dmax}')
    # The input vector is a SEPARATE nthD chain, over runtime inputs only.
    inputs = sum(1 for s in srcs if s == 'input')
    print(f'  input-vector chain: {inputs} inputs'
          f'  -> at most {inputs*(inputs-1)//2:,} tl steps')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for f in sys.argv[1:]:
        report(f)
