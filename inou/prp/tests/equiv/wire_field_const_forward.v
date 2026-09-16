// A read before a constant driver of a split struct wire field.
module wire_field_const_forward(input valid, input [4:0] bits, output ready);
  typedef struct packed {logic flag; logic [3:0] data;} Bits;
  typedef struct packed {logic ready; logic valid; Bits bits;} Lane;
  typedef struct packed {Lane _0;} Bundle;
  wire Lane from_exu;
  wire Bundle io;
  wire Lane gen = '{ready: from_exu.ready, valid: valid, bits: bits};
  assign io = '{_0: gen};
  assign from_exu = '{ready: 1'b1, valid: io._0.valid, bits: io._0.bits};
  assign ready = io._0.ready;
endmodule
