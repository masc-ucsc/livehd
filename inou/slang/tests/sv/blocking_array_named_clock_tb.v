// Behavioral oracle: `slot` may move ONLY on a falling sclk edge. Neither the
// unrelated clk edges nor the rising sclk edge may disturb it. The array has no
// reset, so one falling edge runs first to establish a known state.
module tb;
  reg clk = 0, sclk = 1, en = 0, idx = 0;
  reg [3:0] d = 0;
  wire [7:0] q;
  wire [3:0] r;
  reg [7:0] expected;
  blocking_array_named_clock dut(.*);

  task check(input [8*24:1] where);
    begin
      #1;
      if (q !== expected)
        $fatal(1, "%0s: time=%0t expected q=%h got %h", where, $time, expected, q);
    end
  endtask

  task fall_sclk;  // the ONE edge that may commit the array
    begin
      sclk = 0;
      expected = en ? (idx ? {d, 4'h0} : {4'h0, d}) : 8'h00;
      check("negedge sclk");
    end
  endtask

  task pulse_clk;  // must never disturb the array
    begin
      clk = 1; check("posedge clk");
      clk = 0; check("negedge clk");
    end
  endtask

  initial begin
    #1 fall_sclk();              // establish a known state (no reset on slot)
    sclk = 1; check("posedge sclk");
    en = 1; idx = 0; d = 4'h5;
    pulse_clk();                 // the new inputs must NOT reach slot yet
    fall_sclk();                 // slot[0] <= 5
    sclk = 1; check("posedge sclk");
    pulse_clk();
    d = 4'ha; idx = 1;
    pulse_clk();
    fall_sclk();                 // slot[1] <= a, slot[0] cleared
    sclk = 1; check("posedge sclk");
    en = 0;
    pulse_clk();
    fall_sclk();                 // the clear-only pass wipes both lanes
    sclk = 1; check("posedge sclk");
    pulse_clk();
    if (r !== d)
      $fatal(1, "the implicit-clock register lost its own clock: r=%h d=%h", r, d);
    $display("blocking array kept its own clock edge");
    $finish;
  end
endmodule
