// Golden for named_tuple_port_import.prp: each imported tuple port flattens
// to one Verilog port per field, like the inline spelling.
module top (
  input  [7:0] \req.addr ,
  input        \req.write ,
  input  [7:0] \req.data ,
  input  [3:0] \p.lo ,
  input  [3:0] \p.hi ,
  input  [3:0] lo,
  input  [3:0] hi,
  input  [7:0] a,
  input  [7:0] d,
  output [7:0] y,
  output       w,
  output [3:0] s,
  output [3:0] s2,
  output [7:0] x,
  output [7:0] \o.addr ,
  output       \o.write ,
  output [7:0] \o.data
);
  assign y  = \req.addr ^ \req.data ;
  assign w  = \req.write ;
  assign s  = \p.lo ^ \p.hi ;
  assign s2 = lo ^ hi;
  assign x  = a + d;
  assign \o.addr  = a;
  assign \o.write = \req.write ;
  assign \o.data  = d ^ a;
endmodule
