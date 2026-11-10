module mem_reload(input clock, input reset, input redirect, input we, input [1:0] wa, input [7:0] wd,
                  input awe, input [1:0] awa, input [7:0] awd, input [1:0] ra, output [7:0] rd, output [7:0] ard);
  reg [3:0][7:0] spec;
  reg [3:0][7:0] arch;
  reg [3:0][7:0] snext;
  reg [1:0] raddr;
  reg       redir_q;
  wire [3:0][7:0] nxt;
  wire [3:0][7:0] anx;
  genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : g
    assign nxt[i] = redir_q ? arch[i] : ((we && wa == i) ? wd : spec[i]);
    assign anx[i] = (awe && awa == i) ? awd : arch[i];
  end endgenerate
  always @(posedge clock or posedge reset) begin
    if (reset) begin spec <= 32'h0; arch <= 32'h0; end
    else begin spec <= nxt; arch <= anx; end
  end
  always @(posedge clock) begin raddr <= ra; redir_q <= redirect; end
  assign rd  = spec[raddr];
  assign ard = arch[raddr];
endmodule
