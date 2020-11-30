module adder_stage (
  input       clock,
  input [2:0] a,
  input [2:0] b,
  output reg [3:0] out
);

wire [3:0] adder_next = $signed({1'b0, a}) + $signed({1'b0, b});

always @ (posedge clock) begin 
  out <= adder_next;
end

endmodule
