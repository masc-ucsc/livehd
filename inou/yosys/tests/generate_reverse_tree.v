// Concurrent element drivers must be ordered by the elements they read,
// independent of generate iteration order and nonzero array bounds.
module generate_reverse_tree(input [31:0] a, output [9:0] y);
  wire [9:0] nodes [1:7];
  for (genvar i = 0; i < 4; i = i + 1) begin : leaves
    assign nodes[4+i] = {2'b0, a[8*i +: 8]};
  end
  for (genvar i = 1; i < 4; i = i + 1) begin : root_first
    assign nodes[i] = nodes[2*i] + nodes[2*i+1];
  end
  assign y = nodes[1];
endmodule
