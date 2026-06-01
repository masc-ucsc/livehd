//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "upass_attributes_handler.hpp"

// Category-A LNAST/upass attribute handlers. Each handler enforces or
// lowers one named attribute's semantics.
//
//   * Const_handler — `type=const` single-bind enforcement. A `const` may
//     receive at most one non-nil assignment per cycle; a second binding
//     is a compile error.
//
// (`wrap`/`saturate` are no longer attributes — Task 1t lowers them to a
// `wrap|sat(v=…, type=…)` library call handled in
// uPass_attributes::process_func_call.)

namespace upass {
namespace attributes {

class Const_handler : public Attribute_handler {
public:
  void on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) override;
};

}  // namespace attributes
}  // namespace upass
