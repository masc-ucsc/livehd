//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// test file with lots of repeating instantiations to test grids of modules

module leaf_grid(input x, output y);
  assign y = ~x;
endmodule

module grid_hier_test(input [15:0] testi, output [15:0] testo);
  leaf_grid lg[32:0](.x(testi[5]), .y(testo[0]));
endmodule