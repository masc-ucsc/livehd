// Golden for struct_init_whole_read: a nested-struct wire initialized from a
// '{...} pattern over const-indexed array-of-struct element fields (CIRCT's
// CtrlBlock _GEN idiom), consumed only WHOLE inside a concat. The slang
// reader must split the initializer into the same per-field leaves its reads
// resolve through — the old gate stored the flat name instead, leaving every
// leaf undriven ("incompletely driven"). Functionally io_out forwards
// cfVec[1].bits = io_cfVec_1_bits.
module \struct_init_whole_read.top (
  input               clock,
  input               reset,
  input               io_cfVec_0_valid,
  input struct packed {logic [3:0] foldpc; struct packed {logic e1; logic e2; } ev; logic [7:0] instr; } io_cfVec_0_bits,
  input               io_cfVec_1_valid,
  input struct packed {logic [3:0] foldpc; struct packed {logic e1; logic e2; } ev; logic [7:0] instr; } io_cfVec_1_bits,
  output [13:0]       io_out
);
  wire struct packed {logic valid; struct packed {logic [3:0] foldpc; struct packed {logic e1; logic e2; } ev; logic [7:0] instr; } bits; }
    _GEN_A = '{valid: io_cfVec_0_valid, bits: io_cfVec_0_bits};
  wire struct packed {logic valid; struct packed {logic [3:0] foldpc; struct packed {logic e1; logic e2; } ev; logic [7:0] instr; } bits; }
    _GEN_B = '{valid: io_cfVec_1_valid, bits: io_cfVec_1_bits};
  wire struct packed {struct packed {logic valid; struct packed {logic [3:0] foldpc; struct packed {logic e1; logic e2; } ev; logic [7:0] instr; } bits; } [1:0] cfVec; }
    io_frontend;
  assign io_frontend = '{cfVec: ({{_GEN_B}, {_GEN_A}})};
  wire struct packed {logic [3:0] foldpc; struct packed {logic e1; logic e2; } ev; logic [7:0] instr; }
    _GEN_1 =
    '{foldpc: io_frontend.cfVec[1'h1].bits.foldpc,
      ev: io_frontend.cfVec[1'h1].bits.ev,
      instr: io_frontend.cfVec[1'h1].bits.instr};
  assign io_out = {_GEN_1};
endmodule
