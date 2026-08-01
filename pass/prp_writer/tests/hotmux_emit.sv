module hotmux_emit (
  input  logic [1:0] sel,
  input  logic [7:0] a,
  input  logic [7:0] b,
  input  logic [7:0] c,
  input  logic [7:0] d,
  output logic [7:0] out
);
  always_comb begin
    unique case (sel)
      2'd0: out = a;
      2'd1: out = b;
      2'd2: out = c;
      default: out = d;
    endcase
  end
endmodule
