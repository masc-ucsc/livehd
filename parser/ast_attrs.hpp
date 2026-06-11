//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "hhds/attr.hpp"

namespace ast_parser::attrs {

// Token entry index into the scanner's token table.
struct token_entry_t {
  using value_type = uint32_t;
  using storage    = hhds::flat_storage;
};
inline constexpr token_entry_t token_entry{};

}  // namespace ast_parser::attrs

namespace hhds {

}  // namespace hhds
