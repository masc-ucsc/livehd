#!/usr/bin/env python3
"""transport.py -- the jump: B's state at cycle N from A's run, with NO cycle of B simulated.

Reads only:
  * A's states: one lhdcert-ckpt v1 file per cycle, <states>/<j>.ckpt for j = 0..N (written by
    cert_trace's ckpt-at on A's certificate; pass/lean/tools/formats/ckpt.md), with flops and the
    memory named through A's _io.json;
  * the testbench's inputs (the stim v1 file cert_trace reads; input ordinals named by A's _io.json).
Runs the monitor over cycles 0..N-1 (h_{j+1} = mon(h_j, s_j, i_j), from A's state s_j and input
i_j only) and writes, for each requested N, B's state R(s_N, h_N) as a folder in the layout
`lhd_ckpt import-lhd` reads (regs.json `<inst>.<flop>`, `<inst>.arr.hex` $readmemh words,
meta.json `cycle`), to be imported against B's certificate.

The monitor and R are the ones in gen.py's product designs, restated over integers. The monitor
m_flg is set when a write stores 0xFF and never cleared, so after cycle j it holds B's flg.
  product        R: B's flg = m_flg; acc, cnt and arr as A's.
  product_wrong  the same R, with a monitor whose flag a write of any other value clears (the
                 control).
Nothing here evaluates B's step or reads any file of B. A's own step, restated, is checked
against A's next saved state at every cycle (the inputs are read right).

usage: transport.py <product|product_wrong> <A states dir> <A _io.json> <stim> <N,N,...> <out dir>
Writes <out dir>/ckp<N>/ and <out dir>/transport.json (counters, the monitor at every cycle).
"""
import json, os, sys

W = 256     # U8
INST = 't'  # the folder's instance prefix (import-lhd inst:t)

def read_ckpt_v1(path):
    """lhdcert-ckpt v1 (Compiler/Ckpt.lean's render): flops and memories by ordinal."""
    lines = open(path).read().split('\n')
    assert lines[0] == 'lhdcert-ckpt v1', (path, lines[0])
    i, out = 1, {'flops': [], 'mems': []}
    while i < len(lines):
        t = lines[i].split()
        if not t:
            i += 1
            continue
        if t[0] in ('design', 'cycle'):
            out[t[0]] = t[1]
        elif t[0] == 'flops':
            n = int(t[1])
            out['flops'] = [int(lines[i + 1 + k].split()[1], 16) for k in range(n)]
            i += n
        elif t[0] == 'mem':
            depth, dflt, k = int(t[3]), int(t[4], 16), int(t[5])
            words = {}
            for r in range(k):
                a, v = lines[i + 1 + r].split()
                words[int(a, 16)] = int(v, 16)
            out['mems'].append([words.get(a, dflt) for a in range(depth)])
            i += k
        elif t[0] == 'digest':
            break
        i += 1
    return out

def io_names(io_path):
    io = json.load(open(io_path))
    flops = {f['ordinal']: f['name'] for f in io.get('flops', [])}
    ins = {p['ordinal']: p['name'] for p in io.get('inputs', [])}
    mems = {m['ordinal']: (m.get('inst') or m['name']) for m in io.get('memories', [])}
    return flops, ins, mems

def read_stim(path, ins_by_ord, ncycles):
    """stim v1: `in <ord> <w> <v>` every cycle, `at <c> in <ord> <w> <v>` that cycle wins;
    unlisted inputs are 0. Returns, per cycle 0..ncycles-1, a dict input name -> value."""
    lines = open(path).read().split('\n')
    assert lines[0] == 'stim v1', lines[0]
    every, at = {}, {}
    for l in lines[1:]:
        t = l.split()
        if not t:
            continue
        if t[0] == 'in':
            every[int(t[1])] = int(t[3]) % (1 << int(t[2]))
        elif t[0] == 'at' and t[2] == 'in':
            at.setdefault(int(t[1]), {})[int(t[3])] = int(t[5]) % (1 << int(t[4]))
        else:
            raise SystemExit(f'{path}: unparseable {l!r}')
    cyc = []
    for c in range(ncycles):
        v = {nm: every.get(o, 0) for o, nm in ins_by_ord.items()}
        for o, x in at.get(c, {}).items():
            v[ins_by_ord[o]] = x
        cyc.append(v)
    return cyc

