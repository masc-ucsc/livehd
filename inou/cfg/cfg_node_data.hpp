#ifndef CFG_NODE_DATA_H_
#define CFG_NODE_DATA_H_

#include "nodetype.hpp"
#include <sstream>
#include <string>
#include <vector>

const int  OPERANDS_INIT_SIZE = 4;   // most CFG nodes will have 4 operands
const char ENCODING_DELIM     = '|'; // deliminator used to encode CFG data into wirename field

class CFG_Node_Data {
public:
  CFG_Node_Data(Node_Type_Op op, const char *data_str) : oper(op) {
    std::istringstream ss(data_str);

    assert(std::getline(ss, target, ENCODING_DELIM)); // the first var in the data_str is the target
                                                      // this read shouldn't fail

    std::string buffer;
    while(std::getline(ss, buffer, ENCODING_DELIM)) {
      if(!buffer.empty())
        operands.push_back(buffer);
    }
  }

  Node_Type_Op                    get_operator() const { return oper; }
  std::string                     get_target() const { return target; }
  const std::vector<std::string> &get_operands() const { return operands; }

private:
  Node_Type_Op             oper;
  std::string              target;
  std::vector<std::string> operands;
};

#endif
