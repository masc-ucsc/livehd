// Behavioral oracle for the unknown bits of an asynchronous reset constant.
// The defined bits must reset EXACTLY; the unknown ones must carry no defined
// value at all. `^x` is the x/z-agnostic test: inou.cgen.verilog spells an
// unknown constant `?`, which iverilog reads as z, so 4'bxxxx and 4'bzzzz must
// both pass while any folded constant (4'b0000 included) must fail.
module tb;
  reg clk = 0, rst = 0;
  reg [7:0] d = 8'h00;
  wire [3:0] a, b;
  async_reset_unknown_slices dut(.*);

  task check_reset(input [8*24:1] where);
    begin
      if (a[1:0] !== 2'b11)
        $fatal(1, "%0s: defined reset slice changed: a=%b", where, a);
      if ((^a[3:2]) !== 1'bx)
        $fatal(1, "%0s: unknown reset slice was folded to a defined value: a=%b", where, a);
      if (b[2] !== 1'b0 || b[0] !== 1'b1)
        $fatal(1, "%0s: defined reset bits changed: b=%b", where, b);
      if ((^{b[3], b[1]}) !== 1'bx)
        $fatal(1, "%0s: unknown whole-register reset bits were folded: b=%b", where, b);
    end
  endtask

  initial begin
    // Assert away from a clock edge: a reset demoted to synchronous would not
    // fire here at all.
    #1 rst = 1;
    #1 check_reset("async assert");
    d = 8'h9c;
    #1 clk = 1;
    #1 check_reset("reset holds over a clock edge");
    clk = 0;
    #1 rst = 0;
    #1 check_reset("release is not a clock edge");
    #1 clk = 1;
    #1;
    if (a !== d[7:4] || b !== d[3:0])
      $fatal(1, "clocked arm mismatch: a=%b b=%b d=%h", a, b, d);
    clk = 0;
    #1 rst = 1;
    #1 check_reset("re-assert");
    $display("unknown asynchronous reset slices survived the fold");
    $finish;
  end
endmodule
