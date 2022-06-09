module RenameMapTableSimpleF5(
  input        clock,
  input        reset,
  output [5:0] io_map_resps_0_prs1,
  input        io_remap_reqs_0_valid
);
  assign io_map_resps_0_prs1 = {{5'd0}, io_remap_reqs_0_valid};
endmodule
