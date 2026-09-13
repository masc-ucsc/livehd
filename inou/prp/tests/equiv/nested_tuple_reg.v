module nested_tuple_reg (
  input        clock,
  input        a,
  input        b,
  output       y,
  output       z
);

  // The nested tuple splits one flop per LEAF, named by the dotted path -- the
  // same convention the flat spelling uses (see reg_named_tuple_type.v).
  reg \ctl.ex.aa ;
  reg \ctl.ex.bb ;
  reg \ctl.wb.cc ;

  always @(posedge clock) begin
    \ctl.ex.aa  <= a;
    \ctl.ex.bb  <= b;
    \ctl.wb.cc  <= a & b;
  end

  assign y = \ctl.ex.aa ;
  assign z = \ctl.wb.cc ;

endmodule
