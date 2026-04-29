//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>

#include "hhds/attr.hpp"

namespace lnast::attrs {

// Source location: pos1, pos2, line, tok packed into one trivially-copyable
// struct so a single attribute lookup recovers everything except the textual
// payload (the token text rides on hhds::attrs::name).
struct loc_t {
  struct value_type {
    uint64_t pos1 = 0;
    uint64_t pos2 = 0;
    uint32_t line = 0;
    uint8_t  tok  = 0;  // Token_id; 0 == Token_id_nop
  };
  using storage = hhds::flat_storage;
};
inline constexpr loc_t loc{};

// Source filename. Often empty, so kept as a separate attribute that is only
// allocated when actually set.
struct fname_t {
  using value_type = std::string;
  using storage    = hhds::flat_storage;
};
inline constexpr fname_t fname{};

// SSA subscript. Heavily mutated during ssa_trans.
struct ssa_subs_t {
  using value_type = int16_t;
  using storage    = hhds::flat_storage;
};
inline constexpr ssa_subs_t ssa_subs{};

}  // namespace lnast::attrs

namespace hhds {

template <>
[[nodiscard]] inline std::string attr_tag_name<lnast::attrs::loc_t>() {
  return "lnast.loc";
}

template <>
[[nodiscard]] inline std::string attr_tag_name<lnast::attrs::fname_t>() {
  return "lnast.fname";
}

template <>
[[nodiscard]] inline std::string attr_tag_name<lnast::attrs::ssa_subs_t>() {
  return "lnast.ssa_subs";
}

}  // namespace hhds
