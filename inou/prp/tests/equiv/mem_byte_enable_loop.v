// Golden for mem_byte_enable_loop (the state is named `mem` like the Pyrope
// side): one read-modify-write per cycle with a per-byte mask; the async read
// forwards the merged word on an address match.
module mem_byte_enable_loop (
  input         clock,
  input         we,
  input  [3:0]  be,
  input  [1:0]  wa,
  input  [31:0] d,
  input  [1:0]  ra,
  output [31:0] q
);
  reg  [31:0] mem[3:0];
  wire [31:0] mask   = {{8{be[3]}}, {8{be[2]}}, {8{be[1]}}, {8{be[0]}}};
  wire [31:0] merged = (mem[wa] & ~mask) | (d & mask);

  always @(posedge clock) begin
    if (we) mem[wa] <= merged;
  end

  assign q = (we && (wa == ra)) ? merged : mem[ra];
endmodule
