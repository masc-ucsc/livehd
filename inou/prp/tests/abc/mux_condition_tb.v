module tb;
  reg [7:0] a;
  wire [3:0] actual, expected;
  top dut(.a(a), .r(actual));
  reference ref_dut(.a(a), .r(expected));
  initial begin
    for (integer i=0; i<256; i=i+1) begin
      a=i; #1;
      if (actual !== expected) $fatal(1,"a=%d expected=%h actual=%h",a,expected,actual);
    end
    $finish;
  end
endmodule
