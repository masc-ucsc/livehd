// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// A parameterized leaf with NO default that works: the shape of core-et's
// `txfma_adder` (hw/ip/minion/vpu/rtl/txfma_adder.sv), reduced to what makes it
// unelaborable as a top.
//
// `Width = 0` means the ports are `logic [-1:0]` and slang refuses the module
// outright -- so this fixture is REFUSED without `-G Width=...` and accepted
// with it, which is the whole mechanism lhd/tests/module_params_test.sh exists
// to pin.
//
// It is a COPY of the shape, not of the file: it pins that `-G` reaches slang
// and produces the intended widths. That core-et's own txfma_adder elaborates
// and is ACCEPTED is separate, recorded evidence (pass/lean/MODULE_PARAMS.tsv).
module param_width_leaf #(
  parameter int unsigned Width = 0
) (
  input  wire [Width-1:0] a_i,
  input  wire [Width-1:0] b_i,
  input  wire             cin_i,
  output wire [Width-1:0] sum_o
);
  wire [Width:0] sum_tmp;
  assign sum_tmp = a_i + b_i + {{(Width-1){1'b0}}, cin_i};
  assign sum_o   = sum_tmp[Width-1:0];
endmodule
