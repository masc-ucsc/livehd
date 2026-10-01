//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lnast.hpp"

// A private SSA version (`<base>___ssa_<N>`) is minted by upass.ssa only. A
// tree that already carries some (an SSA'd body copied into a new definition,
// e.g. a rolled loop's lifted body) must demote them before it is SSA'd again,
// and every producer of such a tree must demote by the SAME rule so the names
// it binds from outside still match. Shared by upass.ssa and the loop roller.
namespace upass {

// The separator and first-digit offsets of a private SSA version suffix.
inline std::optional<std::pair<std::size_t, std::size_t>> stale_ssa_suffix(std::string_view name) {
  const auto pos = name.rfind("___ssa_");
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t digit_pos = pos + 7;
  if (digit_pos >= name.size()) {
    return std::nullopt;
  }
  for (std::size_t i = digit_pos; i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return std::nullopt;
    }
  }
  return std::pair{pos, digit_pos};
}

// Rename every private version under `root` to `<base>__w<N>` (N bumped past
// any name already in the tree), consistently: every occurrence of one stale
// name lands on the same demoted name, which preserves the def/use links.
// Each demoted name is recorded on `ln` (Lnast::ssa_demoted_base) so it stays
// distinguishable from a source-authored `x__w<N>`.
// Returns the stale -> demoted map (empty when nothing was renamed).
inline absl::flat_hash_map<std::string, std::string> demote_stale_ssa(Lnast& ln, const Lnast_nid& root) {
  absl::flat_hash_map<std::string, std::string> demote;
  // Freshly parsed trees cannot contain the private suffix: check that cheap
  // path before collecting every node name into the collision set.
  bool                                          has_stale = false;
  for (auto nid : ln.depth_preorder(root)) {
    if (!nid.is_invalid() && stale_ssa_suffix(ln.get_name(nid))) {
      has_stale = true;
      break;
    }
  }
  if (!has_stale) {
    return demote;
  }
  absl::flat_hash_set<std::string> taken;  // every name in the tree
  for (auto nid : ln.depth_preorder(root)) {
    if (!nid.is_invalid()) {
      taken.emplace(ln.get_name(nid));
    }
  }
  for (auto nid : ln.depth_preorder(root)) {
    if (nid.is_invalid()) {
      continue;
    }
    const std::string_view nm     = ln.get_name(nid);
    const auto             suffix = stale_ssa_suffix(nm);
    if (!suffix) {
      continue;
    }
    auto it = demote.find(nm);
    if (it == demote.end()) {
      const auto [pos, d] = *suffix;
      const std::string base(nm.substr(0, pos));
      uint64_t          version = 0;
      for (std::size_t i = d; i < nm.size() && version < (uint64_t{1} << 40); ++i) {
        version = version * 10 + static_cast<uint64_t>(nm[i] - '0');
      }
      std::string cand;
      do {
        cand = base + "__w" + std::to_string(version);
        ++version;
      } while (taken.contains(cand));
      taken.emplace(cand);
      ln.note_ssa_demoted(cand, base);
      it = demote.emplace(std::string(nm), std::move(cand)).first;
    }
    ln.set_name(nid, it->second);  // interns the demoted name
  }
  return demote;
}

}  // namespace upass
