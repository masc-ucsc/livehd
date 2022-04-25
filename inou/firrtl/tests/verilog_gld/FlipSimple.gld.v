module FlipSimple(
  input        clock,
  input        reset,
  input  [3:0] auto_foo_bar,
  output [4:0] auto_foo_baz
);
  assign auto_foo_baz = auto_foo_bar + 4'hd;
endmodule
