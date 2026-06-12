
#include "eprp_var.hpp"

#include <algorithm>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "diag.hpp"

void Eprp_var::add(const Eprp_dict& _dict) {
  for (const auto& var : _dict) {
    add(var.first, var.second);
  }
}

void Eprp_var::add(Eprp_lnasts& _lns) {
  for (const auto& ln : _lns) {
    lnasts.emplace_back(ln);
  }
}

void Eprp_var::add(const Eprp_var& _var) { add(_var.dict); }

void Eprp_var::add(const std::shared_ptr<hhds::Graph>& graph) {
  if (graph && std::find(graphs.begin(), graphs.end(), graph) == graphs.end()) {
    graphs.push_back(graph);
  }
}

void Eprp_var::add(std::unique_ptr<Lnast> lnast) { lnasts.emplace_back(std::move(lnast)); }

void Eprp_var::add(const std::shared_ptr<Lnast>& lnast) { lnasts.emplace_back(lnast); }

void Eprp_var::add(std::string_view name, std::string_view value) {
  if (name == "files") {
    for (const auto& v : absl::StrSplit(value, ',')) {
      std::string v_str(v);
      if (access(v_str.c_str(), R_OK) == -1) {
        livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                           .code     = "missing-file",
                                                           .category = "io",
                                                           .pass     = "cli",
                                                           .message  = std::format("input file `{}` is not accessible", v_str),
                                                           .hint     = "check the path and the `files:` argument"});
        throw std::runtime_error("not valid file");
      }
    }
  } else if (name == "path" || name == "src_path") {
    std::string path(value);
    if (access(path.c_str(), R_OK) == -1) {
      mkdir(path.c_str(), 0755);
      if (access(path.c_str(), R_OK) == -1) {
        livehd::diag::sink().emit(
            livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                     .code     = "missing-path",
                                     .category = "io",
                                     .pass     = "cli",
                                     .message  = std::format("path `{}` is not accessible and could not be created", path),
                                     .hint     = "check the `path:`/`src_path:` argument and permissions"});
        std::print("ERROR: path {} is not accessible (skipping)\n", path);
        throw std::runtime_error("not valid file");
      }
    }
  }

  dict[name] = value;
}

void Eprp_var::replace(const Eprp_var::Eprp_lnasts& lns) {
  lnasts.clear();
  lnasts = lns;
}

void Eprp_var::replace(const std::shared_ptr<Lnast>& lnast_old, std::shared_ptr<Lnast>& lnast_new) {
  auto itr = std::find(lnasts.begin(), lnasts.end(), lnast_old);

  if (itr != lnasts.cend()) {
    auto indx       = std::distance(lnasts.begin(), itr);
    lnasts.at(indx) = std::move(lnast_new);
  } else {
    I(false, "lnast provided is not found in the vector");
  }
}

std::string_view Eprp_var::get(std::string_view name, std::string_view default_value) const {
  const auto& elem = dict.find(name);
  if (elem == dict.end()) {
    return default_value;
  }
  return elem->second;
}

std::string_view Eprp_var::get_stage(std::string_view name, std::string_view default_value) const {
  const auto& elem = stage_dict.find(name);
  if (elem == stage_dict.end()) {
    return default_value;
  }
  return elem->second;
}
