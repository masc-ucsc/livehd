// Behavioral oracle for the unknown bits of an asynchronous reset constant.
// The defined bits must reset EXACTLY; every unknown one must carry no defined
// value at all. The check is PER BIT on purpose: a reduction over a group
// (`^a[3:2]`) stays x when only one of the two bits folds, so it would pass the
// very bug this fixture exists for. inou.cgen.verilog spells an unknown
// constant `?`, which iverilog reads as z, so the test must accept x and z
// alike -- hence "is not 0 and not 1" rather than a compare against 'x.
module tb;
  reg clk = 0, rst = 0;
  reg [7:0] d = 8'h00;
  wire [3:0] a, b;
  async_reset_unknown_slices dut(.*);

  task unknown_bit(input [8*40:1] where, input [8*8:1] who, input bit_val);
    if (bit_val === 1'b0 || bit_val === 1'b1)
      $fatal(1, "%0s: %0s was folded to a defined value (%b): a=%b b=%b", where, who, bit_val, a, b);
  endtask

  task check_reset(input [8*40:1] where);
    begin
      // The slice-accumulator register: a[3:2] unknown, a[1:0] = 2'b11.
      if (a[1:0] !== 2'b11)
        $fatal(1, "%0s: defined reset slice changed: a=%b", where, a);
      unknown_bit(where, "a[3]", a[3]);
      unknown_bit(where, "a[2]", a[2]);
      // The whole-register control: b = 4'bx0x1, which never used the
      // accumulator and already kept its unknowns.
      if (b[2] !== 1'b0 || b[0] !== 1'b1)
        $fatal(1, "%0s: defined reset bits changed: b=%b", where, b);
      unknown_bit(where, "b[3]", b[3]);
      unknown_bit(where, "b[1]", b[1]);
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
