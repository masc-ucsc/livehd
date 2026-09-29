module \loop_runtime_break_nested.top (
  input      [7:0] stop,
  output     [3:0] total
);
  wire [2:0] low_count = stop[0] ? 3'd0
                       : stop[1] ? 3'd1
                       : stop[2] ? 3'd2
                       : stop[3] ? 3'd3
                                 : 3'd4;
  wire [2:0] high_count = stop[4] ? 3'd0
                        : stop[5] ? 3'd1
                        : stop[6] ? 3'd2
                        : stop[7] ? 3'd3
                                  : 3'd4;
  assign total = low_count + high_count;
endmodule
