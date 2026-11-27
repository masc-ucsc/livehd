// Golden for imported_enum_port.prp: MemCmd:U5 = (load=0, store=1).
module top (
  input  [4:0] cmd,
  output       st,
  output       ld
);
  assign st = (cmd == 5'd1);
  assign ld = (cmd == 5'd0);
endmodule
