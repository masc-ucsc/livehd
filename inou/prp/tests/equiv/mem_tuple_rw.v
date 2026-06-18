module \mem_tuple_rw.mtr (
  input      [7:0] wa,
  input      [3:0] wb,
  input      [2:0] wi,
  input      [2:0] ri,
  input            we,
  output     [7:0] za,
  output     [3:0] zb,
  input            clock
);

  // The tuple memory is split per field: one array for `.a`, one for `.b`.
  reg [7:0] mem_a [0:7];
  reg [3:0] mem_b [0:7];

  always @(posedge clock) begin
    if (we) begin
      mem_a[wi] <= wa;
      mem_b[wi] <= wb;
    end
  end

  // async read + same-cycle write forwarding (fwd=1 for reg memories)
  assign za = (we && (wi == ri)) ? wa : mem_a[ri];
  assign zb = (we && (wi == ri)) ? wb : mem_b[ri];

endmodule
