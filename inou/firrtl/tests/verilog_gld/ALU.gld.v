module ALU(
  input         clock,
  input         reset,
  input  [3:0]  io_operation,
  input  [31:0] io_inputx,
  input  [31:0] io_inputy,
  output [31:0] io_result
);
  wire [31:0] _T_1 = io_inputx & io_inputy;
  wire [31:0] _T_3 = io_inputx | io_inputy;
  wire [31:0] _T_6 = io_inputx + io_inputy;
  wire [31:0] _T_9 = io_inputx - io_inputy;
  wire [31:0] _T_11 = io_inputx;
  wire [31:0] _T_14 = $signed(io_inputx) >>> io_inputy[4:0];
  wire [31:0] _T_18 = io_inputx ^ io_inputy;
  wire [31:0] _T_21 = io_inputx >> io_inputy[4:0];
  wire [31:0] _T_24 = io_inputy;
  wire [62:0] _GEN_15 = {{31'd0}, io_inputx};
  wire [62:0] _T_28 = _GEN_15 << io_inputy[4:0];
  wire [31:0] _T_31 = ~_T_3;
  wire  _GEN_1 = io_operation == 4'hd ? io_inputx == io_inputy : io_operation == 4'he & io_inputx != io_inputy; // @[]
  wire  _GEN_2 = io_operation == 4'hc ? io_inputx >= io_inputy : _GEN_1; // @[]
  wire  _GEN_3 = io_operation == 4'hb ? $signed(io_inputx) >= $signed(io_inputy) : _GEN_2; // @[]
  wire [31:0] _GEN_4 = io_operation == 4'ha ? _T_31 : {{31'd0}, _GEN_3}; // @[]
  wire [62:0] _GEN_5 = io_operation == 4'h8 ? _T_28 : {{31'd0}, _GEN_4}; // @[]
  wire [62:0] _GEN_6 = io_operation == 4'h9 ? {{62'd0}, $signed(_T_11) < $signed(_T_24)} : _GEN_5; // @[]
  wire [62:0] _GEN_7 = io_operation == 4'h2 ? {{31'd0}, _T_21} : _GEN_6; // @[]
  wire [62:0] _GEN_8 = io_operation == 4'h0 ? {{31'd0}, _T_18} : _GEN_7; // @[]
  wire [62:0] _GEN_9 = io_operation == 4'h1 ? {{62'd0}, io_inputx < io_inputy} : _GEN_8; // @[]
  wire [62:0] _GEN_10 = io_operation == 4'h3 ? {{31'd0}, _T_14} : _GEN_9; // @[]
  wire [62:0] _GEN_11 = io_operation == 4'h4 ? {{31'd0}, _T_9} : _GEN_10; // @[]
  wire [62:0] _GEN_12 = io_operation == 4'h7 ? {{31'd0}, _T_6} : _GEN_11; // @[]
  wire [62:0] _GEN_13 = io_operation == 4'h5 ? {{31'd0}, _T_3} : _GEN_12; // @[]
  wire [62:0] _GEN_14 = io_operation == 4'h6 ? {{31'd0}, _T_1} : _GEN_13; // @[]
  assign io_result = _GEN_14[31:0];
endmodule
