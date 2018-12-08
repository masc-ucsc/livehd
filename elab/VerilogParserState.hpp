#ifndef VERILOGPARSERSTATE_HPP
#define VERILOGPARSERSTATE_HPP

#include <stdio.h>

#include <bitset>
#include <map>
#include <string>

#include "VerilogEnums.hpp"

#define VERILOG_PARSER_STATE_CNT 6

class VerilogParserState {
  public:
    VerilogParserState();
    ~VerilogParserState();

    void toggle(VerilogParserState_e state);
    bool in(VerilogParserState_e state);

    std::bitset<VERILOG_PARSER_STATE_CNT> stateList;
    std::map<VerilogParserState_e, std::string> string_map;
    std::map<std::string, VerilogParserState_e> enum_map;
};

#endif //VERILOGPARSERSTATE_HPP
