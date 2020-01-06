
// vcs -full64 -xzcheck +warn=noSV-LCM-PPWI +warn=noSV-LCM-RIPI +v2k -sverilog +cli -q sample.v

// vcs -full64 +v2k -sverilog -q sample.v

//Compiler version E-2011.03-SP1_Full64; Runtime version E-2011.03-SP1_Full64;  Mar 27 14:09 2012
//memory[127] =    2301383
//memory[127] =  170187640
//memory[127] = 3487714722
//memory[127] = 1364951989
//memory[127] = 2391831663
//memory[127] = 2273385865
//memory[127] = 1009614595
//memory[127] = 2895488477
//memory[127] = 3636037032
//memory[127] = 3231259825
//$finish called from file "sample.v", line 206.
//$finish at simulation time            100010000
//           V C S   S i m u l a t i o n   R e p o r t 
//Time: 100010000
//CPU Time:      7.050 seconds;       Data structure size:   0.0Mb
//Tue Mar 27 14:09:42 2012
//
//real    0m7.328s


module sample_stage1
( input                  clk,
  input                  reset,

  input logic            to1_aValid,
  input logic [32-1:0]   to1_a, // from stage 2

  input logic [32-1:0]   to1_b, // from stage 3

  output logic           to2_aValid,
  output logic [32-1:0]  to2_a,
  output logic [32-1:0]  to2_b,

  output logic           to3_cValid,
  output logic [32-1:0]  to3_c
);


  always @(posedge clk) begin
    to2_b <= to1_b + 1;
  end

  always @(posedge clk) begin
    if (reset) begin
      to2_a <= 'bx;
      to2_aValid <= 0;
    end else begin
      to2_a    <= to1_a + to1_b + 2;
      to2_aValid <= to1_aValid;
    end
  end

  logic [32-1:0] tmp;

  always @(posedge clk) begin
    if (reset) begin
      tmp <= 0;
    end else begin
      tmp <= tmp + 23;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to3_cValid <=  0;
      to3_c <= 0;
    end else begin
      to3_cValid <=  (tmp & 1);
      to3_c <= tmp + to1_a;
    end
  end


endmodule

module sample_stage2
( input                  clk,
  input                  reset,

  input logic            to2_aValid,
  input logic [32-1:0]   to2_a,
  input logic [32-1:0]   to2_b,

  output logic           to1_aValid,
  output logic [32-1:0]  to1_a, 

  output logic           to2_eValid,
  output logic [32-1:0]  to2_e, 

  output logic           to3_dValid,
  output logic [32-1:0]  to3_d
);

  logic [32-1:0] tmp;

  always @(posedge clk) begin
    if (reset) begin
      tmp <= 1;
    end else begin
      tmp <= tmp + 13; // A prime number
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to3_dValid <= 0;
    end else begin
      to3_dValid <=  (tmp & 1'd1) == 1'd0;
      to3_d <= tmp+to2_b;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to2_eValid <= 0;
    end else begin
      to2_eValid <=  (tmp & 2'b1) == 2'b1 && to2_aValid && to1_aValid;
      to2_e <= tmp+to2_a + to1_a;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to1_aValid <= 0;
    end else begin
      to1_aValid <=  (tmp & 2'd2) == 2'd2;
      to1_a <= tmp+3;
    end
  end


endmodule

module sample_stage3
( input                  clk,
  input                  reset,

  input logic           to3_cValid,
  input logic [32-1:0]  to3_c,

  input logic           to3_dValid,
  input logic [32-1:0]  to3_d,

  output logic [32-1:0]  to1_b
);

  logic [32-1:0] tmp;
  logic [32-1:0] tmp2;

 logic [32-1:0] memory[0:256-1];

  always @(posedge clk) begin
    if (reset) begin
      tmp <= 0;
      tmp2 <= 0;
    end else begin
      tmp <= tmp + 7; // A prime number

      if ((tmp & 16'hFFFF) == 45339) begin
        tmp2 <= tmp2 + 1;
        if ((tmp2 &15) == 0) begin
          $display("memory[127] = %d",memory[127]);
        end
      end
    end

  end

 logic [8-1:0] reset_iterator;

 initial begin
   reset_iterator = 0;
 end

 always @(posedge clk) begin
   if (reset) begin
     reset_iterator <= reset_iterator + 1;
     memory[reset_iterator] <= 'b0;
   end else begin
     if (to3_cValid && to3_dValid) begin
       //$display("memory[%d] = %d",(to3_c + tmp) & 8'hff, to3_d);
       memory[(to3_c + tmp) & 8'hff] <= to3_d;
     end
     to1_b <= memory[tmp&8'hff];
   end
 end


endmodule

module top_sample();

  logic clk;
  logic reset;

  initial begin
    clk = 0;
    reset = 1;
    #10000
    reset = 0;
    #100000000
    $finish();
  end

  always begin
    #5 clk = !clk;
  end

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

  sample_stage1 s1 (.*);
  sample_stage2 s2 (.*);
  sample_stage3 s3 (.*);

endmodule
