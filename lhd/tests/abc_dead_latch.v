module dead_latch(input clk, input a, input b, output reg y);
reg [1:0] st = 0;
reg v = 1'd0;
always @(*) begin
  y <= v;
  case (st)
    2'd1: if (st == 2'd2) v <= b;
    default: ;
  endcase
end
always @(posedge clk) st <= st + a;
endmodule
