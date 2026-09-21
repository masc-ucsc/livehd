module tb;
  reg clk_i=0, en_i=1;
  reg [2:0] wa_i=0, wd_i=0, ra_i=0;
  wire [2:0] rd_o, ref_rd;
  comb_array_const_index_read dut(.*);
  array_reference golden(.clk_i(clk_i),.en_i(en_i),.wa_i(wa_i),.wd_i(wd_i),.ra_i(ra_i),.rd_o(ref_rd));
  initial begin
    for (integer k=0; k<8; k=k+1) begin
      wa_i=k; wd_i=k^3; #1; clk_i=1; #1; clk_i=0;
    end
    for (integer k=0; k<128; k=k+1) begin
      en_i=k&1; wa_i=(k>>1)%8; wd_i=(k*5)%8; ra_i=(k>>3)%8; #1;
      if (rd_o !== ref_rd) $fatal(1,"array pre-edge mismatch %d",k);
      clk_i=1; #1;
      if (rd_o !== ref_rd) $fatal(1,"array post-edge mismatch %d",k);
      clk_i=0;
    end
    $finish;
  end
endmodule
