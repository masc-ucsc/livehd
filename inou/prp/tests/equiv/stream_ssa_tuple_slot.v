module stream_ssa_tuple_slot(input [1:0] sel, input [7:0] x, input [7:0] y,
                             output reg [7:0] post_write, output reg [7:0] pre_write);
  always @* begin
    case (sel)
      2'd0:    post_write = 8'd10;
      2'd1:    post_write = 8'd20;
      2'd2:    post_write = y;
      default: post_write = 8'd30;
    endcase
  end
  always @* begin
    case (sel)
      2'd0:    pre_write = 8'd40;
      2'd1:    pre_write = 8'd50;
      2'd2:    pre_write = x;
      default: pre_write = 8'd60;
    endcase
  end
endmodule
