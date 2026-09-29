module nested_tuple_return (
  input        clock,
  input  [3:0] bits,
  output       y,
  output       z
);

  // One flop per LEAF, named by the dotted path -- the same split the flat
  // spelling produces. The whole-tuple assignment must come out identical to
  // assigning each field individually.
  reg \ctl.ex.aa ;
  reg \ctl.ex.bb ;
  reg \ctl.wb.cc ;

  always @(posedge clock) begin
    \ctl.ex.aa  <= bits[0];
    \ctl.ex.bb  <= bits[1];
    \ctl.wb.cc  <= bits[2];
  end

  assign y = \ctl.ex.aa ;
  assign z = \ctl.wb.cc ;

endmodule
