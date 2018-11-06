//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <sstream>

#include "inou_cfg.hpp"

#include "cfg_node_data.hpp"

CFG_Node_Data::CFG_Node_Data(const LGraph *g, Index_ID node) {
  const char *data_str = g->get_node_wirename(node);

  if (data_str == nullptr) {
    target = EMPTY_MARKER;
    operator_txt = EMPTY_MARKER;
    return;
  }

#if 1
  std::string s = data_str;
  int ndiscard = 2;
  size_t pos = 0;
  std::string tmp;
  std::string delimiter = "|";

  while ((pos = s.find(delimiter)) != std::string::npos) {
    tmp = s.substr(0, pos);

    if (ndiscard==0) {
      operands.push_back(strdup(tmp.c_str()));
    }else{
      if(ndiscard==2) {
        operator_txt = tmp;
      } else {
        target = tmp;
      }
      ndiscard--;
    }
    s.erase(0, pos + delimiter.length());
  }
  std::cout << s << std::endl;


#elif 0

  int len =0;
  const char *start = data_str;
  int ndiscard = 2;

  while(*data_str) {
    if (*(data_str+len) == '|') {
      assert(ndiscard>=0);
      std::string tmp(start,len);
      if (ndiscard==0) {
        char *tmp1 = strdup(tmp.c_str());
        operands.push_back(tmp1);
        free(tmp1);
      }else{
        if(ndiscard==2) {
          operator_txt = tmp;
        } else {
          target = tmp;
        }
        ndiscard--;
      }

      data_str += len+1;
      start = data_str;
      len = 0;
    }else{
      len++;
    }
  }

#else
  std::istringstream ss(data_str);

  std::getline(ss, operator_txt, ENCODING_DELIM);
  assert(!operator_txt.empty());

  std::getline(ss, target, ENCODING_DELIM);
  assert(!target.empty());

  for(std::string buffer; std::getline(ss, buffer, ENCODING_DELIM);) {
    if (!buffer.empty())
      operands.push_back(strdup(buffer.c_str()));
  }
#endif
}

CFG_Node_Data::CFG_Node_Data(const std::string &parser_raw) {
  std::istringstream ss(parser_raw);
  std::string buffer;

  for (int i = 0; i < CFG_METADATA_COUNT; ) {   // the first few are metadata, and these reads should not fail or
                                                // we have a formatting issue

    assert(ss >> buffer);

    if (!buffer.empty()) // only count non-empty tokens
      i++;
  }

  ss >> operator_txt;
  ss >> target;

  while (ss >> buffer) { // actually save the remaining
    if (!buffer.empty())
      operands.push_back(buffer);
  }
}

std::string CFG_Node_Data::encode() const {
  std::string encd = operator_txt + ENCODING_DELIM + target + ENCODING_DELIM;

  for (const auto &op : operands)
    encd += op + ENCODING_DELIM;

  return encd;
}
