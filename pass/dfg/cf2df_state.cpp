//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cf2df_state.hpp"
using std::unordered_map;
using std::string;

void CF2DF_State::update_reference(const string &v, Index_ID n) {
  last_refs[v] = n;
  if (!table.has(v)) table.add(v);
}

unordered_map<string, Index_ID> CF2DF_State::inputs() const {
  return filter_util([](const CF2DF_State *s, const string &v) { return s->is_input(v); });
}

unordered_map<string, Index_ID> CF2DF_State::outputs() const {
  return filter_util([](const CF2DF_State *s, const string &v) { return s->is_output(v); });
}

unordered_map<string, Index_ID> CF2DF_State::filter_util(filter fproc) const {
  unordered_map<string, Index_ID> rtrn;

  for (const auto &pair : references()) {
    if (fproc(this, pair.first))
      rtrn[pair.first] = pair.second;
  }

  return rtrn;
}