//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass/aegraph/runtime: the analyses a rule guard may ask about (milestone G).
//
// Every analysis a guard can name is declared in rules/analyses.decl (with the :spec that is both the
// hypothesis of its proof obligations and the sampler of the generated fuzz) and implemented here, in
// runtime/analyses.cpp, with AEG_ANALYSIS. rulegen refuses a guard on a declared analysis that has no
// AEG_ANALYSIS, and the generated rule code calls it as `an::NAME(ctx, classes, constants)`.
//
// The facts are lazy and memoized per class in the e-graph (Egraph::analysis_cache), so a graph that never
// meets a guarded rule pays nothing. They are computed from the class's own e-nodes (the members of a union
// are equal, so a fact of any member holds for the class) and never from a `?` literal: a literal is a leaf.
// Leaves (IO, state) carry no width in the e-graph, exactly as in classic cprop, so a leaf has no bound.
//
//   Facts    an integer interval [lo, hi] (kInf = unbounded) and known-zero / known-one masks of the low 64
//            bits, for the unlimited-integer value of the class
//   polarity (base, inverted): the class is `base` or EQ(base, 0) of a 0/1 base
//   Layout   the persistent lane layout: bits [lo, hi) of the class equal bits [off, off + hi - lo) of
//            another class (or are zero) for each lane, from Concat, Set_mask, SHL and Or/Sum of
//            disjoint shifted fields. A packed word stays one class; a window read asks the layout

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "runtime/match.hpp"

namespace livehd::aegraph::rt {

using Wide = __int128;
inline constexpr Wide kInf = Wide{1} << 100;

struct Facts {
  Wide     lo = -kInf;  // -kInf = unbounded below
  Wide     hi = kInf;   // kInf = unbounded above
  uint64_t kz = 0;      // bits of the low 64 known to be 0
  uint64_t ko = 0;      // bits of the low 64 known to be 1

  [[nodiscard]] bool nonneg() const { return lo >= 0; }
  [[nodiscard]] bool bounded() const { return lo > -kInf && hi < kInf; }
};

inline constexpr int64_t kLaneInf = int64_t{1} << 40;

// Bits [lo, hi) of the class are bits [off, off + hi - lo) of `src`, or zero.
struct Lane {
  int64_t  lo   = 0;
  int64_t  hi   = 0;
  Class_id src  = kNoClass;
  int64_t  off  = 0;
  bool     zero = false;
};
using Layout = std::vector<Lane>;  // ascending, contiguous from 0 to kLaneInf

// What an e-graph remembers about its classes (Egraph::analysis_cache).
struct Cached_facts {
  Facts  facts;
  int8_t depth = 0;  // the recursion budget the fact was computed with
};

struct Analysis_cache {
  absl::flat_hash_map<Class_id, Facts>                        seeds;  // declared bounds of leaf classes (seed_leaf)
  absl::flat_hash_map<Class_id, Cached_facts>                 facts;
  absl::flat_hash_map<Class_id, std::shared_ptr<const Layout>> layouts;
};

// A leaf class (a graph input, a register output) whose pin declares `bits` physical bits: [0, 2^bits - 1]
// unsigned, [-2^(bits-1), 2^(bits-1) - 1] signed. Declared widths define the value domain of an input or a
// register (the simulator and the LEC encoder both truncate to them), so this is a fact about the class.
void seed_leaf(const Egraph& eg, Class_id cls, int bits, bool is_unsigned);
// The value facts of a class. The class id must be valid.
[[nodiscard]] Facts facts_of(const Egraph& eg, Class_id cls);
// (base, inverted) of a 0/1 class: the class is `base`, or EQ(base, 0) when inverted. A class that is no
// recognizable boolean is its own base, not inverted, and `valid` is false.
struct Polarity {
  Class_id base     = kNoClass;
  bool     inverted = false;
  bool     valid    = false;
};
[[nodiscard]] Polarity polarity_of(const Egraph& eg, Class_id cls);
// The lane layout of a class (a single identity lane for a class without structure).
[[nodiscard]] std::shared_ptr<const Layout> layout_of(const Egraph& eg, Class_id cls);
// The lane of `layout` that holds the whole window [lo, hi), or null.
[[nodiscard]] const Lane* find_lane(const Layout& layout, int64_t lo, int64_t hi);

// ---------------------------------------------------------------------------
// Registry of the implemented analyses (the soundness fuzz walks it)
// ---------------------------------------------------------------------------

using Analysis_fn = bool (*)(Ctx&, std::span<const Class_id>, std::span<const int64_t>);

struct Analysis_info {
  const char* name    = "";
  size_t      n_class = 0;  // class arguments
  size_t      n_const = 0;  // constant arguments
  Analysis_fn fn      = nullptr;
};

class Analysis_registry {
public:
  static Analysis_registry&                 instance();
  void                                      add(const Analysis_info& info);
  [[nodiscard]] const std::vector<Analysis_info>& all() const { return list_; }
  [[nodiscard]] const Analysis_info*        find(std::string_view name) const;

private:
  std::vector<Analysis_info> list_;
};

struct Analysis_registrar {
  Analysis_registrar(const char* name, size_t n_class, size_t n_const, Analysis_fn fn) {
    Analysis_registry::instance().add({name, n_class, n_const, fn});
  }
};

}  // namespace livehd::aegraph::rt

// AEG_ANALYSIS(name, n_class, n_const) { body } -- in the body `ctx` (Ctx&), `cls` (the class arguments) and
// `k` (the constant arguments, int64) are visible. The function lives in livehd::aegraph::rt::an, which is
// where the generated rule code looks for it.
#define AEG_ANALYSIS(NAME, NCLASS, NCONST)                                                                        \
  namespace livehd::aegraph::rt::an {                                                                             \
  bool NAME(::livehd::aegraph::rt::Ctx&, std::span<const ::livehd::aegraph::Class_id>, std::span<const int64_t>); \
  }                                                                                                               \
  static const ::livehd::aegraph::rt::Analysis_registrar aeg_an_reg_##NAME{                                       \
      #NAME, NCLASS, NCONST, &::livehd::aegraph::rt::an::NAME};                                                   \
  bool livehd::aegraph::rt::an::NAME([[maybe_unused]] ::livehd::aegraph::rt::Ctx&                     ctx,         \
                                     [[maybe_unused]] std::span<const ::livehd::aegraph::Class_id> cls,       \
                                     [[maybe_unused]] std::span<const int64_t>                         k)
