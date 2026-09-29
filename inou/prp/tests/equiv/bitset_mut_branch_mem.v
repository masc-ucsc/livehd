// Independent sequential golden. The state is deliberately an unpacked array,
// matching the Pyrope memory representation while avoiding set_mask entirely.
module bitset_mut_branch_mem_top (
  input  logic [63:0] a,
  input  logic        load,
  input  logic        clear_low,
  input  logic        rst_n,
  output logic [63:0] o,
  input  logic        clock
);
  logic [3:0] q [0:15];
  logic [63:0] d;
  integer i;

  always_ff @(posedge clock) begin
    if (!rst_n) begin
      for (i = 0; i < 16; i = i + 1)
        q[i] <= 4'b0;
    end else if (clear_low) begin
      for (i = 0; i < 14; i = i + 1)
        q[i] <= 4'b0;
    end else if (load) begin
      for (i = 0; i < 16; i = i + 1)
        q[i] <= a[i*4 +: 4];
    end
  end

  always_comb begin
    for (i = 0; i < 16; i = i + 1)
      d[i*4 +: 4] = q[i];
    if (clear_low)
      d[55:0] = 56'b0;
    else if (load)
      d = a;
    o = d;
  end
endmodule
