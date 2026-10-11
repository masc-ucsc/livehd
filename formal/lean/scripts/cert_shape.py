#!/usr/bin/env python3
"""Static shape of a DCERT1 certificate -- no Lean, no specialization.

Reports the two `nthD` chains `I_hw` walks, which section 7 of PHASE6_PERF.md
shows are what the residual is almost entirely made of:

  (A) the RUNTIME VECTOR chain.  `srcVal` reads an `.input idx` source as
      `nthD inp idx` and a `.flopQ idx` as `nthD fq idx`, so each such source
      costs its OWN INDEX in steps -- once per source DECLARATION, whether or
      not anything reads the slot.  `.flopQAsync idx .. ri ..` reads both
      vectors and costs `ri + idx`.

  (B) the SOURCE-PREFIX slot chain.  `mkSources` conses source `d` at depth
      `nSources - 1 - d`, so a node dep pointing at source `d` costs that many
      steps.  Deps pointing at NODE results cost nothing (measured: `midD`).

SCOPE -- read this before quoting a number:

  * This is a PREDICTOR for `I_hw` as currently written, not a bound on any
    other interpreter or schema.
  * (B) assumes each operator reads each of its deps EXACTLY ONCE.  That holds
    for the ops in `rt_intpipe_alu`, where the prediction lands within 0.015%
    of the measured `tl` count, and it is NOT established for ops this
    predictor has never been checked against.  `--ops` prints the op histogram
    so you can see whether a design leaves that validated set.
  * Control flow is ignored.  A dynamic `ite` residualises BOTH arms, so an op
    whose arms read different slots pays for both; the aggregate depth here is
    therefore not a universal execution bound.
  * Memory sources are `bvMk 0 0` in `I_hw` -- defined-and-wrong, unsupported
    -- so they are counted as free and flagged.

  * (B) is RESIDUAL `tl` in the regime where only the source prefix
    residualizes.  It is NOT the number of `nthD` unfolds the specializer
    performs: each unfold is a `.ucall .dyn` whether or not it leaves a `tl`,
    and a dep read from node i costs `(ns + i) - 1 - d` unfolds, node-targeted
    deps included.  On `rt_alu_gate` that is 41,271,557 against (B)'s
    18,828,961, and the residual actually holds 65 `tl`.  Using (B) as a
    specialization-time step count is exactly the error PHASE6_PERF.md 32
    records.

  * The flop-state chain is reported separately as (C) and is only the
    `nthD fq idx` hold read; `flopD` residualises about TWICE that, so (C) is a
    lower bound, not a fit.

    python3 scripts/cert_shape.py [--ops] [--summary] FILE.dcert [...]
"""
import sys, collections

OPNAME = {0:'Const',1:'Sum',2:'Sub',3:'Mult',4:'Div',5:'UDiv',6:'SDiv',7:'And',
          8:'Or',9:'Xor',10:'Ror',11:'Not',12:'LT',13:'GT',14:'ULT',15:'UGT',
          16:'SLT',17:'SGT',18:'EQ',19:'SHL',20:'SRA',21:'MuxBool',22:'MuxN',
          23:'Sext',24:'GetMask',25:'SetMask',26:'MemRead',27:'MemWrite',
          28:'MemWriteBE'}

# ops this predictor has been validated against (the rt_intpipe_alu mix)
VALIDATED = {1,7,8,9,10,11,14,16,18,19,20,21,22,23,24}


def parse(path):
    toks = open(path).read().split()
    if not toks or toks[0] != 'DCERT1':
        raise SystemExit(f'{path}: bad magic')
    p = [1]

    def nxt():
        v = int(toks[p[0]]); p[0] += 1; return v

    srcs = []                      # (kind, vector-chain cost, detail)
    for _ in range(nxt()):
        t = nxt()
        if t == 0:                             # input idx w
            idx, w = nxt(), nxt()
            srcs.append(('input', idx, idx))
        elif t == 1:                           # const w v
            nxt(); nxt()
            srcs.append(('const', 0, None))
        elif t == 2:                           # flopQ idx w
            idx, w = nxt(), nxt()
            srcs.append(('flopQ', idx, idx))
        elif t == 3:                           # flopQAsync idx w ri rv al
            idx, w, ri, rv, al = nxt(), nxt(), nxt(), nxt(), nxt()
            srcs.append(('flopQAsync', ri + idx, (ri, idx)))
        elif t == 4:                           # memImg idx aw dw
            nxt(); nxt(); nxt()
            srcs.append(('memImg', 0, None))
        elif t == 5:                           # memConst aw dw n cs...
            nxt(); nxt()
            for _ in range(nxt()): nxt()
            srcs.append(('memConst', 0, None))
        else:
            raise SystemExit(f'{path}: unknown source tag {t}')
    nodes = []
    for _ in range(nxt()):
        c, _arg, _w = nxt(), nxt(), nxt()
        deps = [nxt() for _ in range(nxt())]
        nxt()                                  # origin
        nodes.append((c, deps))
    outs = [(nxt(), nxt()) for _ in range(nxt())]
    nflop = nxt()
    for _ in range(nflop):                     # w din he e hr r rv al
        for _ in range(8): nxt()
    return srcs, nodes, outs, nflop


