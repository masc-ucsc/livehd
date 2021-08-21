
#include "eprp_var.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"

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

void Eprp_var::add(Eprp_lnasts &_lns) {
  for (const auto &ln : _lns) {
    lnasts.emplace_back(ln);
  }
}

void Eprp_var::add(const Eprp_var &_var) {
  add(_var.lgs);
  add(_var.dict);
}

void Eprp_var::add(Lgraph *lg) {
  if (std::find(lgs.begin(), lgs.end(), lg) == lgs.end())
    lgs.push_back(lg);
}

void Eprp_var::add(std::unique_ptr<Lnast> lnast) { lnasts.emplace_back(std::move(lnast)); }

void Eprp_var::add(const std::shared_ptr<Lnast> &lnast) { lnasts.emplace_back(lnast); }

void Eprp_var::add(const mmap_lib::str &name, const mmap_lib::str &value) {

  if (name == "files") {
    for (const auto &v : value.split(',')) {
      if (access(v.to_s().c_str(), R_OK) == -1) {
        fmt::print("ERROR: file '{}' is not accessible (skipping)\n", v);
        throw std::runtime_error("not valid file");
      }
    }
  } else if (name == "path" || name == "src_path") {
    auto path = value.to_s();
    if (access(path.c_str(), R_OK) == -1) {
      mkdir(path.c_str(), 0755);
      if (access(path.c_str(), R_OK) == -1) {
        fmt::print("ERROR: path {} is not accessible (skipping)\n", path);
        throw std::runtime_error("not valid file");
      }
    }
  }

  dict[name] = value;
}

void Eprp_var::replace(const std::shared_ptr<Lnast> &lnast_old, std::shared_ptr<Lnast> &lnast_new) {
  // lnast_old.swap(lnast_new);

  std::vector<std::shared_ptr<Lnast> >::iterator itr = std::find(lnasts.begin(), lnasts.end(), lnast_old);

  // auto indx = lnasts.begin();
  if (itr != lnasts.cend()) {
    auto indx       = std::distance(lnasts.begin(), itr);
    lnasts.at(indx) = std::move(lnast_new);
    // lnasts.at(indx).swap(lnast_new);
    // lnasts.at(indx) = lnast_new;
  } else {
    I(false, "lnast provided is not found in the vector");
  }
  // lnasts[indx] = lnast_new;
}

void Eprp_var::delete_label(const mmap_lib::str &name) {
  auto it = dict.find(name);
  if (it != dict.end())
    dict.erase(it);
}

mmap_lib::str Eprp_var::get(const mmap_lib::str &name) const {
  const auto &elem = dict.find(name);
  if (elem == dict.end()) {
    return mmap_lib::str();
  }
  return elem->second;
}
