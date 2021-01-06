// verilog file with less hierarchy than hier_test

module leaf1(input [15:0] x, output [15:0] y);
  assign y = (x == 16'h0) ? 16'h5 : 16'h3;
endmodule

module leaf2(input [15:0] x, input [15:0] y, output [15:0] sum);
  assign sum = x + y;
endmodule

module simple_hier_test(input [15:0] testi, output [15:0] testo);
  wire [15:0] w1;
  wire [15:0] w2;
  wire [15:0] w3;

  leaf1 l1(.x(testi), .y(w1));
  leaf2 l2(.x(testi), .y(testi), .sum(w2));
  leaf1 l3(.x(w1), .y(w3));

  assign testo = w1 | w2 | w3;

endmodule