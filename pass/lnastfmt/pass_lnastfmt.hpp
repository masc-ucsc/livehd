//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <memory>
#include <string_view>

#include "lnast.hpp"
#include "pass.hpp"

class Pass_lnastfmt : public Pass {
protected:
  void parse_ln(const std::shared_ptr<Lnast>& ln, Eprp_var& var, const mmap_lib::str & module_name);
  void observe_lnast(Lnast* ln);
  void process_node(Lnast* ln, const mmap_lib::Tree_index& it);

  absl::flat_hash_map<mmap_lib::str, mmap_lib::str> ref_hash_map;

  static bool is_temp_var(const mmap_lib::str & test_string);
  static bool is_ssa(const mmap_lib::str & test_string);

  Lnast_node duplicate_node(std::shared_ptr<Lnast>& lnastfmted, const std::shared_ptr<Lnast>& ln, const mmap_lib::Tree_index& it);

public:
  static void fmt_begin(Eprp_var& var);
  Pass_lnastfmt(const Eprp_var& var);
  static void setup();
};
