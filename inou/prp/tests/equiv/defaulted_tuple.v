module top (
  input            clk,
  input            resetn,
  input      [1:0] opcode,
  input      [4:0] rd,
  output           add,
  output           sub,
  output     [4:0] dest
);

  // Same state-naming convention as reg_named_tuple_type.v: the tuple register
  // is split per field, one Flop each, so the state names are the dotted field
  // paths. Per-field DEFAULTS (`mut add:bool = nil`) are a property of the TYPE,
  // not of the packing -- the shape is still the anonymous
  // `(add:bool, sub:bool, rd:u5)`, so the split is identical.
  reg       \instr.add ;
  reg       \instr.sub ;
  reg [4:0] \instr.rd ;

  always @(posedge clk) begin
    \instr.add  <= resetn && (opcode == 2'd0);
    \instr.sub  <= resetn && (opcode == 2'd1);
    \instr.rd   <= rd;
  end

  assign add  = \instr.add ;
  assign sub  = \instr.sub ;
  assign dest = \instr.rd ;

endmodule
