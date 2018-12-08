#ifndef VERILOGENUMS_HPP
#define VERILOGENUMS_HPP

enum class VerilogType_e {
  ERROR = -1,

  INPUT,
  OUTPUT,
  INOUT,
  WIRE,
  REG,
  TRI,

  PARAMETER,
  DEFPARAM,
};

enum class VerilogParserState_e {
  IN_MODULE,
  IN_FUNCTION,
  IN_TASK,
  IN_BLOCK,

  IN_PORT_DECLARATION,
  ERROR,
};


enum class VerilogKeyword_e {
  ERROR = -1,

  MODULE,
  ENDMODULE,
  BEGIN,
  END,
  FUNCTION,
  ENDFUNCTION,
  TASK,
  ENDTASK,

  SIGNED,
  UNSIGNED,
};

#endif //VERILOGENUMS_HPP
