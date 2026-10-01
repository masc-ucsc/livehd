#!/usr/bin/env python3
"""Operator census over the CORE-ET certificates, and what `I_hw` can reach.

WHAT THIS MEASURES, precisely, because the headline number invites a stronger
reading than it earns:

  * It is STATIC OPERATOR REACHABILITY.  For each module it collects the set of
    LGraph operators appearing in that module's CERTIFICATE node table, and
    reports a module as "reachable" when every one of those operators has an
    implementation in `I_hw`.
  * It is NOT execution.  Nothing here projects, runs or compares a simulator.
    "reachable 65/65" means no module contains an operator `I_hw` lacks -- it
    does NOT mean 65 designs were simulated, nor that any was.  Running them is
    Phase 6 of the plan, and the first external milestone there is 30 CVA6
    blocks through the projected simulator with recorded execution and
    comparison against the reference semantics.
  * The population is the READY SUBSET of the census, not every generated
    module.  See the filter below.

INPUTS

  --generated   directory of generated CORE-ET models, one subdir per module,
                each holding `<mod>/lean/<mod>_Lgraph.lean`
                (default: /mada/users/czeng14/projects/livehd-new/generated/core-et)
  <generated>/coreet_census.tsv
                tab-separated, with a `module` and a `verdict` column.
                FILTER: verdict == "READY".

  Operators are taken from the certificate's node table only -- the text of
  `def <mod>_nodesTree : BT NodeCert := ...` up to the next `def` -- matching
  `op := LGraphOp.<Op_X>`.  The FAST model in the same file is deliberately not
  scanned: it is not what `interpretDesign` runs.

  The IMPLEMENTED set is not hardcoded.  It is read back out of
  `Projection/HardwareInterpreter.lean`, from the operator-code constants
  `applyOp` dispatches on, so this script cannot drift from the interpreter.

CAVEATS seen in the data

  * Modules whose certificate node table is empty (`BT.lf`) contribute no
    operators and are excluded; there were two at the time of writing
    (`minion_dcache_texsend`, `null_vpu`).
  * A handful of modules carry a non-empty certificate without being READY;
    they are excluded by the filter.  Including them adds `Op_Mult`.
"""
import argparse, collections, csv, os, re, sys

OP_IN_CERT = re.compile(r'op := LGraphOp\.(Op_[A-Za-z0-9_]+)')
NODES_TREE = re.compile(r'def \w+_nodesTree : BT NodeCert :=(.*?)\ndef ', re.S)
IMPL_CODE  = re.compile(r'opCode \(?\.(Op_[A-Za-z0-9_]+)')

def implemented_ops(hw_path):
    try:
        txt = open(hw_path, errors='replace').read()
    except OSError as e:
        sys.exit(f"cannot read {hw_path}: {e}")
    ops = set(IMPL_CODE.findall(txt))
    if not ops:
        sys.exit(f"no operator codes found in {hw_path}; has applyOp changed shape?")
    return ops

def census(generated):
    tsv = os.path.join(generated, 'coreet_census.tsv')
    try:
        with open(tsv, newline='') as fh:
            ready = {r['module'] for r in csv.DictReader(fh, delimiter='\t')
                     if r.get('verdict') == 'READY'}
    except OSError as e:
        sys.exit(f"cannot read {tsv}: {e}")
    sets, empty = {}, []
    for mod in sorted(ready):
        path = os.path.join(generated, mod, 'lean', f'{mod}_Lgraph.lean')
        if not os.path.exists(path):
            continue
        m = NODES_TREE.search(open(path, errors='replace').read())
        ops = set(OP_IN_CERT.findall(m.group(1))) if m else set()
        (sets.setdefault(mod, ops) if ops else empty.append(mod))
    return ready, sets, empty

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--generated',
                    default='/mada/users/czeng14/projects/livehd-new/generated/core-et')
    ap.add_argument('--interpreter',
                    default=os.path.join(here, '..', 'HardwareInterpreter.lean'))
    a = ap.parse_args()

    impl = implemented_ops(a.interpreter)
    ready, sets, empty = census(a.generated)
    allops = {o for s in sets.values() for o in s}

    print(f"generated dir      : {a.generated}")
    print(f"filter             : coreet_census.tsv verdict == READY")
    print(f"READY modules      : {len(ready)}")
    print(f"  with a non-empty certificate node table : {len(sets)}")
    print(f"  with an EMPTY ({'BT.lf'}) node table      : {len(empty)} {empty}")
    print(f"distinct operators : {len(allops)}")
    print()
    print(f"{'operator':<14}{'nodes':>9}{'modules':>9}  implemented")
    nodes, mods = collections.Counter(), collections.Counter()
    for mod in sets:
        path = os.path.join(a.generated, mod, 'lean', f'{mod}_Lgraph.lean')
        m = NODES_TREE.search(open(path, errors='replace').read())
        found = OP_IN_CERT.findall(m.group(1))
        nodes.update(found)
        for o in set(found):
            mods[o] += 1
    for op, c in sorted(nodes.items(), key=lambda kv: -kv[1]):
        print(f"{op:<14}{c:>9}{mods[op]:>9}  {'yes' if op in impl else 'NO'}")
    print()
    reach = [m for m, s in sets.items() if s <= impl]
    print(f"implemented operators : {len(allops & impl)} / {len(allops)}")
    print(f"uncovered operators   : {sorted(allops - impl) or 'NONE'}")
    print(f"STATIC REACHABILITY   : {len(reach)} / {len(sets)}"
          f"  (every operator present has an implementation)")
    print(f"unreachable modules   : {sorted(set(sets) - set(reach)) or 'NONE'}")
    print()
    print("NOT an execution result: no module was projected, run or compared here.")
    return 0 if not (allops - impl) else 1

if __name__ == '__main__':
    sys.exit(main())
