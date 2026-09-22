module tb;
  reg clk = 0;
  reg rst = 0;
  reg [79:0] data = 0;
  wire [79:0] wide;
  wire signed [3:0] signed_q;
  wire [2:0] tail, truncated;
  wire [0:7] ascending;
  wire [7:0] sliced;
  wire [4:0] extended;
  async_reset_concat_shapes dut(.*);

  task check_reset;
    begin
      if (wide !== 80'h8123_4567_89ab_cdef_0123 || signed_q !== -4'sd3 || tail !== 3'b101
          || ascending !== 8'h13 || sliced !== 8'h42 || extended !== 5'h1f || truncated !== 3'h7)
        $fatal(1, "concatenated asynchronous reset value mismatch");
    end
  endtask

  initial begin
    // Assert away from a clock edge: a synchronous fallback must fail.
    #2 rst = 1;
    #1 check_reset();
    data = 80'hffff_ffff_ffff_ffff_abcd;
    #1 clk = 1;
    #1 check_reset();
    clk = 0;
    #1 rst = 0;
    #1 check_reset();
    #1 clk = 1;
    #1;
    if (wide !== data || signed_q !== -4'sd3 || tail !== 3'd5
        || ascending !== 8'hac || sliced !== 8'hdb || extended !== 5'h19 || truncated !== 3'd5)
      $fatal(1, "concatenated data assignment mismatch");
    clk = 0;
    data = 0;
    #1 rst = 1;
    #1 check_reset();
    $display("concatenated asynchronous reset checks passed");
    $finish;
  end
endmodule
