//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <sstream>

#include "inou_cfg.hpp"

#include "cfg_node_data.hpp"

CFG_Node_Data::CFG_Node_Data(const LGraph *g, Index_ID node) {

  std::string_view data_str = g->get_node_wirename(node);

  if(data_str.empty()) {
    target       = EMPTY_MARKER;
    operator_txt = EMPTY_MARKER;
    return;
  }


  // FIXME: Weird code, use STL methods in string_view or abseil
  // https://en.cppreference.com/w/cpp/string/basic_string_view/find
  // https://abseil.io/docs/cpp/guides/strings#abslstrsplit-for-splitting-strings
#if 1
  int         len      = 0;
  std::string start(data_str);
  int         ndiscard = 2;

  size_t pos = 0;

  // With the new std::string_view I had to disable (not clear how to conver)

  while(data_str[pos]) {
    if(data_str[pos + len] == ENCODING_DELIM) {
      assert(ndiscard >= 0);
      std::string tmp(start, len);
      if(ndiscard == 0) {
        operands.push_back(tmp);
      } else {
        if(ndiscard == 2) {
          operator_txt = tmp;
        } else {
          target = tmp;
        }
        ndiscard--;
      }

      pos += len + 1;
      start = &data_str[pos];
      len   = 0;
    } else {
      len++;
    }
  }
#endif
}

CFG_Node_Data::CFG_Node_Data(const std::string &parser_raw) {
  std::istringstream ss(parser_raw);
  std::string        buffer;

  for(int i = 0; i < CFG_METADATA_COUNT;) { // the first few are metadata, and these reads should not fail or
                                            // we have a formatting issue

    ss >> buffer;

    if(!buffer.empty()) // only count non-empty tokens
      i++;
  }

  ss >> operator_txt;
  ss >> target;

  while(ss >> buffer) { // actually save the remaining
    if(!buffer.empty())
      operands.push_back(buffer);
  }
}

std::string CFG_Node_Data::encode() const {
  std::string encd = operator_txt + ENCODING_DELIM + target + ENCODING_DELIM;

  for(const auto &op : operands)
    encd += op + ENCODING_DELIM;

  return encd;
}
