// Golden for named_tuple_port.prp: a tuple port flattens to one Verilog port
// per field, named `<port>.<field>` (escaped), exactly like the inline
// spelling `req:(addr:u8, write:bool, data:u8)`.
module top (
  input  [7:0] \req.addr ,
  input        \req.write ,
  input  [7:0] \req.data ,
  output [7:0] \rsp.data ,
  output       \rsp.hit ,
  output [7:0] y,
  output       w,
  output [7:0] z
);
  assign \rsp.data = \req.addr ^ \req.data ;
  assign \rsp.hit  = \req.write && (\req.addr == 8'd3);
  assign y = \req.data ;
  assign w = \req.write ;
  assign z = \req.addr + \req.data ;
endmodule
