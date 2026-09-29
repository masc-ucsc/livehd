// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Domino gate model (pass/domino, phase P1 of pass/domino/README.md).
//
// A domino gate is one NMOS pull-down network: transistors in series are AND,
// transistors in parallel are OR, and any series-parallel (SP) arrangement is
// allowed. The gate output rises when the network conducts, so the function it
// computes must be POSITIVE UNATE in the rails it reads. This file answers, for
// a truth table over at most kMaxVars variables:
//
//   * which polarity (rail) every variable has to be read in, or "binate" when
//     no single-gate implementation exists;
//   * the cheapest SP network (fewest transistors) whose longest series path
//     -- the NMOS stack -- stays within a bound;
//   * whether the gate fits the machine's limits (fan-in k, stack D, budget B).
//
// The minimum-transistor search is a memoized DP over the functions reachable
// by cofactoring (at most 3^n of them): disjoint OR/AND decompositions through
// the prime implicants / implicates, plus the two Shannon shapes
// x*f1 + f0 and f1*(x + f0) (valid because f0 <= f1 for a monotone f). It is
// exact against a brute force over every monotone function of <= 4 variables
// (domino_gate_test.cpp) and is a heuristic beyond that: a cheaper network may
// exist, which only ever UNDERSTATES what fits, matching the "lower bound"
// stance of the design-space study this pass reproduces.
//
// The complement of a gate (its dual-rail twin) is NOT the dual tree of the
// same formula: it is the minimum tree of the DUAL function over the opposite
// rails, and its stack is bounded by the widest prime implicate, so a wide OR
// has a deep twin. fit(...) with Rail::neg answers exactly that question.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace livehd::domino {

inline constexpr int kMaxVars   = 8;
inline constexpr int kMaxPoints = 1 << kMaxVars;  // 256 truth-table entries
inline constexpr int kInfCost   = 1 << 20;        // "no network within the bound"

// Truth table of a function over n <= kMaxVars variables. Bit x (0 <= x < 2^n)
// holds f(x) where bit j of x is variable j. Bits at or above 2^n stay zero.
struct Truth {
  std::array<uint64_t, 4> w{};

  [[nodiscard]] bool get(int x) const { return ((w[static_cast<size_t>(x >> 6)] >> (x & 63)) & 1u) != 0; }
  void               set(int x, bool v) {
    auto&          word = w[static_cast<size_t>(x >> 6)];
    const uint64_t bit  = uint64_t{1} << (x & 63);
    word                = v ? (word | bit) : (word & ~bit);
  }
  friend bool operator==(const Truth&, const Truth&) = default;
  template <typename H>
  friend H AbslHashValue(H h, const Truth& t) {
    return H::combine(std::move(h), t.w[0], t.w[1], t.w[2], t.w[3]);
  }
};

[[nodiscard]] Truth       truth_const(bool v, int n);
[[nodiscard]] Truth       truth_var(int j, int n);
[[nodiscard]] Truth       truth_not(const Truth& f, int n);
[[nodiscard]] Truth       truth_and(const Truth& a, const Truth& b);
[[nodiscard]] Truth       truth_or(const Truth& a, const Truth& b);
[[nodiscard]] Truth       truth_xor(const Truth& a, const Truth& b);
// f with variable j fixed to `val`; the result no longer depends on j.
[[nodiscard]] Truth       cofactor(const Truth& f, int n, int j, bool val);
// g(x) = f(x with bit j toggled): reads variable j in the opposite polarity.
[[nodiscard]] Truth       flip_var(const Truth& f, int n, int j);
// The dual f^d(x) = !f(!x). For a monotone f, the twin gate computes f^d over
// the opposite rails, and the primes of f^d are the prime implicates of f.
[[nodiscard]] Truth       dual(const Truth& f, int n);
[[nodiscard]] bool        is_const(const Truth& f, int n);
[[nodiscard]] bool        depends_on(const Truth& f, int n, int j);
[[nodiscard]] uint32_t    support_mask(const Truth& f, int n);
[[nodiscard]] Truth       truth_from_bits(std::string_view bits, int n);  // "0111" = MSB x=2^n-1 first
[[nodiscard]] std::string to_bits(const Truth& f, int n);

enum class Unate : uint8_t { independent, positive, negative, binate };
[[nodiscard]] std::array<Unate, kMaxVars> unateness(const Truth& f, int n);
[[nodiscard]] std::string_view            to_string(Unate u);

// Prime implicants of a POSITIVE unate f, as variable masks (bit j = variable
// j), i.e. the minimal true points. Every SP network for f has a series path
// per prime, so the smallest possible stack is the largest prime.
[[nodiscard]] std::vector<uint32_t> prime_implicants(const Truth& f, int n);

// A series-parallel formula: the transistor network of one gate. Leaves are
// variables read in their positive polarity; the caller applies Gate_fit's
// leaf rails outside.
struct Sp_formula {
  enum class Kind : uint8_t { lit, series, parallel };
  struct Node {
    Kind             kind = Kind::lit;
    int              var  = -1;  // Kind::lit only
    std::vector<int> kids;       // series / parallel children (node indices)
  };
  std::vector<Node> nodes;
  int               root        = -1;
  int               transistors = 0;  // number of leaves
  int               stack       = 0;  // longest series path (NMOS stack depth)
  int               width       = 0;  // longest parallel path (the twin's stack lower bound)

