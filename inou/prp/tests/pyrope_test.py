#!/usr/bin/env python3

import argparse
import glob
import os
import sys

from prplib import PrpTest, PrpRunner

parser = argparse.ArgumentParser()
# `-i` may be repeated: one target then runs several fixtures back to back (the
# `prp-errors-<group>` targets of `_ERR_MERGED` in inou/prp/BUILD). Every fixture
# still prints its own `<name> - <mode> - success/FAILED` line and uses its own
# scratch dirs; one failure does not stop the later fixtures, but fails the run.
parser.add_argument('-i', '--input', action='append', help='pyrope file to test (repeatable)', required=True)
# `--mode` OVERRIDES the fixture's `:type:` header. One fixture can then be run
# two ways from two targets — which is the whole point of the verilator
# differential: `prp-sim-<x>` runs it under `lhd sim` (and may be a known
# failure) while `prp-vsim-<x>` runs the SAME source under verilator and must
# pass. The two disagreeing IS the test result.
parser.add_argument('--mode', help='override the fixture\'s :type: (e.g. vsim)', default=None)

parser.add_argument('--bitfuzz', action='store_true', help='strip internal width/sign annotations before cprop in equivalence flows')

# One target covers both legs of a fixture that is also checked under bitfuzz:
# the plain run first, then the same fixture again with `--bitfuzz`. Each leg
# starts from freshly removed scratch dirs, so the fuzzed leg never reuses a
# plain-leg artifact.
parser.add_argument('--with-bitfuzz', action='store_true', help='after the plain run passes, rerun the same fixture with --bitfuzz')

# The e-graph constant-propagation engine (pass.aegraph) for the design under
# test only: the reference side and any golden Verilog stay on the classic engine.
# Same as LHD_PRP_COMPILE_SETS=compile.cprop.engine=aegraph (see prplib.py).
parser.add_argument('--aegraph', action='store_true', help='compile the design under test with compile.cprop.engine=aegraph')

args = parser.parse_args()


def load_test(path):
    test = PrpTest(path)
    if args.mode:
        test.params['type'] = [args.mode]
    return test


runner = PrpRunner(bitfuzz=args.bitfuzz, aegraph=args.aegraph)
rc = 0
for path in args.input:
    test = load_test(path)
    fixture_rc = runner.run(os.getcwd(), test)

    if not fixture_rc and args.with_bitfuzz:
        test = load_test(path)
        print('{} - rerun with --bitfuzz'.format(test.params['name']))
        fixture_rc = PrpRunner(bitfuzz=True, aegraph=args.aegraph).run(os.getcwd(), test)

    rc |= fixture_rc

if rc:
    sys.exit(1)
