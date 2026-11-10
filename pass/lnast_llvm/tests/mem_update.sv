module mem_update(input clk, input rst, input redirect, input we, input [2:0] wa, input [7:0] wd,
                  input awe, input [2:0] awa, input [7:0] awd, input [2:0] ra, output [7:0] rs, output [7:0] ra_out);
  reg [7:0] arch [0:7];
  reg [7:0] spec [0:7];
  reg [7:0] wpipe;
  reg [2:0] wpa;
  reg       wpv;
  integer i;
  always @(posedge clk or posedge rst) begin
    if (rst) begin
      for (i = 0; i < 8; i = i + 1) begin arch[i] <= 8'd0; spec[i] <= 8'd0; end
    end else begin
      if (awe) arch[awa] <= awd;
      if (redirect) begin
        for (i = 0; i < 8; i = i + 1) spec[i] <= arch[i];
      end else if (we) spec[wa] <= wd;
    end
  end
  always @(posedge clk) begin wpipe <= wd; wpa <= wa; wpv <= we; end
  assign rs = (wpv && wpa == ra) ? wpipe : spec[ra];
  assign ra_out = arch[ra];
endmodule
