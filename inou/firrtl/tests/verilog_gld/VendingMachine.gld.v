module VendingMachine(
  input   clock,
  input   reset,
  input   io_nickel,
  input   io_dime,
  output  io_valid
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_REG_INIT
  reg [2:0] state; // @[VendingMachine.scala 22:22]
  wire [2:0] _GEN_0 = io_nickel ? 3'h1 : state; // @[VendingMachine.scala 24:22 VendingMachine.scala 24:30 VendingMachine.scala 22:22]
  wire [2:0] _GEN_1 = io_dime ? 3'h2 : _GEN_0; // @[VendingMachine.scala 25:22 VendingMachine.scala 25:30]
  wire [2:0] _GEN_2 = state == 3'h0 ? _GEN_1 : state; // @[VendingMachine.scala 23:26 VendingMachine.scala 22:22]
  wire [2:0] _GEN_3 = io_nickel ? 3'h2 : _GEN_2; // @[VendingMachine.scala 28:22 VendingMachine.scala 28:30]
  wire [2:0] _GEN_4 = io_dime ? 3'h3 : _GEN_3; // @[VendingMachine.scala 29:22 VendingMachine.scala 29:30]
  wire [2:0] _GEN_5 = state == 3'h1 ? _GEN_4 : _GEN_2; // @[VendingMachine.scala 27:23]
  wire [2:0] _GEN_6 = io_nickel ? 3'h3 : _GEN_5; // @[VendingMachine.scala 32:22 VendingMachine.scala 32:30]
  wire [2:0] _GEN_7 = io_dime ? 3'h4 : _GEN_6; // @[VendingMachine.scala 33:22 VendingMachine.scala 33:30]
  wire [2:0] _GEN_8 = state == 3'h2 ? _GEN_7 : _GEN_5; // @[VendingMachine.scala 31:24]
  wire [2:0] _GEN_9 = io_nickel ? 3'h4 : _GEN_8; // @[VendingMachine.scala 36:22 VendingMachine.scala 36:30]
  assign io_valid = state == 3'h4; // @[VendingMachine.scala 42:22]
  always @(posedge clock) begin
    if (reset) begin // @[VendingMachine.scala 22:22]
      state <= 3'h0; // @[VendingMachine.scala 22:22]
    end else if (state == 3'h4) begin // @[VendingMachine.scala 39:24]
      state <= 3'h0; // @[VendingMachine.scala 40:11]
    end else if (state == 3'h3) begin // @[VendingMachine.scala 35:24]
      if (io_dime) begin // @[VendingMachine.scala 37:22]
        state <= 3'h4; // @[VendingMachine.scala 37:30]
      end else begin
        state <= _GEN_9;
      end
    end else if (state == 3'h2) begin // @[VendingMachine.scala 31:24]
      state <= _GEN_7;
    end else begin
      state <= _GEN_5;
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
