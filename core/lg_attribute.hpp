#pragma once

#include <string>
#include "absl/container/flat_hash_map.h"

template <const char* name, typename Attr_data>
class Lg_attribute {
protected:
  static absl::flat_hash_map<std::string, Attr_data> lg2data;

  static std::string get_full_name(LGraph* lg) {
    return absl::StrCat(std::to_string(lg->get_lgid()), name);
  }

public:
  static Attr_data get_data(LGraph* lg) {
    return lg2data[get_full_name(lg)];
  }

  static void update_data(LGraph* lg, Attr_data& d) {
    lg2data[get_full_name(lg)] = d;
  }

  static void has_data(LGraph* lg) {
    return lg2data.contains(get_full_name(lg));
  }
};