// test file with lots of repeating instantiations to test grids of modules

module leaf_grid(input x, output y);
  assign y = ~x;
endmodule

module grid_hier_test(input [15:0] testi, output [15:0] testo);
  leaf_grid lg[15:0](.x(testi), .y(testo));
endmodule