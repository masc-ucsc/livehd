// Golden for tuple_field_bits.prp: every attribute read is the declared
// width/max of the field, a constant; `z` is the addr field passed through a
// local typed from `req.addr.[bits]` (6 bits).
module top (
  input  [5:0] \req.addr ,
  input        \req.write ,
  input  [7:0] \req.data ,
  input  [4:0] a,
  input  [2:0] b,
  output [7:0] wa,
  output [7:0] wd,
  output [7:0] mw,
  output [7:0] mx,
  output [7:0] wx,
  output [5:0] z,
  output [7:0] cb,
  output [7:0] kw,
  output [7:0] w6
);
  assign wa = 8'd6;
  assign wd = 8'd8;
  assign mw = 8'd1;
  assign mx = 8'd63;
  assign wx = 8'd5;
  assign z  = \req.addr ;
  assign cb = 8'd6;
  assign kw = 8'd8;
  assign w6 = 8'd6;
endmodule