def costs(path):
    """(nsrc, nnode, chainA, chainB, chainC) -- the numbers --summary tabulates."""
    srcs, nodes, outs, nflop = parse(path)
    ns = len(srcs)
    a = sum(c for _, c, _ in srcs)
    b = 0
    for i, (_c, deps) in enumerate(nodes):
        envlen = ns + i
        for d in deps:
            if 0 <= d < min(ns, envlen):
                b += ns - 1 - d
    # (C) the flop hold-vector chain.  `flopNexts` walks the flops array with a
    # position counter and `flopNext` reads `nthD fq <position>`, so this is a
    # sum over POSITIONS, one per fq read.  `flopD` residualises 2x this, so
    # (C) is per-read and therefore a LOWER bound on the flop chain.
    c = nflop * (nflop - 1) // 2
    return ns, len(nodes), a, b, c


def summary(paths):
    print(f'{"design":<38} {"src":>7} {"node":>7} {"(A) inp":>10} '
          f'{"(B) slot":>14} {"(C) flop":>9} {"before":>14} {"after":>10}')
    rows = []
    for f in paths:
        try:
            ns, nn, a, b, c = costs(f)
        except SystemExit:
            continue
        rows.append((a + b, f.split('/')[-1], ns, nn, a, b, c))
    for tot, nm, ns, nn, a, b, c in sorted(rows):
        print(f'{nm:<38} {ns:7,} {nn:7,} {a:10,} {b:14,} {c:9,} '
              f'{a+b:14,} {a+c:10,}')
    print('\n"before" = (A)+(B), the reference interpreter.  "after" = (A)+(C), '
          'what remains\nonce the env0 rewrite removes (B).  Both are PREDICTIONS '
          'from the model in\nPHASE6_PERF.md section 7.3, validated on rt_intpipe_alu '
          'to 0.015% and on 26\nsynthetic points exactly.  (C) is a lower bound.')


def report(path, show_ops):
    srcs, nodes, outs, nflop = parse(path)
    ns, nn = len(srcs), len(nodes)
    kinds = collections.Counter(k for k, _, _ in srcs)
    print(f'{path}')
    print(f'  sources {ns}  nodes {nn}  outputs {len(outs)}  flops {nflop}')
    print(f'  source kinds: {dict(kinds)}')

    # ---- (A) runtime vector chain: SUM OF ACTUAL INDICES, per declaration ----
    chainA = sum(c for _, c, _ in srcs)
    byk = collections.Counter()
    for k, c, _ in srcs:
        byk[k] += c
    idxs = sorted(d for k, _, d in srcs if k in ('input', 'flopQ') and d is not None)
    print(f'  (A) runtime-vector chain {chainA:,} tl steps   {dict(byk)}')
    if idxs:
        dense = idxs == list(range(len(idxs)))
        print(f'      indices {idxs[:8]}{"..." if len(idxs) > 8 else ""}'
              f'  max {idxs[-1]}  distinct {len(set(idxs))}/{len(idxs)}'
              f'  {"dense 0..m-1" if dense else "SPARSE/REPEATED -- m(m-1)/2 would be wrong"}')

    # ---- (B) source-prefix slot chain -------------------------------------
    chainB = 0
    bysrc = collections.Counter()
    tgt = collections.Counter()
    oor = 0
    for i, (_c, deps) in enumerate(nodes):
        envlen = ns + i                        # entries pushed before this node
        for d in deps:
            if d < 0 or d >= envlen:
                oor += 1
                continue
            if d < ns:
                tgt[srcs[d][0]] += 1
                chainB += ns - 1 - d
                bysrc[srcs[d][0]] += ns - 1 - d
            else:
                tgt['node'] += 1
    print(f'  (B) source-prefix chain  {chainB:,} tl steps   {dict(bysrc)}')
    print(f'      dep targets {dict(tgt)}')
    if oor:
        print(f'      !! {oor} OUT-OF-RANGE deps (slot >= env length at that node)')
    cC = nflop * (nflop - 1) // 2
    print(f'  (C) flop hold chain      {cC:,} tl steps per fq read (flopD shows 2)')
    print(f'  PREDICTED residual tl = {chainA + chainB:,}'
          f'   (without (B): {chainA + cC:,}+)')

    unval = {c for c, _ in nodes} - VALIDATED
    if unval:
        print(f'  !! ops outside the validated set: '
              f'{sorted(OPNAME.get(c, c) for c in unval)} -- prediction unchecked')
    if kinds['memImg'] or kinds['memConst']:
        print(f'  !! memory sources present: unsupported in I_hw, counted free')
    if show_ops:
        h = collections.Counter(OPNAME.get(c, c) for c, _ in nodes)
        print(f'  ops: {h.most_common()}')
        print(f'  arities: {collections.Counter(len(d) for _, d in nodes).most_common()}')


if __name__ == '__main__':
    flags = {a for a in sys.argv[1:] if a.startswith('--')}
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        raise SystemExit(__doc__)
    if '--summary' in flags:
        summary(args)
    else:
        for f in args:
            report(f, '--ops' in flags)
