
#include "absl/strings/str_split.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "eprp_var.hpp"

void Eprp_var::add(const Eprp_dict &_dict) {
  for (const auto &var : _dict) {
    add(var.first, var.second);
  }
}

void Eprp_var::add(const Eprp_lgs &_lgs) {
  for (const auto &lg : _lgs) {
    add(lg);
  }
}

void Eprp_var::add(const Eprp_var &_var) {
  add(_var.lgs);
  add(_var.dict);
}

void Eprp_var::add(LGraph *lg) {
  if (std::find(lgs.begin(), lgs.end(), lg) == lgs.end()) lgs.push_back(lg);
}

void Eprp_var::add(std::unique_ptr<Lnast> lnast) {
  lnasts.emplace_back(std::move(lnast));
}

void Eprp_var::add(const std::string &name, std::string_view value) {
  if (name == "files") {
    std::vector<std::string> svector = absl::StrSplit(value,',');

    for (const auto& v : svector) {
      const std::string file{v};
      if (access(file.c_str(), R_OK)==-1) {
        fmt::print("ERROR: file {} is not accessible (skipping)\n", v);
        throw std::runtime_error("not valid file");
      }
    }
  } else if (name == "path") {
    const std::string path { value };
    if (access(path.c_str(), R_OK)==-1) {
      mkdir(path.c_str(),0755);
      if (access(path.c_str(), R_OK)==-1) {
        fmt::print("ERROR: path {} is not accessible (skipping)\n", path);
        throw std::runtime_error("not valid file");
      }
    }
  }
  dict[name] = value;
}
void Eprp_var::replace(std::shared_ptr<Lnast> lnast_old, std::unique_ptr<Lnast> lnast_new) {
  std::vector<std::shared_ptr<Lnast> >::iterator itr = std::find(lnasts.begin(), lnasts.end(), lnast_old);
  
  //auto indx = lnasts.begin();
  if (itr != lnasts.cend()) {
    auto indx = std::distance(lnasts.begin(), itr);
    lnasts.at(indx) = std::move(lnast_new);
  } else {
    I(false, "lnast provided is not found in the vector");
  }
  //lnasts[indx] = lnast_new;
}

void Eprp_var::delete_label(const std::string &name) {
  auto it = dict.find(name);
  if (it != dict.end()) dict.erase(it);
}

std::string_view Eprp_var::get(const std::string &name) const {
  const auto &elem = dict.find(name);
  if (elem == dict.end()) {
    static const std::string empty("");
    return empty;
  }
  return elem->second;
}

