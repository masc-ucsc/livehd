module tb;
  reg clock=0, en=1;
  reg [3:0] addr=0;
  wire [7:0] comb, q, expected_comb, expected_q;
  rom dut(.*);
  reference golden(.clock(clock),.en(en),.addr(addr),.comb(expected_comb),.q(expected_q));
  initial begin
    for (integer k=0; k<32; k=k+1) begin
      addr=k%16; en=(k<16); #1;
      if (comb !== expected_comb) $fatal(1,"ROM read mismatch at %d",addr);
      clock=1; #1;
      if (q !== expected_q) $fatal(1,"ROM registered read mismatch at %d",addr);
      clock=0;
    end
    $finish;
  end
endmodule
