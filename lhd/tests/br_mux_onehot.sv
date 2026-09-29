// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Small Bedrock-style one-hot mux used to pin pass.abc's physical-sizing QoR.
// Five 34-bit lanes are the shape instantiated by br_apb_demux_select_onehot.
module br_mux_onehot (
    input  logic [4:0]       select,
    input  logic [4:0][33:0] in,
    output logic [33:0]      out
);
  always_comb begin
    out = '0;
    for (int i = 0; i < 5; ++i) begin
      out |= ({34{select[i]}} & in[i]);
    end
  end
endmodule
