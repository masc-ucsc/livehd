// Golden for bundle_port_unread_readback (lhdtrack suggestions7 item 4).
//
// inou.slang gives every combinational bundle OUTPUT of a NON-top module (a
// packed struct port, or a packed vector its per-module SROA split into bit
// leaves) a reader-side `wire <port>__bpr.<f>` plus a bridge store per field.
// Here neither child reads its own output, so every `__bpr` leaf is unread.
// In the graphs flow (`--emit-dir lg:` next to `pyrope:`) upass's named-store
// DCE swept the unread bridge stores but kept the `wire` declarations, and the
// prp_writer self-check rejected the emitted child ("wire `o__bpr.e1` is
// declared but never driven"). The hierarchy matters: the top module is never
// split, so a standalone module does not reproduce it.
module bpur_vec(input logic [1:0] a, output logic [1:0] o);
  assign o[0] = a[1];
  assign o[1] = a[0];
endmodule

module bpur_struct(
  input  logic [3:0] b,
  output struct packed {logic [2:0] d; logic z; } out
);
  assign out.d = b[2:0] ^ 3'h5;
  assign out.z = b[3];
endmodule

module bundle_port_unread_readback(
  input  logic [1:0] a,
  input  logic [3:0] b,
  output logic [1:0] o,
  output logic [3:0] s
);
  wire struct packed {logic [2:0] d; logic z; } so;
  bpur_vec    u_vec(.a(a), .o(o));
  bpur_struct u_struct(.b(b), .out(so));
  assign s = {so.z, so.d};
endmodule
