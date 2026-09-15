module latch_match_partial(input [1:0] sel, input [3:0] a, b, output [3:0] q);
  reg [3:0] state;
  assign q = state;
  always_latch begin
    case (sel)
      2'd0: state <= a;
      2'd1: state <= b;
      default: ;
    endcase
  end
endmodule
