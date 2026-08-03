// Twin of the tuple port. lgcheck pairs ports BY NAME, so the golden spells the
// port leaf exactly as the Pyrope side emits it (`inp.bits.vec`), escaped.
module tuple_wire_port_leaf_name(input \inp.bits.vec , input a, input b, output [1:0] o);
  assign o[1] = a & \inp.bits.vec ;
  assign o[0] = b;
endmodule
