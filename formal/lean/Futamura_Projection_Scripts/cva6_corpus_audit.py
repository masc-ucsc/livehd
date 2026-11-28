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

    python3 Futamura_Projection_Scripts/cva6_corpus_audit.py \
        --frozen  <d4>/pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/cva6_30.list \
        --corpus  <d4>/temp/cva6_30_auth/blocks \
        --corpus  <d4>/temp/cva6_30_auth2/blocks \
        [--json out.json]
"""
import argparse, glob, hashlib, json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..',
                                'pass', 'lean', 'Lgraph_IR_Semantics_Simulator', 'tests', 'd4'))
import dcert_delta  # noqa: E402  the one Python DCERT reader


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            h.update(b)
    return h.hexdigest()


def parse(path):
    """sources, nodes count, outputs count, flops of a DCERT4 certificate, read by
    `dcert_delta.read_cert` (it refuses every other version: a ValueError here).
    Written for DCERT1 (fp history): the frozen corpus it audited was DCERT1."""
    try:
        c = dcert_delta.read_cert(path)
    except dcert_delta.Refused as e:
        raise ValueError(str(e))
    srcs = []
    for _tag, kind, f in c['sources']:
        if kind == 'memConst':             # aw dw n cs...
            srcs.append(('memConst', f[0], f[1], f[2]))
        else:
            srcs.append((kind, *f))
    flops = [dict(zip('width din hasEn en hasRst rst rstVal activeLow'.split(), f[:8]))
             for f in c['flops']]
    return srcs, len(c['nodes']), len(c['outputs']), flops


def reset_audit(srcs, flops):
    """Per-FLOP reset consistency.

    CANDIDATE CONFLICT SCAN, and the limits matter.  An earlier version
    compared two SETS of (input, activeLow) pairs gathered across the whole
    design, which can report a conflict for pins that never meet on the same
    flop, and silently labelled an unresolved reset slot `ok`.  This matches by
    FLOP IDENTITY instead: `flopQAsync` source *i* describes the state of flop
    *i*, so it is compared against `FlopDesc` row *i* and nothing else.

    Still NOT a proof of intended RTL semantics.  It only says whether the two
    places a certificate records a reset polarity agree, per flop.

    Returns (per_flop, summary) where each per_flop entry is one of
    'agree' / 'CONFLICT' / 'unknown:<why>' / 'no-async-source' / 'no-flop-reset'.
    """
    async_by_idx = {}
    for s in srcs:
        if s[0] == 'flopQAsync':          # (kind, idx, w, ri, rv, activeLow)
            async_by_idx[s[1]] = {'reset_input': s[3], 'active_low': s[5]}

    per_flop = []
    for i, f in enumerate(flops):
        src = async_by_idx.get(i)
        if src is None:
            per_flop.append({'flop': i, 'verdict': 'no-async-source'})
            continue
        if not f['hasRst']:
            per_flop.append({'flop': i, 'verdict': 'no-flop-reset',
                             'src_active_low': src['active_low']})
            continue
        sl = f['rst']
        if sl >= len(srcs):
            per_flop.append({'flop': i, 'verdict': 'unknown:reset-slot-out-of-range',
                             'slot': sl})
            continue
        if srcs[sl][0] != 'input':
            # the reset is driven by a node or a constant: the two sides are not
            # describing the same thing in a form this scan can compare
            per_flop.append({'flop': i,
                             'verdict': f'unknown:reset-slot-is-{srcs[sl][0]}',
                             'slot': sl})
            continue
        fin = srcs[sl][1]
        same_pin = (fin == src['reset_input'])
        same_pol = (f['activeLow'] == src['active_low'])
        if not same_pin:
            per_flop.append({'flop': i, 'verdict': 'unknown:different-reset-pin',
                             'src_input': src['reset_input'], 'flop_input': fin})
        elif not same_pol:
            per_flop.append({'flop': i, 'verdict': 'CONFLICT', 'input': fin,
                             'src_active_low': src['active_low'],
                             'flop_active_low': f['activeLow']})
        else:
            per_flop.append({'flop': i, 'verdict': 'agree', 'input': fin,
                             'active_low': f['activeLow']})

    summary = {}
    for e in per_flop:
        k = e['verdict'].split(':')[0]
        summary[k] = summary.get(k, 0) + 1
    return per_flop, summary


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
        per_flop, rsum = reset_audit(srcs, flops)
        kinds = {}
        for s in srcs: kinds[s[0]] = kinds.get(s[0], 0) + 1
        rows.append({'block': n, 'path': p, 'sha256': sha256(p),
                     'bytes': os.path.getsize(p), 'sources': len(srcs),
                     'nodes': nn, 'outputs': no, 'flops': len(flops),
                     'source_kinds': kinds, 'sequential': len(flops) > 0,
                     'reset_per_flop': per_flop, 'reset_summary': rsum})

    w = max((len(r['block']) for r in rows), default=10)
    print(f'frozen names: {len(names)}   with a certificate: {len(rows)}   missing: {len(missing)}')
    if missing: print('  MISSING: ' + ', '.join(missing))
    print()
    print(f'{"block":{w}} {"sources":>8} {"nodes":>8} {"flops":>6}  per-flop reset verdicts')
    for r in sorted(rows, key=lambda x: x['nodes']):
        if not r['sequential']:
            v = 'combinational (no flops; this scan does not apply)'
        else:
            v = ', '.join(f'{k}={n}' for k, n in sorted(r['reset_summary'].items()))
        print(f'{r["block"]:{w}} {r["sources"]:8,} {r["nodes"]:8,} {r["flops"]:6}  {v}')
    seq = [r for r in rows if r['sequential']]
    bad = [r for r in seq if r['reset_summary'].get('CONFLICT')]
    unk = [r for r in seq if r['reset_summary'].get('unknown')]
    allc = [r for r in bad if r['reset_summary']['CONFLICT'] == r['flops']]
    print()
    print(f'sequential: {len(seq)}   any CONFLICT: {len(bad)}   '
          f'CONFLICT on EVERY flop: {len(allc)}   any unknown: {len(unk)}   '
          f'combinational: {len(rows) - len(seq)}')
    print('This is a CANDIDATE CONFLICT SCAN matched by flop identity.  It says')
    print('whether a certificate\'s two records of a reset polarity agree; it is')
    print('NOT a claim about intended RTL semantics.')
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
