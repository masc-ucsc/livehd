module adder_stage (
  input       clk,
  input [2:0] a,
  input [2:0] b,
  output reg [3:0] out
);

wire [3:0] adder_next = a + b;

always @ (posedge clk) begin 
  out <= adder_next;
end

endmodule
