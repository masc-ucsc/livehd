
// vcs -full64 -xzcheck +warn=noSV-LCM-PPWI +warn=noSV-LCM-RIPI +v2k -sverilog +cli -q sample.v

// vcs -full64 +v2k -sverilog -q sample.v

module sample(input clk, input reset);

  logic           to2_aValid;
  logic [32-1:0]  to2_a;
  logic [32-1:0]  to2_b;

  logic           to3_cValid;
  logic [32-1:0]  to3_c;

  logic           to1_aValid;
  logic [32-1:0]  to1_a;

  logic           to2_eValid;
  logic [32-1:0]  to2_e;

  logic           to3_dValid;
  logic [32-1:0]  to3_d;

  logic [32-1:0]  to1_b;

  sample1 s1 (.*);
  sample2 s2 (.*);
  sample3 s3 (.*);

endmodule
