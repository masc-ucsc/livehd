// Flat top for the pass.color + pass.partition LEC test (2c-color / 2p-partition).
// Several fan-out points + two output cones give the coloring multiple regions,
// so partition produces several modules wired through a new top.
module part_flat(input [7:0] a, input [7:0] b, input [7:0] c, output [7:0] y, output [7:0] z);
  wire [7:0] s = a + b;   // fan-out 2 (feeds the y-cone and the z-cone)
  wire [7:0] t = b ^ c;
  assign y = s & t;
  assign z = s ^ c;
endmodule
