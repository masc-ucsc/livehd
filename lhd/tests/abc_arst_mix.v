// Asynchronous-reset registers of every shape pass.abc maps onto a Liberty's
// clear/preset flop cells: active-high (`posedge rst`) and active-low
// (`negedge rst_n`) resets, reset values mixing 0 and 1 bits (clear AND preset
// cells in one register), a reset reached through an inverter inside the
// region, a reset synchronizer whose muxed/registered resets are computed
// inside the region (they cross ABC as outputs and mapped logic drives the
// cell pins), and a synchronous-reset register (folded into D) alongside.
module abc_arst_mix(input clk, input rst, input rst_n, input en, input scan, input scan_rst_n, input [3:0] d,
                    output [3:0] qa, output [3:0] qb, output [3:0] qc, output [3:0] qs, output [1:0] qe,
                    output qr);
  reg [3:0] a;  // active-high async reset, value 1010
  always @(posedge clk or posedge rst)
    if (rst) a <= 4'b1010;
    else a <= d ^ a;

  reg [3:0] b;  // active-low async reset, value 0110, with an enable
  always @(posedge clk or negedge rst_n)
    if (!rst_n) b <= 4'b0110;
    else if (en) b <= b + d;

  wire rst_i = ~rst_n;  // the reset through region logic: traced to rst_n
  reg [3:0] c;          // active-high on the inverted wire, all ones
  always @(posedge clk or posedge rst_i)
    if (rst_i) c <= 4'hf;
    else c <= {c[2:0], d[3] ^ a[0]};

  reg [3:0] s;  // synchronous reset: folded into D, a plain DFF cell
  always @(posedge clk)
    if (rst) s <= 4'h5;
    else s <= s ^ b;

  // prim_rst_sync shape: async assert on a scan-muxed reset, sync release.
  wire rst_int_n = scan ? scan_rst_n : rst_n;
  reg  rst_q;
  always @(posedge clk or negedge rst_int_n)
    if (!rst_int_n) rst_q <= 1'b0;
    else rst_q <= 1'b1;
  wire rst_sync_n = scan ? scan_rst_n : rst_q;
  reg [1:0] e;  // reset by the synchronized (register-driven) reset, value 01
  always @(posedge clk or negedge rst_sync_n)
    if (!rst_sync_n) e <= 2'b01;
    else e <= e + {1'b0, en};

  assign qa = a;
  assign qb = b;
  assign qc = c;
  assign qs = s;
  assign qe = e;
  assign qr = rst_q;  // observable: the independent checker pairs state by output
endmodule
