//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The rule subset the tree aegraph runs (todo 2a-treeaeg step 6). An explicit
// allowlist, not a tag or width filter: a rule joins only after someone checked
// that its result is spellable as one LNAST statement and that its guard is
// structural (no leaf seeds, no bitwidth ranges reach the tree e-graph). Every
// other registered rule sits in the exclusion table with the reason, and
// upass_aeg_test checks that the two lists partition rt::Registry exactly, so a
// new rule fails that test until it is placed in one of them.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace upass::aeg {

struct Excluded_rule {
  std::string_view name;
  std::string_view reason;
};

[[nodiscard]] std::span<const std::string_view> allowed_rules();
[[nodiscard]] std::span<const Excluded_rule>    excluded_rules();

// Per rt::Rule_id (slot 0 unused): 1 when the rule is on the allowlist.
[[nodiscard]] const std::vector<uint8_t>& allowed_rule_mask();

}  // namespace upass::aeg
