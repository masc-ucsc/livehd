// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "prp_ast_facade.hpp"

namespace prp_sim {
struct Param_raw {
  std::string name;
  bool        required = false;
  TSNode      default_node{};
  std::string type;
  unsigned    width         = 0;  // signed value width (unsigned parameters include a zero sign bit)
  size_t      supplied_word = 0;  // ABI word immediately after the value

  bool                       is_string = false;  // the default is a quoted string: the parameter is a string handle
  std::optional<std::string> string_default;
  bool                       dynamic = false;  // the default is an expression the test evaluates itself (not a literal)
};

std::vector<Param_raw> read_params_raw(const std::string& src, TSNode test);
}  // namespace prp_sim
