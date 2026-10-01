//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "lnast.hpp"

// A defaulted comb input (`b:u8 = chk(a=a)`, todo 3g E) is lowered as the
// body PROLOGUE: the default expression's statements, then
// `store(__default_b, <value>)`. The default is only USED where an inlined call
// omits the argument; the comb's own module reads its input port, and a call
// that provides the argument binds the actual. There the statements that only
// compute that default are dead -- side effects included: an `assert` inside
// the default expression must not fire when the default is not taken. Shared
// by upass.tolg (the comb's own module) and the runner's comb inliner (a call
// that provides the argument) so both drop exactly the same statements.
namespace upass {

// Class indices of the top-level statements of `stmts` that only compute the
// default locals in `unused` (Lnast_io_entry::default_value_name names). The
// locals in `used` stay roots, as does every statement after the prologue (the
// last default-local store) and every name an io port's deferred width bound
// reads (a generic port's bound desugar shares the prologue). A backward
// liveness walk over the prologue: a statement survives when it defines a name
// something live reads. A check (a cassert, or control flow holding one) is a
// side effect of the default whose SEGMENT it sits in -- the statements after
// the previous default-local store up to its own store -- so it survives
// exactly when that default is not in `unused`.
inline absl::flat_hash_set<int64_t> unused_default_prologue(const Lnast& ln, const Lnast_nid& stmts,
                                                            const absl::flat_hash_set<std::string>& unused,
                                                            const absl::flat_hash_set<std::string>& used) {
  absl::flat_hash_set<int64_t> dead;
  if (unused.empty() || stmts.is_invalid()) {
    return dead;
  }
  const auto store_target = [&](const Lnast_nid& n) -> std::string {
    if (!Lnast_ntype::is_store(ln.get_type(n))) {
      return {};
    }
    const auto d = ln.get_first_child(n);
    return !d.is_invalid() && Lnast_ntype::is_ref(ln.get_type(d)) ? std::string(ln.get_name(d)) : std::string{};
  };
  std::vector<Lnast_nid> body;
  size_t                 prologue_end = 0;
  for (auto c = ln.get_first_child(stmts); !c.is_invalid(); c = ln.get_sibling_next(c)) {
    body.push_back(c);
    if (const auto t = store_target(c); !t.empty() && (unused.contains(t) || used.contains(t))) {
      prologue_end = body.size();
    }
  }
  if (prologue_end == 0) {
    return dead;
  }
  // Every ref a statement mentions (conservatively including its own
  // destination), and the names it defines: the first ref child of a
  // statement; nothing for a cassert; for control flow, what its nested
  // statements define.
  std::function<void(const Lnast_nid&, absl::flat_hash_set<std::string>&)> refs_of
      = [&](const Lnast_nid& n, absl::flat_hash_set<std::string>& out) {
          if (Lnast_ntype::is_ref(ln.get_type(n))) {
            out.insert(std::string(ln.get_name(n)));
          }
          for (auto k = ln.get_first_child(n); !k.is_invalid(); k = ln.get_sibling_next(k)) {
            refs_of(k, out);
          }
        };
  std::function<void(const Lnast_nid&, std::vector<std::string>&)> defs_of = [&](const Lnast_nid& n, std::vector<std::string>& out) {
    const auto t = ln.get_type(n);
    if (Lnast_ntype::is_cassert(t)) {
      return;
    }
    if (Lnast_ntype::is_stmts(t) || Lnast_ntype::is_if_like(t)) {
      for (auto k = ln.get_first_child(n); !k.is_invalid(); k = ln.get_sibling_next(k)) {
        defs_of(k, out);
      }
      return;
    }
    if (const auto d = ln.get_first_child(n); !d.is_invalid() && Lnast_ntype::is_ref(ln.get_type(d))) {
      out.emplace_back(ln.get_name(d));
    }
  };
  std::function<bool(const Lnast_nid&)> has_check = [&](const Lnast_nid& n) {
    if (Lnast_ntype::is_cassert(ln.get_type(n))) {
      return true;
    }
    for (auto k = ln.get_first_child(n); !k.is_invalid(); k = ln.get_sibling_next(k)) {
      if (has_check(k)) {
        return true;
      }
    }
    return false;
  };
  absl::flat_hash_set<std::string> live;
  for (size_t i = prologue_end; i < body.size(); ++i) {
    refs_of(body[i], live);
  }
  const auto& io = ln.io_meta();
  for (const auto* ports : {&io.inputs, &io.outputs}) {
    for (const auto& e : *ports) {
      for (const auto* bound : {&e.bound_max_text, &e.bound_min_text}) {
        if (!bound->empty()) {
          live.insert(*bound);
        }
      }
    }
  }
  std::string segment;  // the default local whose segment body[i] belongs to
  for (size_t i = prologue_end; i-- > 0;) {
    const auto target = store_target(body[i]);
    if (unused.contains(target) || used.contains(target)) {
      segment = target;
    }
    if (used.contains(target) || (!unused.contains(segment) && has_check(body[i]))) {
      refs_of(body[i], live);
      continue;
    }
    std::vector<std::string> defs;
    defs_of(body[i], defs);
    if (std::any_of(defs.begin(), defs.end(), [&](const std::string& d) { return live.contains(d); })) {
      refs_of(body[i], live);
      continue;
    }
    dead.insert(body[i].get_class_index().value);
  }
  return dead;
}

}  // namespace upass
