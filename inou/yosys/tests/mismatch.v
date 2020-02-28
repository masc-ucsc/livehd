module mismatch(input [3:0] a, output [4:0] b);

  wire [2:0] tmp;

  assign tmp = ^a;

  assign b = a[3]? tmp : {6'b0};

endmodule

