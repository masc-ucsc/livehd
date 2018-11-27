//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cf2df_state.hpp"
using std::string;
using std::unordered_map;

void CF2DF_State::set_alias(const string &v, Index_ID n) {
  auxtab[v] = n;
  fmt::print("set alias {} <-> {}\n", v, n);
}

unordered_map<string, Index_ID> CF2DF_State::inputs() const {
  return filter_util([](const CF2DF_State *s, const string &v) { return s->is_input(v); });
}

unordered_map<string, Index_ID> CF2DF_State::outputs() const {
  return filter_util([](const CF2DF_State *s, const string &v) { return s->is_output(v); });
}

unordered_map<string, Index_ID> CF2DF_State::filter_util(filter fproc) const {
  unordered_map<string, Index_ID> rtrn;

  for(const auto &pair : get_auxtab()) {
    if(fproc(this, pair.first))
      rtrn[pair.first] = pair.second;
  }

  return rtrn;
}