  [[nodiscard]] bool        evaluate(uint32_t x) const;
  [[nodiscard]] std::string to_string(std::span<const std::string> names = {}) const;  // a(b+c)+bc
  void                      finalize();  // recompute transistors/stack/width from the tree
};

struct Gate_limits {
  int k      = 8;   // logical variables per gate
  int stack  = 4;   // series NMOS per path
  int budget = 16;  // transistors in the network
};

enum class Rail : uint8_t { pos, neg };
enum class Fit_reason : uint8_t { ok, constant, binate, fanin, stack, budget };
[[nodiscard]] std::string_view to_string(Fit_reason r);

struct Gate_fit {
  Fit_reason                 reason = Fit_reason::constant;
  std::array<Rail, kMaxVars> leaf_rail{};      // rail each support variable is read in
  uint32_t                   support     = 0;  // variables the gate reads
  uint32_t                   binate_mask = 0;  // variables that block a single-gate form
  int                        max_prime   = 0;  // stack lower bound (largest prime implicant)
  std::optional<Sp_formula>  formula;          // set when reason is ok

  [[nodiscard]] bool ok() const { return reason == Fit_reason::ok; }
};

// Memoized minimum-transistor SP factoring plus the gate-fit decision. Keep one
// instance per mapping run: cut functions repeat and the memo is the win.
//
// Search space, per function g over its m support variables and stack bound d:
//   * m <= 4: an exact table (closure of the literals under AND/OR, 166
//     functions x 4 stack bounds), unless set_exact_small(false);
//   * disjoint parallel / series decompositions (co-occurrence components of
//     the prime implicants / implicates);
//   * Shannon shapes x*g1 + g0 and g1*(x + g0);
//   * algebraic division by every kernel K of the prime SOP (g = K*Q + R) and,
//     through the dual, by every kernel of the prime POS (g = (K'+Q')*R').
class Sp_factorer {
public:
  // Cheapest SP tree for a positive unate, non-constant f with stack <=
  // max_stack and at most max_transistors leaves. nullopt when none exists
  // (the largest prime exceeds max_stack, or every tree is over the budget).
  // The budget is what keeps the search bounded: it is branch-and-bound on
  // the transistor count, so always pass the real gate budget when you have
  // one. Unbounded queries on wide symmetric functions can take minutes.
  [[nodiscard]] std::optional<Sp_formula> factor(const Truth& f, int n, int max_stack, int max_transistors = kInfCost);
  // Minimum transistors for the same question, kInfCost when none within the budget.
  [[nodiscard]] int                       cost(const Truth& f, int n, int max_stack, int max_transistors = kInfCost);

  // Can f (or !f for Rail::neg) be ONE domino gate under `lim`? Chooses the
  // rail of every variable, factors, and reports the first limit that fails
  // in the order constant, binate, fanin, stack, budget.
  [[nodiscard]] Gate_fit fit(const Truth& f, int n, const Gate_limits& lim, Rail out_rail = Rail::pos);

  // Testing knob: false runs the heuristic search on <= 4 variables too, so a
  // brute force can measure the heuristic against the exact table.
  void                 set_exact_small(bool v) { exact_small_ = v; }
  [[nodiscard]] size_t memo_size() const { return memo_.size(); }
  void                 clear() { memo_.clear(); }

private:
  struct Key {
    Truth       f;
    int8_t      m                                  = 0;
    int8_t      d                                  = 0;
    friend bool operator==(const Key&, const Key&) = default;
    template <typename H>
    friend H AbslHashValue(H h, const Key& k) {
      return H::combine(std::move(h), k.f, k.m, k.d);
    }
  };
  // One way to write g: (product of `series`) + (sum of `parallel`). Each part
  // is a positive unate non-constant function over the same m variables.
  struct Cand {
    std::vector<Truth> series;
    std::vector<Truth> parallel;
  };
  // f over n variables compacted to its m support variables; var[i] is the
  // original index of compact variable i.
  struct Compact {
    Truth                     g;
    int                       m = 0;
    std::array<int, kMaxVars> var{};
  };

  // cost <= bound: exact minimum. Otherwise no tree with <= bound leaves
  // exists; a later query with a larger bound recomputes.
  struct Memo {
    int cost  = kInfCost;
    int bound = -1;
  };
  absl::flat_hash_map<Key, Memo> memo_;
  bool                           exact_small_ = true;

  [[nodiscard]] static Compact           compact(const Truth& f, int n);
  [[nodiscard]] static std::vector<Cand> candidates(const Truth& g, int m);
  // Cheapest cost of a candidate under stack d and leaf budget `bound`
  // (kInfCost when over); fills the per-series-part stack allocation when
  // `alloc` is non-null.
  int                                    cand_cost(const Cand& c, int m, int d, int bound, std::vector<int>* alloc);
  int                                    best_c(const Truth& g, int m, int d, int bound);
  int                                    tighten(const Truth& g, int m, int d, int bound);
  int build_c(const Truth& g, int m, int d, int bound, Sp_formula& out, const std::array<int, kMaxVars>& relabel);
};

}  // namespace livehd::domino
