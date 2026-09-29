module lesssign_u1_mux(
  input            clk,
  input            reset,
  input      [1:0] sel,
  input      [2:0] d0,
  input      [2:0] d1,
  input      [2:0] d2,
  input      [2:0] d3,
  output     [2:0] q,
  output           zero
);
  reg [2:0] state;

  always @(posedge clk) begin
    if (reset)
      state <= 3'b000;
    else begin
      case (sel)
        2'd0: state <= d0;
        2'd1: state <= d1;
        2'd2: state <= d2;
        default: state <= d3;
      endcase
    end
  end

  assign q = state;
  assign zero = (state == 3'b000);
endmodule
