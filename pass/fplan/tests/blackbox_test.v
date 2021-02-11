//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// simple_hier_test, but with a black box module

module blackbox(input [15:0] x, output [15:0] y);
endmodule

module leaf1(input [15:0] x, output [15:0] y);
  assign y = (x == 16'h0) ? x : 16'h3;
endmodule

module leaf2(input [15:0] x, input [15:0] y, output [15:0] sum);
  assign sum = x + y;
endmodule

module blackbox_test(input [15:0] testi, output [15:0] testo);
  wire [15:0] w1;
  wire [15:0] w2;
  wire [15:0] w3;

  wire [15:0] wb1;
  wire [15:0] wb2;

  leaf1 l1(.x(testi), .y(w1));
  leaf2 l2(.x(testi), .y(testi), .sum(w2));
  leaf1 l3(.x(w1), .y(w3));

  blackbox b1(.x(testi), .y(wb1));
  blackbox b2(.x(testi), .y(wb2));

  assign testo = w1 | w2 | w3 & wb1 & wb2;

endmodule