
#include <algorithm>
#include <utility>

#include "eprp_var.hpp"

void Eprp_var::add(const Eprp_dict &_dict) {
  for (const auto& var : _dict) {
    add(var.first, var.second);
  }
}

void Eprp_var::add(const Eprp_lgs &_lgs) {
  for (const auto& lg : _lgs) {
    add(lg);
  }
}

void Eprp_var::add(const Eprp_var &_var) {
  add(_var.lgs);
  add(_var.dict);
}

void Eprp_var::add(LGraph *lg) {
  if (std::find(lgs.begin(), lgs.end(), lg) == lgs.end())
    lgs.push_back(lg);
}

void Eprp_var::add(const std::string &name, const std::string &value) {
  dict[name] = value;
}

void Eprp_var::delete_label(const std::string &name) {
  auto it = dict.find(name);
  if (it != dict.end())
    dict.erase(it);
}

const std::string Eprp_var::get(const std::string &name) const {
  const auto &elem = dict.find(name);
  if (elem == dict.end()) {
    static const std::string empty("");
    return empty;
  }
  return elem->second;
}

/*
const std::string Eprp_var::get(const std::string &name, const std::string &def_val) const {
  const auto &elem = dict.find(name);
  if (elem == dict.end()) {
    return def_val;
  }
  return elem->second;
}*/

