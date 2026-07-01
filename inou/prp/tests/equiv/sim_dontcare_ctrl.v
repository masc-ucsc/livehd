// Golden for sim_dontcare_ctrl: ctrl_o is intentionally undefined when
// valid_o is false. This mirrors the XS Dispatcher failures where LG emits
// constants with '?' payload fields, while the Pyrope side computes concrete
// values for bits that are not semantically observed.
module \sim_dontcare_ctrl.pick (
  input        valid,
  input  [7:0] ctrl,
  output       valid_o,
  output [7:0] ctrl_o
);
  assign valid_o = valid;
  assign ctrl_o  = valid ? ctrl : 8'hxx;
endmodule
