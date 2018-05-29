#ifndef CFG_NODE_DATA_H_
#define CFG_NODE_DATA_H_

#include "nodetype.hpp"
#include <sstream>
#include <string>
#include <vector>

const int  OPERANDS_INIT_SIZE = 4;   // most CFG nodes will have 4 operands
const char ENCODING_DELIM     = '|'; // deliminator used to encode CFG data into wirename field
const int  CFG_METADATA_COUNT = 5;
const std::string EMPTY_MARKER = "<none>";
const std::string COND_BR_MARKER = "if";

class CFG_Node_Data {
public:
  CFG_Node_Data(const LGraph *g, Index_ID node);
  CFG_Node_Data(const std::string &parser_raw);
  CFG_Node_Data(const std::string &t, const std::vector<std::string> &ops, const std::string &ot)
    : target(t), operands(ops), oper_text(ot) { }
  CFG_Node_Data(const CFG_Node_Data &o) : target(o.get_target()), operands(o.get_operands()), oper_text(o.get_operator()) { }

  std::string encode() const;

  const std::string              &get_target() const { return target; }
  const std::string              &get_operator() const { return oper_text; }
  const std::vector<std::string> &get_operands() const { return operands; }

private:
  std::string              target;
  std::vector<std::string> operands;
  std::string              oper_text;
};

#endif
