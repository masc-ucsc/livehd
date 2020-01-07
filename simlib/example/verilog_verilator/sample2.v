
module sample2
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
      to3_dValid <=  (tmp[0]) == 1'd0;
      to3_d <= tmp+to2_b;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to2_eValid <= 0;
    end else begin
      to2_eValid <=  (tmp[0]) == 1'b1 && to2_aValid && to1_aValid;
      to2_e <= tmp+to2_a + to1_a;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to1_aValid <= 0;
    end else begin
      to1_aValid <=  (tmp[1]) == 1'd1;
      to1_a <= tmp+3;
    end
  end


endmodule

