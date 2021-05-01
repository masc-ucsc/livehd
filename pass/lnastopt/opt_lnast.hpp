//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include "absl/container/flat_hash_map.h"

#include "lconst.hpp"
#include "lnast.hpp"
#include "pass.hpp"

class Opt_lnast {
protected:

  int level;
  absl::flat_hash_map<std::string, std::string> level_forward_ref;
  absl::flat_hash_map<std::string, Lconst>      level_forward_val;

  void process_plus(std::shared_ptr<Lnast> ln, const Lnast_nid &lnid);
  void process_assign(std::shared_ptr<Lnast> ln, const Lnast_nid &lnid);
  void process_todo(std::shared_ptr<Lnast> ln, const Lnast_nid &lnid);

public:
  Opt_lnast(const Eprp_var &var);

  void opt(std::shared_ptr<Lnast> ln);
};
