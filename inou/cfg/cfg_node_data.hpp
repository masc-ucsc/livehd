//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CFG_NODE_DATA_H_
#define CFG_NODE_DATA_H_

#include <string>
#include <vector>
#include "absl/strings/str_split.h"
#include "lgraph.hpp"

class CFG_Node_Data {
private:
  std::string              target;
  std::vector<std::string> operands;
  std::string              operator_txt;

  static inline constexpr int               OPERANDS_INIT_SIZE = 4;   // most CFG nodes will have 4 operands
  static inline constexpr char              ENCODING_DELIM     = ','; // deliminator used to encode CFG data into wirename field
  static inline constexpr int               CFG_METADATA_COUNT = 5;
  static inline constexpr std::string_view  EMPTY_MARKER       = "<none>";
  static inline constexpr std::string_view  COND_BR_MARKER     = "if";

public:
  CFG_Node_Data(const LGraph *g, Node node);
  CFG_Node_Data(const std::string &parser_raw);
  CFG_Node_Data(const std::string &t, const std::vector<std::string> &ops, const std::string &ot)
      : target(t)
      , operands(ops)
      , operator_txt(ot) {
  }

  std::string encode() const;
  void                            modify_operator(std::string new_op) {operator_txt = new_op;}
  //SH:FIXME:ASK: avoid return a const reference of an object!?
  const std::string              &get_target()   const { return target; }
  std::string_view                get_operator() const { return operator_txt; }
  const std::vector<std::string> &get_operands() const { return operands; }

  bool is_br_marker() const { return operator_txt == CFG_Node_Data::COND_BR_MARKER; }

};

#endif
