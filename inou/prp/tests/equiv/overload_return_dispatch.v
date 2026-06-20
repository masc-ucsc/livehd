module \overload_return_dispatch.ovret (
   input  [7:0] x
  ,output [8:0] s
);

  // Whole bind through [mk_two, mk_one] skips the multi-output mk_two (its two
  // outputs cannot bind one variable) and selects the single-output mk_one.
  assign s = x + 1;

endmodule
