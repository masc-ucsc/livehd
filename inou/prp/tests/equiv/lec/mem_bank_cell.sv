/*
:lec_top: mem_bank_cell
:lec_set: formal.bound=4
A resetless register file inside a tile instance: the reference keeps the
Memory `tile.mem`; the variants are the mapped netlist shape `lhd synth`
emits for bedrock br_ram_flops (lhdtrack, ASAP7): the storage is a
`cgen_memory_*_blasted` module instance that kept its SOURCE name
(`\tile.mem `, not cgen's hex `__lhdmem_h..._e` wrapper), holding one
QN-output DFF cell per bit `\_mem[i][b] ` whose Liberty model is inlined, so
each state cut is `tile.mem._mem[i][b].flop_16`. LEC must tie the Memory's
power-on array to those cells through the model segment; without the tie a
read of a never-written entry compares two unrelated free values and
false-REFUTES (rd_data ref=all-ones impl=0).
*/
module mem_bank_tile(
  input  logic       clk,
  input  logic       wr_valid,
  input  logic [1:0] wr_addr,
  input  logic [1:0] wr_data,
  input  logic [1:0] rd_addr,
  output logic [1:0] rd_data
);
  logic [1:0] mem[4];
  always_ff @(posedge clk) begin
    if (wr_valid) mem[wr_addr] <= wr_data;
  end
  assign rd_data = mem[rd_addr];
endmodule

module mem_bank_cell(
  input  logic       clk,
  input  logic       wr_valid,
  input  logic [1:0] wr_addr,
  input  logic [1:0] wr_data,
  input  logic [1:0] rd_addr,
  output logic [1:0] rd_data
);
  mem_bank_tile tile(.clk(clk), .wr_valid(wr_valid), .wr_addr(wr_addr), .wr_data(wr_data),
                     .rd_addr(rd_addr), .rd_data(rd_data));
endmodule
