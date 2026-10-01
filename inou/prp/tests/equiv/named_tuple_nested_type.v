// Golden for named_tuple_nested_type.prp: the nested tuple port flattens to
// one Verilog port per leaf (`i.inner.d`, ...), like the inline spelling; the
// typed local and the child `leaf` are plain wiring.
module top (
  input  [3:0] \i.inner.d ,
  input        \i.inner.v ,
  input  [1:0] \i.tag ,
  input  [3:0] \j.inner.d ,
  input        \j.inner.v ,
  input  [1:0] \j.tag ,
  input  [3:0] a,
  input        v,
  input  [1:0] t,
  output [3:0] y,
  output       w,
  output [1:0] z,
  output [3:0] iy,
  output       iw,
  output [1:0] iz,
  output [3:0] ld,
  output [1:0] lk
);
  assign y  = a;
  assign w  = v;
  assign z  = t;
  assign iy = \i.inner.d ^ a;
  assign iw = \i.inner.v ;
  assign iz = \i.tag ;
  assign ld = \j.inner.d ;
  assign lk = \j.tag ;
endmodule
