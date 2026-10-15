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
