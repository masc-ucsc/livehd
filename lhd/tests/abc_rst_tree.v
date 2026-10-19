// A wide asynchronous-reset register file for pass.abc.reset_fanout: 32 bits on one
// active-low reset input with reset values mixing 0 and 1 (clear AND preset cells,
// a polarity inverter on the preset side of an active-high-preset library), and a
// 16-bit register whose reset is computed inside the region (a scan mux: it crosses
// ABC as an output and mapped logic drives the cell pins).
module abc_rst_tree(input clk, input rst_n, input scan, input scan_rst_n, input [31:0] d,
                    output [31:0] qa, output [15:0] qb);
  reg [31:0] a;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) a <= 32'ha5a5a5a5;
    else a <= d ^ a;

  wire rst_int_n = scan ? scan_rst_n : rst_n;
  reg [15:0] b;
  always @(posedge clk or negedge rst_int_n)
    if (!rst_int_n) b <= 16'h0;
    else b <= {b[14:0], d[0]};

  assign qa = a;
  assign qb = b;
endmodule
