//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <memory>
#include <string_view>
#include "lnast.hpp"
#include "pass.hpp"

class Pass_lnastfmt : public Pass {
protected:
  void parse_ln(std::shared_ptr<Lnast> ln, Eprp_var& var, std::string_view module_name);
  void observe_lnast(Lnast* ln);
  void process_node(Lnast* ln, const mmap_lib::Tree_index &it);    
  absl::flat_hash_map<std::string_view, std::string_view> ref_hash_map;
  bool is_temp_var(std::string_view test_string);
  bool is_ssa(std::string_view test_string);

public:
  static void fmt_begin(Eprp_var& var);
  Pass_lnastfmt(const Eprp_var& var);
  static void setup();
};
