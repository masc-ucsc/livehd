// Registers nothing observes, registers observed only in part, and the same across a hierarchy.
module sweep_child(input clk, input rst, input [7:0] d, output [31:0] wide, output [7:0] narrow);
  reg [31:0] pipe;     // only the low 8 bits leave through `narrow`
  reg [31:0] sink;     // feeds nothing
  reg [7:0]  taps;
  always @(posedge clk) begin
    if (rst) begin
      pipe <= 32'h0;
      sink <= 32'h0;
      taps <= 8'h0;
    end else begin
      pipe <= {pipe[23:0], d};
      sink <= sink + {24'h0, d};
      taps <= taps ^ d;
    end
  end
  assign wide   = pipe;
  assign narrow = taps;
endmodule

module sweep(input clk, input rst, input [7:0] a, input [1:0] sel, output [7:0] y, output [3:0] z, output w);
  wire [31:0] cw;
  wire [7:0]  cn;
  sweep_child u (.clk(clk), .rst(rst), .d(a), .wide(cw), .narrow(cn));

  // a counter that only feeds itself
  reg [15:0] lonely;
  always @(posedge clk) if (rst) lonely <= 0; else lonely <= lonely + 1;

  // a shift register read only at its far end
  reg [63:0] shifter;
  always @(posedge clk) if (rst) shifter <= 0; else shifter <= {shifter[55:0], a};

  // a wide status word whose upper half is never read
  reg [31:0] status;
  always @(posedge clk) if (rst) status <= 0; else if (sel[0]) status <= {status[15:0], a, a};

  assign y = cn ^ cw[7:0] ^ shifter[63:56] ^ status[7:0];
  assign z = sel[1] ? cw[11:8] : status[11:8];
  assign w = cw[24] ^ status[12];
endmodule
