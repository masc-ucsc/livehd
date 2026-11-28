#!/usr/bin/env python3
"""Reset-polarity census over DCERT4 certificates.

Read by `dcert_delta.read_cert`, the one Python DCERT reader, which refuses every other
version.  The census this script was written for (fp history) read DCERT1, DCERT2 and DCERT3.

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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
                                'pass', 'lean', 'Lgraph_IR_Semantics_Simulator', 'tests', 'd4'))
import dcert_delta  # noqa: E402  the one Python DCERT reader

FLOP_KEYS = 'width din hasEn en hasRst rst rstVal activeLow clock asyncReset'.split()


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def parse(path):
    """(magic, sources, nNodes, nOutputs, flops) of a DCERT4 certificate, read by
    `dcert_delta.read_cert` (it refuses every other version: a ValueError here)."""
    try:
        c = dcert_delta.read_cert(path)
    except dcert_delta.Refused as e:
        raise ValueError(str(e))
    srcs = []
    for _tag, kind, f in c['sources']:
        if kind == 'memConst':                         # aw dw n cs...
            srcs.append(('memConst', f[0], f[1], f[2]))
        else:
            srcs.append((kind, *f))
    flops = [dict(zip(FLOP_KEYS, f)) for f in c['flops']]
    return c['magic'].decode().strip(), srcs, len(c['nodes']), len(c['outputs']), flops


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
