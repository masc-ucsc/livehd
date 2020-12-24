module VendingMachineSwitch(
  input   clock,
  input   reset,
  input   io_nickel,
  input   io_dime,
  output  io_valid
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [2:0] state; // @[VendingMachineSwitch.scala 22:22]
  wire  _T = 3'h0 == state; // @[Conditional.scala 37:30]
  wire  _T_1 = 3'h1 == state; // @[Conditional.scala 37:30]
  wire [2:0] _GEN_2 = io_nickel ? 3'h2 : state; // @[VendingMachineSwitch.scala 30:24 VendingMachineSwitch.scala 30:32 VendingMachineSwitch.scala 22:22]
  wire  _T_2 = 3'h2 == state; // @[Conditional.scala 37:30]
  wire [2:0] _GEN_4 = io_nickel ? 3'h3 : state; // @[VendingMachineSwitch.scala 34:24 VendingMachineSwitch.scala 34:32 VendingMachineSwitch.scala 22:22]
  wire [2:0] _GEN_5 = io_dime ? 3'h4 : _GEN_4; // @[VendingMachineSwitch.scala 35:22 VendingMachineSwitch.scala 35:30]
  wire  _T_3 = 3'h3 == state; // @[Conditional.scala 37:30]
  wire [2:0] _GEN_6 = io_nickel ? 3'h4 : state; // @[VendingMachineSwitch.scala 38:24 VendingMachineSwitch.scala 38:32 VendingMachineSwitch.scala 22:22]
  wire [2:0] _GEN_7 = io_dime ? 3'h4 : _GEN_6; // @[VendingMachineSwitch.scala 39:22 VendingMachineSwitch.scala 39:30]
  wire  _T_4 = 3'h4 == state; // @[Conditional.scala 37:30]
  wire [2:0] _GEN_8 = _T_4 ? 3'h0 : state; // @[Conditional.scala 39:67 VendingMachineSwitch.scala 42:13 VendingMachineSwitch.scala 22:22]
  wire [2:0] _GEN_9 = _T_3 ? _GEN_7 : _GEN_8; // @[Conditional.scala 39:67]
  assign io_valid = state == 3'h4; // @[VendingMachineSwitch.scala 45:22]
  always @(posedge clock) begin
    if (reset) begin // @[VendingMachineSwitch.scala 22:22]
      state <= 3'h0; // @[VendingMachineSwitch.scala 22:22]
    end else if (_T) begin // @[Conditional.scala 40:58]
      if (io_dime) begin // @[VendingMachineSwitch.scala 27:22]
        state <= 3'h2; // @[VendingMachineSwitch.scala 27:30]
      end else if (io_nickel) begin // @[VendingMachineSwitch.scala 26:24]
        state <= 3'h1; // @[VendingMachineSwitch.scala 26:32]
      end
    end else if (_T_1) begin // @[Conditional.scala 39:67]
      if (io_dime) begin // @[VendingMachineSwitch.scala 31:22]
        state <= 3'h3; // @[VendingMachineSwitch.scala 31:30]
      end else begin
        state <= _GEN_2;
      end
    end else if (_T_2) begin // @[Conditional.scala 39:67]
      state <= _GEN_5;
    end else begin
      state <= _GEN_9;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  state = _RAND_0[2:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
