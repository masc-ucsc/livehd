#!/usr/bin/env python3
"""Reset-polarity census over DCERT1 and DCERT2 certificates.

`DirectCheck.lean` declines to cross-check reset polarity, and states why:

    an async flop's `resetPin` slot and its `resetInput` ordinal legitimately
    differ in POLARITY -- the census shows 9,262 async sources whose
    `activeLow` is the opposite of their `FlopDesc.resetActiveLow`, because the
    pin slot reads an already-inverted node while the ordinal reads the raw
    port.

That justification is CONDITIONAL on the pin slot being a node.  This script
makes the condition observable, because it is the whole difference between a
legitimate opposition and an inconsistent certificate:

  node-mediated   `FlopDesc.resetPin` resolves to a NODE slot.  The two sides
                  read different signals, so opposite polarities are expected
                  and nothing here is comparable.  This is the case the comment
                  describes.

  raw-port        `FlopDesc.resetPin` resolves to the SAME `.input` ordinal the
                  `flopQAsync` source names.  Both sides read one port, so
                  opposite polarities mean no value of that port leaves both
                  paths out of reset.  The comment's reason does not apply.

NOT a claim about intended RTL semantics, and not a projection result.  It reads
the two places a certificate records a reset polarity and reports whether, and
how, they can be compared.

    python3 Futamura_Projection_Scripts/reset_polarity_census.py [--json out.json] FILE.dcert [...]
    python3 Futamura_Projection_Scripts/reset_polarity_census.py --glob 'dir/**/*.dcert'
"""
import argparse, collections, glob, hashlib, json, os, sys

SRC = {0: ('input', 2), 1: ('const', 2), 2: ('flopQ', 2),
       3: ('flopQAsync', 5), 4: ('memImg', 3)}
FLOP_KEYS = 'width din hasEn en hasRst rst rstVal activeLow clock asyncReset'.split()


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def parse(path):
    """(magic, sources, nNodes, nOutputs, flops).  DCERT1 flops have 8 fields,
    DCERT2 has 10 -- the two extra are `clock` and `asyncReset`."""
    t = open(path).read().split()
    if not t or t[0] not in ('DCERT1', 'DCERT2'):
        raise ValueError(f'{path}: bad magic')
    magic, i = t[0], [1]

    def nx():
        v = int(t[i[0]]); i[0] += 1; return v

    srcs = []
    for _ in range(nx()):
        tag = nx()
        if tag == 5:                                   # memConst aw dw n cs...
            aw, dw, n = nx(), nx(), nx()
            for _ in range(n): nx()
            srcs.append(('memConst', aw, dw, n))
        elif tag in SRC:
            kind, nf = SRC[tag]
            srcs.append((kind, *[nx() for _ in range(nf)]))
        else:
            raise ValueError(f'{path}: unknown source tag {tag}')
    nn = nx()
    for _ in range(nn):
        nx(); nx(); nx()
        for _ in range(nx()): nx()
        nx()
    no = nx()
    for _ in range(no): nx(); nx()
    nf = 10 if magic == 'DCERT2' else 8
    flops = [dict(zip(FLOP_KEYS[:nf], [nx() for _ in range(nf)]))
             for _ in range(nx())]
    return magic, srcs, nn, no, flops


def census(path):
    magic, srcs, nn, no, flops = parse(path)
    ns = len(srcs)
    # `flopQAsync idx w ri rv al` describes the READ side of flop `idx`
    async_by_flop = {s[1]: {'reset_input': s[3], 'active_low': s[5]}
                     for s in srcs if s[0] == 'flopQAsync'}
    rows = []
    for i, f in enumerate(flops):
        src = async_by_flop.get(i)
        if not f['hasRst']:
            rows.append({'flop': i, 'class': 'no-flop-reset',
                         'src_al': None if src is None else src['active_low'],
                         'flop_al': f['activeLow']})
            continue
        sl = f['rst']
        if sl >= ns:
            kind = 'node'
        else:
            kind = srcs[sl][0]
        if src is None:
            cls = f'no-async-source(slot={kind})'
        elif kind == 'node':
            cls = 'node-mediated'
        elif kind == 'input':
            same_pin = srcs[sl][1] == src['reset_input']
            if not same_pin:
                cls = 'raw-port:different-pin'
            elif src['active_low'] != bool(f['activeLow']):
                cls = 'raw-port:OPPOSITE'
            else:
                cls = 'raw-port:agree'
        else:
            cls = f'slot-is-{kind}'
        rows.append({'flop': i, 'class': cls, 'slot': sl, 'slot_kind': kind,
                     'src_al': None if src is None else int(src['active_low']),
                     'flop_al': f['activeLow'],
                     'pin': srcs[sl][1] if kind == 'input' else None})
    return magic, ns, nn, len(flops), rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='*')
    ap.add_argument('--glob', action='append', default=[])
    ap.add_argument('--json')
    a = ap.parse_args()
    paths = list(a.paths)
    for g in a.glob:
        paths += glob.glob(g, recursive=True)
    paths = sorted(set(paths))
    if not paths:
        raise SystemExit(__doc__)

    out, totals = [], collections.Counter()
    w = max(len(os.path.basename(p)) for p in paths)
    print(f'{"certificate":{w}} {"magic":6} {"src":>7} {"flops":>6}  classes')
    for p in paths:
        try:
            magic, ns, nn, nf, rows = census(p)
        except ValueError as e:
            print(f'{os.path.basename(p):{w}} -- SKIPPED: {e}')
            continue
        c = collections.Counter(r['class'] for r in rows)
        totals.update(c)
        print(f'{os.path.basename(p):{w}} {magic:6} {ns:7,} {nf:6}  '
              f'{dict(c) if nf else "combinational"}')
        out.append({'path': p, 'sha256': sha256(p), 'magic': magic,
                    'sources': ns, 'nodes': nn, 'flops': nf,
                    'classes': dict(c), 'per_flop': rows})
    opp = totals['raw-port:OPPOSITE']
    print(f'\ntotals: {dict(totals)}')
    print(f'\nnode-mediated (DirectCheck\'s documented case, not comparable here): '
          f'{totals["node-mediated"]:,}')
    print(f'raw-port OPPOSITE (one port, two polarities -- no value leaves both '
          f'paths out of reset): {opp:,}')
    print('This is a DESCRIPTOR comparison.  It is not a claim about intended RTL')
    print('semantics and no design was projected, run or compared here.')
    if a.json:
        json.dump({'certificates': out, 'totals': dict(totals)},
                  open(a.json, 'w'), indent=1, sort_keys=True)
        print(f'\nmachine-readable dump: {a.json}')
    return 1 if opp else 0


if __name__ == '__main__':
    sys.exit(main())
