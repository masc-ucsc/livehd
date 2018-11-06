//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CFG_NODE_DATA_H_
#define CFG_NODE_DATA_H_

#include <string>
#include <vector>

#include "lgraph.hpp"

const int  OPERANDS_INIT_SIZE = 4;   // most CFG nodes will have 4 operands
const char ENCODING_DELIM     = '|'; // deliminator used to encode CFG data into wirename field
const int  CFG_METADATA_COUNT = 5;
const std::string EMPTY_MARKER = "<none>";
const std::string COND_BR_MARKER = "if";

class CFG_Node_Data {
private:
  std::string              target;
  std::vector<std::string> operands;
  std::string              operator_txt;

public:
  CFG_Node_Data(const LGraph *g, Index_ID node);
  CFG_Node_Data(const std::string &parser_raw);
  CFG_Node_Data(const std::string &t, const std::vector<std::string> &ops, const std::string &ot)
    : target(t), operands(ops), operator_txt(ot) { }
  //CFG_Node_Data(const CFG_Node_Data &o) : target(o.get_target()), operands(o.get_operands()), operator_txt(o.get_operator()) { }

  //~CFG_Node_Data(){};//dbg
  std::string encode() const;

  const std::string              &get_target() const { return target; }
  const std::string              &get_operator() const { return operator_txt; }
  const std::vector<std::string> &get_operands() const { return operands; }

};

#endif
