
module sample1
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
      to3_cValid <=  tmp[0];
      to3_c <= tmp + to1_a;
    end
  end


endmodule

