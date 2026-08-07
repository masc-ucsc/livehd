#!/usr/bin/env python3

import argparse
import glob
import os
import sys

from prplib import PrpTest, PrpRunner

parser = argparse.ArgumentParser()
parser.add_argument('-i', '--input', help='pyrope file to test', required=True)
# `--mode` OVERRIDES the fixture's `:type:` header. One fixture can then be run
# two ways from two targets — which is the whole point of the verilator
# differential: `prp-sim-<x>` runs it under `lhd sim` (and may be a known
# failure) while `prp-vsim-<x>` runs the SAME source under verilator and must
# pass. The two disagreeing IS the test result.
parser.add_argument('--mode', help='override the fixture\'s :type: (e.g. vsim)', default=None)

args = parser.parse_args()

test = PrpTest(args.input)
if args.mode:
    test.params['type'] = [args.mode]

runner = PrpRunner()
rc = runner.run(os.getcwd(), test)

if rc:
    sys.exit(1)
