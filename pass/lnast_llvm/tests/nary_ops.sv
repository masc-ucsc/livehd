module nary_ops(input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d, input [7:0] e, input signed [7:0] s,
            output [7:0] o, output [7:0] x, output [7:0] n, output signed [7:0] m);
  assign o = a | b | c | d | e;
  assign x = a ^ b ^ c ^ d;
  assign n = a & b & c & d & e;
  assign m = s | b | c | d;
endmodule
