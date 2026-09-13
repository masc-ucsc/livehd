module reg_named_tuple_type (
  input            clk,
  input            resetn,
  input      [1:0] opcode,
  input      [4:0] rd,
  output           add,
  output     [4:0] dest
);

  // The tuple register is split per field, one Flop each, so the state names are
  // the dotted field paths (as in reg_tuple_reset). A NAMED tuple type carries no
  // packing of its own -- it is the anonymous `(add:bool, rd:u5)` shape.
  reg       \instr.add ;
  reg [4:0] \instr.rd ;

  always @(posedge clk) begin
    \instr.add  <= resetn && (opcode == 2'd0);
    \instr.rd   <= rd;
  end

  assign add  = \instr.add ;
  assign dest = \instr.rd ;

endmodule
