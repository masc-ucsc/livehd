module ALUControl(
  input        clock,
  input        reset,
  input        io_aluop,
  input        io_itype,
  input  [6:0] io_funct7,
  input  [2:0] io_funct3,
  output [3:0] io_operation
);
  wire [2:0] _GEN_0 = io_itype | io_funct7 == 7'h0 ? 3'h7 : 3'h4; // @[]
  wire [1:0] _GEN_1 = io_funct7 == 7'h0 ? 2'h2 : 2'h3; // @[]
  wire [2:0] _GEN_2 = io_funct3 == 3'h6 ? 3'h5 : 3'h6; // @[]
  wire [2:0] _GEN_3 = io_funct3 == 3'h5 ? {{1'd0}, _GEN_1} : _GEN_2; // @[]
  wire [2:0] _GEN_4 = io_funct3 == 3'h4 ? 3'h0 : _GEN_3; // @[]
  wire [2:0] _GEN_5 = io_funct3 == 3'h3 ? 3'h1 : _GEN_4; // @[]
  wire [3:0] _GEN_6 = io_funct3 == 3'h2 ? 4'h9 : {{1'd0}, _GEN_5}; // @[]
  wire [3:0] _GEN_7 = io_funct3 == 3'h1 ? 4'h8 : _GEN_6; // @[]
  wire [3:0] _GEN_8 = io_funct3 == 3'h0 ? {{1'd0}, _GEN_0} : _GEN_7; // @[]
  assign io_operation = io_aluop ? _GEN_8 : 4'h7; // @[]
endmodule
