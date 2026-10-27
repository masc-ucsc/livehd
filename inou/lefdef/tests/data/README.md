These fixtures are trimmed from the SkyWater sky130HD PDK installed by ciel,
version e3262351fb1f5a3cc262ced1c76ebe3f2a5218fb, sky130A. Geometry comes from
`sky130_fd_sc_hd.lef`, technology/RC from `sky130_fd_sc_hd__nom.tlef`, and
characterization from `sky130_fd_sc_hd__tt_025C_1v80.lib`. Only inv_1, buf_1,
nand2_1, nor2_1, xor2_1, dfxtp_1 and conb_1 are retained. No dimensions or timing
tables were changed. The original LEF cells also contain their power-pin shapes;
the placement flow exports signal connectivity.

Copyright 2020 The SkyWater PDK Authors. Licensed under Apache-2.0, as described
in the technology LEF header and https://www.apache.org/licenses/LICENSE-2.0.

`inou/slang/tests/verilog/sky130_test.lib` is an HS subset, despite its generic
filename; these HD fixtures therefore carry their own matching Liberty subset.
Tests use only these repository files and never require a local PDK installation.

`sky130hd_seq.lib.fragment` and `sky130hd_seq.lef.fragment` hold five more
sequential HD cells, copied unchanged (cell groups and macros only, not a
library) from the OpenROAD-flow-scripts sky130hd platform files
`sky130_fd_sc_hd__tt_025C_1v80.lib` and `sky130_fd_sc_hd_merged.lef`, which
redistribute the same SkyWater PDK: the integrated clock gates dlclkp_1 and
sdlclkp_1 (with a test enable), the latches dlxtn_1 and dlxtp_1 and the
asynchronous-reset flop dfrtp_1. The lhd smokes append them (all, or only
dfrtp_1 for the placement reset tree) to `sky130hd.lib` / `sky130hd.lef` in
their temporary directory through `lhd/tests/sky130_fixture.py`; the
seven-cell files above stay as they are.
