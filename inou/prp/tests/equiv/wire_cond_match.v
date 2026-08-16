module \wire_cond_match.wcm (input [4:0] a, input [4:0] b, input [1:0] sel, input upd, output reg [4:0] o);
  always_comb begin
    o = 5'd0;
    if (upd) begin
      case (sel)
        2'd0:    o = a;
        2'd1:    o = b;
        default: o = 5'd0;
      endcase
    end
  end
endmodule
