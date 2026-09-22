// Behavioral oracle: the 72-bit reset must be ASYNCHRONOUS (it fires while the
// clock is parked) and must carry both slices intact, including the 8 bits
// above 63 that a uint64 accumulator could not hold.
module tb;
  localparam [71:0] RST_VAL = {36'hFFFFFFFFF, 36'h123456789};
  reg         clk = 0, rst = 0;
  reg  [71:0] d = 72'h0;
  wire [71:0] q;
  async_reset_wide_slices dut(.*);

  initial begin
    d = {36'h3, 36'h4};
    #1 clk = 1; #1 clk = 0;          // load the data arm first
    #1;
    if (q !== d)
      $fatal(1, "clocked arm mismatch: q=%h d=%h", q, d);
    // Assert with the clock parked low: a SYNCHRONOUS reset cannot fire here.
    #1 rst = 1;
    #1;
    if (q !== RST_VAL)
      $fatal(1, "reset did not fire off the clock edge (demoted to synchronous), or lost bits past 63: q=%h want %h", q, RST_VAL);
    #1 clk = 1; #1;
    if (q !== RST_VAL)
      $fatal(1, "reset did not hold over a clock edge: q=%h", q);
    clk = 0;
    #1 rst = 0;
    d = {36'h5, 36'h6};
    #1 clk = 1; #1;
    if (q !== d)
      $fatal(1, "clocked arm after release: q=%h d=%h", q, d);
    $display("wide asynchronous reset slices kept their asynchronous reset");
    $finish;
  end
endmodule
