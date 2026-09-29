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
  reg [7:0] \mem.a [0:7];
  reg [3:0] \mem.b [0:7];

  always @(posedge clock) begin
    if (we) begin
      \mem.a [wi] <= wa;
      \mem.b [wi] <= wb;
    end
  end

  // async read + same-cycle write forwarding (the write precedes the read in
  // program order, so the default ordering="program" forwards it)
  assign za = (we && (wi == ri)) ? wa : \mem.a [ri];
  assign zb = (we && (wi == ri)) ? wb : \mem.b [ri];

endmodule
