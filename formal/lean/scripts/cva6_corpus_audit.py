#!/usr/bin/env python3
"""Reproducible audit of the authoritative CVA6 certificate corpus.

Answers three things with one command, from the certificates alone:

  1. which of the frozen 30 names have a certificate, and its sha256/shape;
  2. the reset-descriptor consistency of every SEQUENTIAL block -- the
     `flopQAsync` sources declare a reset input and polarity, and every
     `FlopDesc` row declares one too, and they must agree;
  3. a machine-readable dump so a reviewer can diff it rather than re-derive.

Support (the six `SupportedByProjection` fields) is NOT computed here -- it is
Lean's, via `proto_probe --support`, and the command is printed so the two are
not conflated.

    python3 scripts/cva6_corpus_audit.py \
        --frozen  <d4>/pass/lean/tests/d4/cva6_30.list \
        --corpus  <d4>/temp/cva6_30_auth/blocks \
        --corpus  <d4>/temp/cva6_30_auth2/blocks \
        [--json out.json]
"""
import argparse, glob, hashlib, json, os, sys

SRC_FIELDS = {0: ('input', 2), 1: ('const', 2), 2: ('flopQ', 2),
              3: ('flopQAsync', 5), 4: ('memImg', 3)}


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def parse(path):
    """sources, nodes count, outputs count, flops -- the whole DCERT1 body."""
    t = open(path).read().split()
    if not t or t[0] != 'DCERT1':
        raise ValueError('bad magic')
    i = [1]

    def nx():
        v = int(t[i[0]]); i[0] += 1; return v

    srcs = []
    for _ in range(nx()):
        tag = nx()
        if tag == 5:                       # memConst: aw dw n cs...
            aw, dw, n = nx(), nx(), nx()
            for _ in range(n): nx()
            srcs.append(('memConst', aw, dw, n))
        elif tag in SRC_FIELDS:
            kind, nf = SRC_FIELDS[tag]
            srcs.append((kind, *[nx() for _ in range(nf)]))
        else:
            raise ValueError(f'unknown source tag {tag}')
    nn = nx()
    for _ in range(nn):
        nx(); nx(); nx()
        for _ in range(nx()): nx()
        nx()
    no = nx()
    for _ in range(no): nx(); nx()
    flops = []
    for _ in range(nx()):
        flops.append(dict(zip('width din hasEn en hasRst rst rstVal activeLow'.split(),
                              [nx() for _ in range(8)])))
    return srcs, nn, no, flops


def reset_audit(srcs, flops):
    """(source-side pins, flop-side pins, conflicts) -- all by input INDEX."""
    src_pins = sorted({(s[3], s[5]) for s in srcs if s[0] == 'flopQAsync'})
    flop_pins = set()
    for f in flops:
        if f['hasRst']:
            sl = f['rst']
            if sl < len(srcs) and srcs[sl][0] == 'input':
                flop_pins.add((srcs[sl][1], f['activeLow']))
            else:
                flop_pins.add((f'slot{sl}', f['activeLow']))
    flop_pins = sorted(flop_pins, key=str)
    conflicts = [(ri, sa, fal) for (ri, sa) in src_pins
                 for (fi, fal) in flop_pins if ri == fi and sa != fal]
    return src_pins, flop_pins, conflicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frozen', required=True)
    ap.add_argument('--corpus', action='append', required=True)
    ap.add_argument('--json')
    a = ap.parse_args()

    names = [n for n in open(a.frozen).read().split() if n]
    found = {}
    for root in a.corpus:
        for p in glob.glob(os.path.join(root, '*', 'lean', '*_gate.dcert')):
            b = os.path.basename(p)[:-len('_gate.dcert')]
            found.setdefault(b, p)

    rows, missing = [], []
    for n in names:
        p = found.get(n)
        if p is None:
            missing.append(n); continue
        srcs, nn, no, flops = parse(p)
        sp, fp, conf = reset_audit(srcs, flops)
        kinds = {}
        for s in srcs: kinds[s[0]] = kinds.get(s[0], 0) + 1
        rows.append({'block': n, 'path': p, 'sha256': sha256(p),
                     'bytes': os.path.getsize(p), 'sources': len(srcs),
                     'nodes': nn, 'outputs': no, 'flops': len(flops),
                     'source_kinds': kinds, 'sequential': len(flops) > 0,
                     'src_reset_pins': sp, 'flop_reset_pins': fp,
                     'reset_conflicts': conf})

    w = max((len(r['block']) for r in rows), default=10)
    print(f'frozen names: {len(names)}   with a certificate: {len(rows)}   missing: {len(missing)}')
    if missing: print('  MISSING: ' + ', '.join(missing))
    print()
    print(f'{"block":{w}} {"sources":>8} {"nodes":>8} {"flops":>6} {"srcRst":>10} {"flopRst":>10}  reset')
    for r in sorted(rows, key=lambda x: x['nodes']):
        s = ','.join(f'{i}:aL{v}' for i, v in r['src_reset_pins']) or '-'
        f = ','.join(f'{i}:aL{v}' for i, v in r['flop_reset_pins']) or '-'
        verdict = ('CONFLICT' if r['reset_conflicts']
                   else ('ok' if r['sequential'] else 'combinational'))
        print(f'{r["block"]:{w}} {r["sources"]:8,} {r["nodes"]:8,} {r["flops"]:6} '
              f'{s:>10} {f:>10}  {verdict}')
    seq = [r for r in rows if r['sequential']]
    bad = [r for r in seq if r['reset_conflicts']]
    print()
    print(f'sequential: {len(seq)}   with reset-polarity CONFLICT: {len(bad)}'
          f'   combinational (defect cannot apply): {len(rows) - len(seq)}')
    print()
    print('sha256 of every certificate audited:')
    for r in sorted(rows, key=lambda x: x['block']):
        print(f'  {r["sha256"]}  {r["block"]}')
    print()
    print('Support is Lean\'s, not this script\'s.  Run:')
    print('  <build>/nat/proto_probe --support <each path above>')
    if a.json:
        json.dump({'frozen_list': a.frozen, 'corpus_roots': a.corpus,
                   'missing': missing, 'blocks': rows},
                  open(a.json, 'w'), indent=1, sort_keys=True)
        print(f'\nmachine-readable dump: {a.json}')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
