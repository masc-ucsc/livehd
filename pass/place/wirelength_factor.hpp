// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
// A two-level implementation of one scalar cell. A top input equal to the
// original arity is the divisor result. All other indices name original pins.
struct Wire_factor {
  std::string           divisor, top;
  std::vector<unsigned> bound, free;
  // Optional second partial term. Input arity names the first partial
  // result and arity + 1 names this term at the final gate.
  std::string           intermediate{};
  std::vector<unsigned> intermediate_args{};
};
// Push the common input inversion of all successor gates ahead of the
// fanout point. Other arguments are independent for each successor.
struct Wire_push {
  std::string           root, load;
  std::vector<unsigned> root_arguments, load_arguments;
  bool                  bypass = false;
};
std::optional<Wire_push> wire_push(const liberty::Comb_cell& root, const liberty::Comb_cell& load, unsigned input,
                                   const std::map<std::string, liberty::Comb_cell>& cells);
std::vector<Wire_factor> wire_factors(const liberty::Comb_cell& original, const std::map<std::string, liberty::Comb_cell>& cells);
bool                     equivalent_factor(const liberty::Comb_cell& original, const Wire_factor& factor,
                                           const std::map<std::string, liberty::Comb_cell>& cells);
}  // namespace livehd::placement
