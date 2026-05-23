//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "upass_attributes_handler.hpp"

// Category-A LNAST/upass attribute handlers. Each handler enforces or
// lowers one named attribute's semantics.
//
//   * Wrap_sat_handler — `wrap`, `saturate`/`sat`. Distinguishes the
//     declaration-site assignment policy (`mut w:u4:[wrap] = 0`) from the
//     statement-level prefix (`wrap x = 0xFF`). The declaration form sets a
//     persistent policy: every assignment to that variable is narrowed.
//     The statement form narrows just the in-flight assignment and leaves
//     no sticky attribute. Narrowing math runs through tmp_fold so cassert
//     consumers see the post-narrowed value via runner_fold_fn.
//   * Const_handler — `type=const` single-bind enforcement. A `const` may
//     receive at most one non-nil assignment per cycle; a second binding
//     is a compile error.

namespace upass {
namespace attributes {

class Wrap_sat_handler : public Attribute_handler {
public:
  void on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) override;
};

class Const_handler : public Attribute_handler {
public:
  void on_attr_set(uPass_attributes& owner, std::string_view lhs, std::string_view value_text) override;
};

}  // namespace attributes
}  // namespace upass
