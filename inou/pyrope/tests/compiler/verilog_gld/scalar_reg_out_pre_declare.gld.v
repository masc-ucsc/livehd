module scalar_reg_out_pre_declare(clock, reset, out, out2);
  reg [2:0] \#reg_0 ;
  input clock;
  input reset;
  wire [1:0] lg_0;
  wire [2:0] lg_1;
  output [1:0] out;
  output [2:0] out2;
  always @(posedge clock) begin
    if (reset) begin
      \#reg_0 <= 0;
    end else begin
      \#reg_0  <= lg_1;
    end
  end
  assign lg_0 = 2'h1;
  assign lg_1 = 3'h2;
  assign out = lg_0;
  assign out2 = \#reg_0 ;
endmodule
