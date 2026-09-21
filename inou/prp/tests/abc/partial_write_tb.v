module tb;
  reg clock=0, reset=1, load=0, we=0;
  reg [1:0] index=0;
  reg [15:0] bulk=0, init_data=16'h1234;
  reg [3:0] value=0;
  wire [15:0] allq, ref_allq, partial_allq;
  wire [3:0] rd, ref_rd, partial_rd;
  partial_write dut(.*);
  reference golden(.clock(clock),.reset(reset),.load(load),.we(we),.index(index),
    .bulk(bulk),.init_data(init_data),.value(value),.allq(ref_allq),.rd(ref_rd));
  initial begin
    #1; clock=1; #1; clock=0;
    for (integer k=0; k<256; k=k+1) begin
      reset=(k%11==0); load=k&1; we=(k>>1)&1; index=(k>>2)%4;
      bulk=k*41; init_data=k*23; value=k*13; #1;
      if (allq !== ref_allq || rd !== ref_rd) $fatal(1,"pre-edge mismatch %d",k);
      clock=1; #1;
      if (allq !== ref_allq || rd !== ref_rd) $fatal(1,"post-edge mismatch %d: %h/%h",k,allq,ref_allq);
      clock=0;
    end
    $finish;
  end
endmodule
