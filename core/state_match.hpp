// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "bus_name.hpp"

namespace livehd::state_match {

struct Register_name {
  std::string key;   // consumer identity (raw hierarchy or encoder key)
  std::string name;  // normalized spelling used for matching
  int         width;
};

// A proposed packed-register/retained-bit relation. Empty entries denote
// eliminated bits. Names and shape alone never prove this relation: a consumer
// must establish its initial state and inductive transitions, and every output.
struct Register_projection {
  std::string              wide_key;
  std::vector<std::string> bit_keys;
};

inline std::vector<Register_projection> renamed_register_projections(const std::vector<Register_name>& ref,
                                                                     const std::vector<Register_name>& impl,
                                                                     char                              hierarchy_separator = '.',
                                                                     const std::set<std::string>&      excluded            = {}) {
  std::set<std::string> ref_names, impl_names;
  for (const auto& r : ref) {
    ref_names.insert(r.name);
  }
  for (const auto& r : impl) {
    impl_names.insert(r.name);
  }
  struct Group {
    std::map<int, std::string> bits;
    bool                       valid = true;
  };
  std::map<std::string, Group> groups;
  for (const auto& r : impl) {
    if (ref_names.contains(r.name)) {
      continue;
    }
    const auto p = bus_name::parse_bus_piece(r.name, true, hierarchy_separator);
    if (!p) {
      continue;
    }
    auto& group = groups[std::string(p->base)];
    if (r.width != 1 || p->index < 0 || p->index >= std::numeric_limits<int>::max()) {
      group.valid = false;
      continue;
    }
    if (!group.bits.emplace(static_cast<int>(p->index), r.key).second) {
      group.valid = false;
    }
  }
  std::map<std::string, std::vector<const Register_name*>> candidates;
  std::map<std::string, size_t>                            wide_claims;
  for (const auto& [base, group] : groups) {
    if (!group.valid || group.bits.empty() || ref_names.contains(base) || impl_names.contains(base)) {
      continue;
    }
    const int extent = group.bits.rbegin()->first + 1;
    for (const auto& wide : ref) {
      if (wide.width > 1 && wide.width >= extent && !impl_names.contains(wide.name) && !excluded.contains(wide.key)) {
        candidates[base].push_back(&wide);
        ++wide_claims[wide.key];
      }
    }
  }
  std::vector<Register_projection> out;
  for (const auto& [base, wides] : candidates) {
    if (wides.size() != 1 || wide_claims.at(wides.front()->key) != 1) {
      continue;
    }
    const auto&         wide = *wides.front();
    Register_projection projection{wide.key, std::vector<std::string>(wide.width)};
    for (const auto& [index, key] : groups.at(base).bits) {
      projection.bit_keys[index] = key;
    }
    out.push_back(std::move(projection));
  }
  return out;
}

// Total storage correspondence for a finite memory. Whole entries and split
// bits may coexist, but every represented entry must have its exact width.
struct Memory_bank {
  std::vector<std::string>              entry_keys;
  std::vector<std::vector<std::string>> bit_keys;
};
struct Memory_projection {
  std::string memory_key;
  int         entries        = 0;
  int         bits           = 0;
  bool        memory_in_impl = false;
  Memory_bank bank;
};

// A read-back cell owns one state segment below its indexed bank entry/bit.
// Preserve the storage indices while removing that model-only segment.
inline std::string memory_bank_name(std::string_view name, char separator = '.') {
  const auto piece = bus_name::parse_bus_piece(name, true, separator);
  if (piece && !piece->suffix.empty()) {
    name.remove_suffix(piece->suffix.size() + 1);
  }
  return std::string(name);
}

template <class Index, class Matched>
std::optional<Memory_bank> memory_bank(const std::string& name, int entries, int width, const Index& candidates,
                                       const Matched& matched, char separator = '.') {
  if (name.empty() || entries <= 0 || width <= 0) {
    return std::nullopt;
  }
  Memory_bank out{std::vector<std::string>(entries), std::vector<std::vector<std::string>>(entries)};
  for (int i = 0; i < entries; ++i) {
    const auto entry = bus_name::entry(name + separator + "_mem", i);
    if (auto it = candidates.find(entry); it != candidates.end()) {
      if (it->second.second != width || matched.contains(entry)) {
        return std::nullopt;
      }
      out.entry_keys[i] = it->second.first;
      continue;
    }
    for (int b = 0; b < width; ++b) {
      const auto key = bus_name::bit(entry, b);
      const auto it  = candidates.find(key);
      if (it == candidates.end() || it->second.second != 1 || matched.contains(key)) {
        return std::nullopt;
      }
      out.bit_keys[i].push_back(it->second.first);
    }
  }
  return out;
}

}  // namespace livehd::state_match
