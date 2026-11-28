// One design that exercises every LGraph operator lhd emits from Verilog, at several widths,
// with memories, two clocks, an async active-low reset and enables. Every memory read is
// clamped to the memory's depth, so no read returns X in a 4-state simulator.
module allops (
  input clk, input clk2, input rst_n,
  input [7:0] a8, input [7:0] b8,
  input [31:0] a32, input [31:0] b32,
  input [64:0] a65, input [64:0] b65,
  input [127:0] a128, input [127:0] b128,
  input [7:0] sh, input [2:0] sel, input en,
  input we, input [3:0] wa, input [31:0] wd,
  input we2, input [3:0] wa2, input [31:0] wd2, input [3:0] be,
  input [3:0] ra,
  // 8 bits
  output [7:0] s8_add, output [7:0] s8_sub, output [7:0] s8_mul, output [7:0] s8_udiv, output [7:0] s8_umod,
  output [7:0] s8_sdiv, output [7:0] s8_smod, output [7:0] s8_and, output [7:0] s8_or, output [7:0] s8_xor,
  output [7:0] s8_not, output [7:0] s8_shl, output [7:0] s8_shr, output [7:0] s8_sra, output [7:0] s8_rotl,
  output [10:0] c8,            // ult ugt ule uge slt sgt sle sge eq ne, and the reduction or
  // 32 bits
  output [31:0] s32_add, output [31:0] s32_sub, output [31:0] s32_mul, output [31:0] s32_udiv,
  output [31:0] s32_umod, output [31:0] s32_sdiv, output [31:0] s32_smod, output [31:0] s32_xor,
  output [31:0] s32_shl, output [31:0] s32_shr, output [31:0] s32_sra, output [31:0] s32_rotl,
  output [9:0] c32, output [2:0] red32,
  // 65 bits (two machine words)
  output [64:0] s65_add, output [64:0] s65_sub, output [64:0] s65_mul, output [64:0] s65_udiv,
  output [64:0] s65_sdiv, output [64:0] s65_shl, output [64:0] s65_sra, output [64:0] s65_not, output [5:0] c65,
  // 128 bits
  output [127:0] s128_add, output [127:0] s128_mul, output [127:0] s128_xor, output [127:0] s128_shr,
  output [127:0] s128_sra, output [5:0] c128,
  // bit ranges, extension, muxes
  output [7:0] get_static, output [7:0] get_dyn, output [63:0] get_wide, output [31:0] set_static,
  output [31:0] set_dyn, output [31:0] sext32, output [64:0] sext65, output [15:0] cat,
  output [31:0] mux2, output [31:0] muxn,
  // state
  output [31:0] acc, output [7:0] cnt2, output [31:0] rd_comb, output [31:0] rd_q,
  output [31:0] rd_be, output [7:0] rom_out,
  output [63:0] mr_all, output [15:0] mr_rd, output [31:0] muxn8, output [7:0] tbl_out
);
  // ---- 8 bits
  assign s8_add = a8 + b8;  assign s8_sub = a8 - b8;  assign s8_mul = a8 * b8;
  assign s8_udiv = a8 / b8; assign s8_umod = a8 % 8'd7;
  assign s8_sdiv = $signed(a8) / $signed(b8); assign s8_smod = $signed(a8) % 8'sd5;
  assign s8_and = a8 & b8;  assign s8_or = a8 | b8;  assign s8_xor = a8 ^ b8;  assign s8_not = ~a8;
  assign s8_shl = a8 << sh; assign s8_shr = a8 >> sh; assign s8_sra = $signed(a8) >>> sh;
  assign s8_rotl = (a8 << sh[2:0]) | (a8 >> (4'd8 - {1'b0, sh[2:0]}));
  assign c8 = {a8 < b8, a8 > b8, a8 <= b8, a8 >= b8, $signed(a8) < $signed(b8), $signed(a8) > $signed(b8),
               $signed(a8) <= $signed(b8), $signed(a8) >= $signed(b8), a8 == b8, a8 != b8, |a8};
  // ---- 32 bits
  assign s32_add = a32 + b32;  assign s32_sub = a32 - b32;  assign s32_mul = a32 * b32;
  assign s32_udiv = a32 / b32; assign s32_umod = a32 % 32'd1000;
  assign s32_sdiv = $signed(a32) / $signed(b32); assign s32_smod = $signed(a32) % 32'sd77;
  assign s32_xor = a32 ^ b32;
  assign s32_shl = a32 << sh; assign s32_shr = a32 >> sh; assign s32_sra = $signed(a32) >>> sh;
  assign s32_rotl = (a32 << sh[4:0]) | (a32 >> (6'd32 - {1'b0, sh[4:0]}));
  assign c32 = {a32 < b32, a32 > b32, a32 <= b32, a32 >= b32, $signed(a32) < $signed(b32),
                $signed(a32) > $signed(b32), $signed(a32) <= $signed(b32), $signed(a32) >= $signed(b32),
                a32 == b32, a32 != b32};
  assign red32 = {&a32, |a32, ^a32};
  // ---- 65 bits
  assign s65_add = a65 + b65;  assign s65_sub = a65 - b65;  assign s65_mul = a65 * b65;
  assign s65_udiv = a65 / b65; assign s65_sdiv = $signed(a65) / $signed(b65);
  assign s65_shl = a65 << sh;  assign s65_sra = $signed(a65) >>> sh;  assign s65_not = ~a65;
  assign c65 = {a65 < b65, a65 > b65, $signed(a65) < $signed(b65), $signed(a65) > $signed(b65),
                a65 == b65, ^a65};
  // ---- 128 bits
  assign s128_add = a128 + b128; assign s128_mul = a128 * b128; assign s128_xor = a128 ^ b128;
  assign s128_shr = a128 >> sh;  assign s128_sra = $signed(a128) >>> sh;
  assign c128 = {a128 < b128, a128 > b128, $signed(a128) < $signed(b128), $signed(a128) > $signed(b128),
                 a128 == b128, &a128};
  // ---- bit ranges, extension, muxes
  assign get_static = a32[11:4];
  assign get_dyn = a32[sh[4:0] +: 8];
  assign get_wide = a128[100:37];
  reg [31:0] t_static, t_dyn;
  always @(*) begin t_static = a32; t_static[15:8] = b8; end
  always @(*) begin t_dyn = a32; t_dyn[sh[4:0] +: 4] = b8[3:0]; end
  assign set_static = t_static; assign set_dyn = t_dyn;
  assign sext32 = {{24{a8[7]}}, a8};
  assign sext65 = $signed(a32);
  assign cat = {a8, b8};
  assign mux2 = en ? a32 : b32;
  reg [31:0] m;
  always @(*) begin
    case (sel)
      3'd0: m = a32; 3'd1: m = b32; 3'd2: m = a32 + 1; 3'd3: m = b32 ^ 32'hdeadbeef; 3'd4: m = {a8, a8, b8, b8};
      default: m = 32'h55aa55aa;
    endcase
  end
  assign muxn = m;
  // ---- state: an enable flop with an async active-low reset, and a second clock domain
  reg [31:0] acc_q;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) acc_q <= 32'h12345678; else if (en) acc_q <= acc_q + a32;
  assign acc = acc_q;
  reg [7:0] cnt2_q;
  always @(posedge clk2 or negedge rst_n)
    if (!rst_n) cnt2_q <= 8'd3; else cnt2_q <= cnt2_q + 8'd1;
  assign cnt2 = cnt2_q;
  // ---- a 12-word memory, two write ports (the second wins on one address), reads clamped
  reg [31:0] mem [0:11];
  integer i;
  initial for (i = 0; i < 12; i = i + 1) mem[i] = 32'd0;
  wire [3:0] rc = (ra < 4'd12) ? ra : 4'd0;
  wire [3:0] w1 = (wa < 4'd12) ? wa : 4'd0;
  wire [3:0] w2 = (wa2 < 4'd12) ? wa2 : 4'd0;
  always @(posedge clk) begin
    if (we) mem[w1] <= wd;
    if (we2) mem[w2] <= wd2;
  end
  assign rd_comb = mem[rc];                       // reads the old word in a cycle that writes it
  reg [31:0] rd_q_r = 32'd0;
  always @(posedge clk) rd_q_r <= mem[rc];
  assign rd_q = rd_q_r;
  // ---- byte enables
  reg [31:0] mbe [0:15];
  integer j;
  initial for (j = 0; j < 16; j = j + 1) mbe[j] = 32'd0;
  always @(posedge clk)
    if (we2) begin
      if (be[0]) mbe[wa2][7:0] <= wd2[7:0];
      if (be[1]) mbe[wa2][15:8] <= wd2[15:8];
      if (be[2]) mbe[wa2][23:16] <= wd2[23:16];
      if (be[3]) mbe[wa2][31:24] <= wd2[31:24];
    end
  assign rd_be = mbe[ra];
  // ---- a ROM with a preloaded image
  reg [7:0] rom [0:15];
  initial $readmemh("rom.hex", rom);
  assign rom_out = rom[ra];
  // ---- a 4-word memory with an async bulk reset (every word cleared by rst_n), read whole
  reg [15:0] mr [0:3];
  integer k;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin for (k = 0; k < 4; k = k + 1) mr[k] <= 16'h0000; end
    else if (we) mr[wa[1:0]] <= wd[15:0];
  assign mr_all = {mr[3], mr[2], mr[1], mr[0]};
  assign mr_rd = mr[ra[1:0]];
  // ---- an 8-way mux on a 3-bit select, and a constant table read by a select
  reg [31:0] m8;
  always @(*) begin
    case (sel)
      3'd0: m8 = a32; 3'd1: m8 = b32; 3'd2: m8 = ~a32; 3'd3: m8 = ~b32;
      3'd4: m8 = a32 & b32; 3'd5: m8 = a32 | b32; 3'd6: m8 = {b8, a8, b8, a8}; 3'd7: m8 = wd;
    endcase
  end
  assign muxn8 = m8;
  wire [7:0] tbl [0:7];
  assign tbl[0] = 8'h11; assign tbl[1] = 8'h22; assign tbl[2] = 8'h33; assign tbl[3] = 8'h44;
  assign tbl[4] = 8'h55; assign tbl[5] = 8'h66; assign tbl[6] = 8'h77; assign tbl[7] = 8'h88;
  assign tbl_out = tbl[sel];
endmodule