# ---- the monitor and the relations (the same functions as gen.py's product designs) --------
def mon(h, s, i):                              # if we and (din == 0xFF) { m_flg = true }
    return {'m_flg': 1 if i['we'] and i['din'] == 0xFF else h['m_flg']}

def mon_wrong(h, s, i):                        # the control: if we { m_flg = (din == 0xFF) }
    return {'m_flg': int(i['din'] == 0xFF) if i['we'] else h['m_flg']}

def R(s, h):                                   # B's flg = m_flg; acc, cnt and arr as A's
    return {'acc': s['acc'], 'cnt': s['cnt'], 'flg': h['m_flg']}, s['arr']

RELATIONS = {'product': (mon, R), 'product_wrong': (mon_wrong, R)}
H0 = {'m_flg': 0}

def main():
    if len(sys.argv) != 7:
        raise SystemExit(__doc__)
    rel, states, a_io, stim, ns, out = sys.argv[1:]
    mfun, rfun = RELATIONS[rel]
    at = sorted(int(x) for x in ns.split(','))
    nmax = at[-1]
    flops, ins, mems = io_names(a_io)
    assert sorted(flops.values()) == ['acc', 'cnt'], flops
    assert list(mems.values()) == ['arr'], mems
    inputs = read_stim(stim, ins, nmax)
    counters = {'A_states_read': 0, 'monitor_steps': 0, 'R_evaluations': 0,
                'B_steps_simulated': 0, 'next_state_crosschecks': 0}

    def state_at(j):
        c = read_ckpt_v1(os.path.join(states, f'{j}.ckpt'))
        assert int(c['cycle']) == j, (j, c['cycle'])
        s = {flops[k]: v for k, v in enumerate(c['flops'])}
        s['arr'] = c['mems'][0]
        counters['A_states_read'] += 1
        return s

    h, prev = dict(H0), state_at(0)
    trace = [{'cycle': 0, 'h': dict(h)}]
    written = []
    for j in range(nmax):
        i = inputs[j]
        h = mfun(h, prev, i)                       # h_{j+1} = mon(h_j, s_j, i_j)
        counters['monitor_steps'] += 1
        cur = state_at(j + 1)
        # A's own step, restated, gives A's next saved state (the inputs are read right; the
        # testbench writes words 0..11 only)
        want = {'acc': (prev['acc'] + i['din']) % W if i['en'] else prev['acc'], 'cnt': (prev['cnt'] + 1) % W,
                'arr': [i['din'] if i['we'] and a == i['wa'] else v for a, v in enumerate(prev['arr'])]}
        assert all(want[k] == cur[k] for k in want), (j, i, want, cur)
        counters['next_state_crosschecks'] += 1
        trace.append({'cycle': j + 1, 'h': dict(h)})
        if j + 1 in at:
            fl, arr = rfun(cur, h)
            counters['R_evaluations'] += 1
            od = os.path.join(out, f'ckp{j + 1}')
            os.makedirs(od, exist_ok=True)
            with open(os.path.join(od, 'regs.json'), 'w') as f:
                json.dump({f'{INST}.{nm}': hex(v) for nm, v in fl.items()}, f, indent=1, sort_keys=True)
            with open(os.path.join(od, f'{INST}.arr.hex'), 'w') as f:
                f.write(''.join(f'{w:02x}\n' for w in arr))
            with open(os.path.join(od, 'meta.json'), 'w') as f:
                json.dump({'cycle': str(j + 1)}, f)   # import-lhd reads string values only
            written.append({'cycle': j + 1, 'dir': od, 'A_state': {k: cur[k] for k in ('acc', 'cnt')},
                            'A_arr': cur['arr'], 'h': dict(h), 'B_flops': fl, 'B_arr': arr})
        prev = cur
    rep = {'relation': rel, 'points': at, 'counters': counters, 'written': written, 'monitor_trace': trace}
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'transport.json'), 'w') as f:
        json.dump(rep, f, indent=1)
    if counters['monitor_steps'] == 0 or counters['R_evaluations'] != len(at):
        raise SystemExit(f'FAIL: vacuous transport {counters}')
    print(f"transport {rel}: points {at}; counters {json.dumps(counters, sort_keys=True)}")
    print(f"transport {rel}: m_flg at cycles {at}: {[w['h']['m_flg'] for w in written]}")
    for w in written:
        print(f"  cycle {w['cycle']}: A {w['A_state']} m_flg {w['h']['m_flg']} -> B {w['B_flops']}")

if __name__ == '__main__':
    main()
