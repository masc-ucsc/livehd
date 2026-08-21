// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "split_selfref.hpp"

#include <pthread.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <print>
#include <queue>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"       // Ntype / Ntype_op
#include "diag.hpp"       // livehd::diag::warn / err (cap diagnostic, retired-entry trip-wire)
#include "node_util.hpp"  // livehd::graph_util::* helpers

namespace livehd::graph_util {

namespace {
// Debug-print helper (mirrors the cgen_sim local; only used by split[dbg] lines).
const char* op_name(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum     : return "Sum";
    case Ntype_op::Mult    : return "Mult";
    case Ntype_op::Div     : return "Div";
    case Ntype_op::And     : return "And";
    case Ntype_op::Or      : return "Or";
    case Ntype_op::Xor     : return "Xor";
    case Ntype_op::Not     : return "Not";
    case Ntype_op::LT      : return "LT";
    case Ntype_op::GT      : return "GT";
    case Ntype_op::EQ      : return "EQ";
    case Ntype_op::SHL     : return "SHL";
    case Ntype_op::SRA     : return "SRA";
    case Ntype_op::Mux     : return "Mux";
    case Ntype_op::Hotmux  : return "Hotmux";
    case Ntype_op::Get_mask: return "Get_mask";
    case Ntype_op::Set_mask: return "Set_mask";
    case Ntype_op::Sext    : return "Sext";
    case Ntype_op::Concat  : return "Concat";
    case Ntype_op::Nconst  : return "Nconst";
    default                : return "op?";
  }
}
}  // namespace

// Break a FALSE word-level combinational loop through a PACKED wire. A single net
// `W` driven by an `Or` (a bit-field pack) whose operands occupy DISJOINT constant
// bit ranges is really a concat: a constant Get_mask slice of `W` reads only ONE
// operand. A `Concat` cell is that same pack spelled EXPLICITLY -- its lanes are
// disjoint by construction, so the footprint/disjointness proof the Or spelling
// needs is replaced by a direct lane lookup (see the Concat arms below).
// inou.cgen.sim schedules `W` as one atomic object, so a slice-read of
// `W` that (through parent logic) drives a DIFFERENT slice of that same `W` looks
// like a cycle even though the bit-level DAG is acyclic -- e.g.
//   hi = io#[2..=3];  low = hi & 3;  io = low | (a << 2);  z = io#[0..=1]
// (io[3:2] comes only from `a<<2`, io[1:0] only from `low`). The fix REDIRECTS
// each constant Get_mask slice-read of such a packed net to read DIRECTLY from the
// Or operand that drives the covered range. This is exactly value-preserving (OR
// of disjoint ranges == concat, so the sliced bits come only from that one
// operand) and drops the false word-level edge so forward_class can order the
// now-acyclic DAG. Strictly a NO-OP unless a genuine word-level comb cycle exists;
// a GENUINE bit-level loop (driver is not an Or of disjoint operands, e.g.
// `w=w+1`) is never split and still fails loudly at emission. Runs on the live sim
// graph, before emission. Returns #rewired reads.
// One dissolve pass. Rewrites are DEFERRED to the end, so a reader cannot see
// another reader's rewrite until the next pass -- the wrapper below iterates.
// Why a slice descent gave up. These used to be one bool named `cap_hit`, and
// the warning it fed said "node-creation budget exhausted" for every one of
// them — which is how an investigation came to raise a budget that, measured,
// changed nothing at all on the design that triggered the message. Naming the
// real limit is the difference between "give it more nodes" (budget), "the
// expression nests deeper than the walker goes" (depth) and "this operand shape
// is not one the pass can descend into" (shape).
enum Stop_reason : unsigned {
  kStopBudget = 1u,  // per-reader or global node-creation allowance
  kStopDepth  = 2u,  // deeper than the `max_depth` recursion guard
  kStopShape  = 4u,  // invalid pin or a degenerate slice range
};

// The recursion guard is a STACK budget, so the numbers below have to be read
// together -- keeping them apart is how a guard comes to be sized for a stack
// the pass does not actually run on.
//
// MEASURED on XiangShan `Rob`, 2026-08-17: the walk crashed at depth 1024 on an
// 8 MB stack, i.e. ~8 KB per frame for this 470-line lambda.
inline constexpr size_t kSplitFrameBytes  = 8u * 1024;
// The stack the ESCALATED local wire walk gets. Reserved
// address space only: pages commit as deep as the walk actually goes.
inline constexpr size_t kSplitStackBytes  = 512UL * 1024 * 1024;
// Depth guard for the escalated walk, backstopping a runaway well inside the
// stack above.
inline constexpr int    kSplitWorkerDepth = 16384;
// Depth guard when the walk runs on the CALLER's stack. Keep the inline attempt
// conservative; deep packed structures escalate to the private stack above.
inline constexpr int    kSplitInlineDepth = 64;
static_assert(static_cast<size_t>(kSplitWorkerDepth) * kSplitFrameBytes <= kSplitStackBytes,
              "the escalated depth guard must fit inside the escalated stack");
static_assert(kSplitInlineDepth < kSplitWorkerDepth, "the inline guard is the conservative one");

// `unresolved_out` reports the on-cycle reads this pass could not dissolve;
// `stop_reasons_out` is the OR of the `kStop*` bits saying which limit stopped
// them, for the wrapper's final diagnostic. `max_depth` is the recursion guard,
// a property of the STACK THIS CALL RUNS ON -- see the constants above.
static int split_selfref_pass(hhds::Graph* g, int& unresolved_out, unsigned& stop_reasons_out, int max_depth,
                              const absl::flat_hash_set<hhds::Node_class>* scoped_cycle) {
  namespace gu = livehd::graph_util;

  unresolved_out   = 0;
  stop_reasons_out = 0;

  I(scoped_cycle != nullptr);
  // A REFERENCE, not a copy: this set is read-only here, it can hold every comb
  // node of a large module, and the body's fixpoint re-enters this function
  // once per round.
  const auto&                   in_cycle = *scoped_cycle;
  std::vector<hhds::Node_class> comb_nodes(in_cycle.begin(), in_cycle.end());
  std::sort(comb_nodes.begin(), comb_nodes.end(), [](const auto& a, const auto& b) {
    return a.get_debug_nid() < b.get_debug_nid();
  });
  if (std::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
    std::print("split[dbg]: {} comb node(s), {} on a word-level cycle\n", comb_nodes.size(), in_cycle.size());
    for (auto& n : in_cycle) {
      std::print("split[dbg]:   on-cycle {}\n", gu::debug_name(n));
    }
  }
  if (in_cycle.empty()) {
    return 0;  // an acyclic word-level schedule already exists -> strict no-op
  }

  auto drv_at = [](const hhds::Node_class& n, uint32_t pid) -> hhds::Pin_class {
    for (auto e : n.inp_edges()) {
      if (static_cast<uint32_t>(e.sink.get_port_id()) == pid) {
        return e.driver;
      }
    }
    return {};
  };

  // footprint(p) = an OVER-approximation [lo,hi) of the bit positions where `p`
  // can be nonzero; {-1,-1} means "cannot bound -> do not split". A sound UPPER
  // bound is required for the disjointness proof below.
  auto footprint = [&](auto&& self, const hhds::Pin_class& p, int depth) -> std::pair<int, int> {
    const std::pair<int, int> kBail{-1, -1};
    if (depth > 8 || p.is_invalid()) {
      return kBail;
    }
    if (gu::is_const_pin(p)) {
      auto c = gu::hydrate_const(p);
      if (c.is_negative()) {
        return kBail;
      }
      if (c.has_unknowns()) {
        // a '?'-const (the slang->prp X-seed idiom) still occupies FIXED bit
        // positions: its declared width is a sound upper bound
        return {0, static_cast<int>(c.get_bits())};
      }
      int lo = c.get_first_bit_set();
      int hi = c.get_last_bit_set();
      if (lo < 0 || hi < 0) {
        return {0, 0};  // known zero -> contributes no bits
      }
      return {lo, hi + 1};
    }
    auto m  = p.get_master_node();
    auto op = gu::type_op_of(m);
    if (op == Ntype_op::SHL) {
      auto kd = drv_at(m, 1);
      if (kd.is_invalid() || !gu::is_const_pin(kd)) {
        return kBail;
      }
      auto kc = gu::hydrate_const(kd);
      if (kc.has_unknowns() || kc.is_negative() || !kc.is_just_i64()) {
        return kBail;
      }
      int  k  = static_cast<int>(kc.to_just_i64());
      auto fx = self(self, drv_at(m, 0), depth + 1);
      if (fx.first < 0) {
        return kBail;
      }
      if (fx.second <= fx.first) {
        return {0, 0};
      }
      return {fx.first + k, fx.second + k};
    }
    if (op == Ntype_op::Concat) {
      // A Concat is always NON-NEGATIVE and strictly below 2^sum(lane widths),
      // so the lane table alone gives the bound -- no recursion into the lanes
      // (each is masked into its own window, so a wide or negative lane cannot
      // spill above the total). Asking the generic arm below instead would tie
      // this to the driver's stamped bits/sign and bail on an O0 graph that has
      // not run bitwidth yet.
      const int total = static_cast<int>(gu::concat_total_width(m));
      return total > 0 ? std::pair<int, int>{0, total} : kBail;
    }
    if (op == Ntype_op::And) {
      // And with a NON-NEGATIVE constant bounds the result to the constant's
      // set-bit range regardless of the other operands' signs (bitwise and
      // clears everything above the mask's top set bit) -- covers the `x & 1`
      // valid-bit clamps the slang->prp regeneration emits.
      std::pair<int, int> best = kBail;
      for (auto e : m.inp_edges()) {
        if (!gu::is_const_pin(e.driver)) {
          continue;
        }
        auto c = gu::hydrate_const(e.driver);
        if (c.has_unknowns() || c.is_negative()) {
          continue;
        }
        int clo = c.get_first_bit_set();
        int chi = c.get_last_bit_set();
        if (clo < 0 || chi < 0) {
          return {0, 0};  // and with 0 -> known zero
        }
        if (best.first < 0 || (chi + 1 - clo) < (best.second - best.first)) {
          best = {clo, chi + 1};
        }
      }
      if (best.first >= 0) {
        return best;
      }
      // no constant operand -> fall through to the generic unsigned bound
    }
    if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
      // the result is always one of the DATA operands (port 0 is the
      // selector; an out-of-range selector yields 0) -> union of their bounds
      std::pair<int, int> u{0, 0};
      bool                any = false;
      for (auto e : m.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) == 0) {
          continue;  // selector
        }
        auto f = self(self, e.driver, depth + 1);
        if (f.first < 0) {
          return kBail;
        }
        if (f.second > f.first) {
          u   = any ? std::pair<int, int>{std::min(u.first, f.first), std::max(u.second, f.second)} : f;
          any = true;
        }
      }
      return any ? u : std::pair<int, int>{0, 0};
    }
    if (op == Ntype_op::Get_mask) {
      auto md = drv_at(m, 2);
      if (!md.is_invalid()) {
        if (!gu::is_const_pin(md)) {
          return kBail;
        }
        auto mc = gu::hydrate_const(md);
        if (mc.has_unknowns()) {
          return kBail;
        }
        // mask == -1 is the "to-unsigned" idiom: the RESULT is the non-negative
        // low sig bits of the output pin (fall through to the unsigned bound).
        if (!(mc.is_just_i64() && mc.to_just_i64() == -1)) {
          auto [a, b] = mc.get_mask_range();
          if (a < 0 || b < 0 || b > (1 << 28)) {
            return kBail;  // noncontiguous / open / negative mask
          }
          int w = b - a;
          if (w <= 1) {
            // The EMITTED single-bit Get_mask clamps to a 0/1 magnitude
            // (node_expr appends .zext_to<1>() for a popcount-1 mask), so at
            // sim semantics the value is soundly bounded {0,1}. This was the
            // footprint bail that vetoed the whole split for every 1-bit
            // packed field (valid bits: sim_packed_selfref_1bit family).
            return {0, 1};
          }
          return {0, w};  // extracted bits are packed down to [0, w)
        }
      }
      // unary width-adjust (zext) OR to-unsigned: result is the non-negative low
      // sig bits of THIS node's (unsigned) output pin.
      int b = gu::bits_of(p);
      return b > 0 ? std::pair<int, int>{0, b} : kBail;
    }
    // generic value: an over-approximation is sound only when UNSIGNED; its
    // literal width bounds every possibly-set bit.
    int b = gu::bits_of(p);
    if (gu::is_unsign(p)) {
      return b > 0 ? std::pair<int, int>{0, b} : kBail;
    }
    return kBail;
  };

  // ---- generalized slice resolution ----------------------------------------
  // resolve(v, lo, hi) returns a pin VALUE-EQUAL (at emitted-sim semantics) to
  // `Get_mask(v, mask[lo, hi))` -- the packed-down bit-field read -- built only
  // from OFF-CYCLE pins and NEW nodes, or an invalid pin when the slice cannot
  // be proven independent of the word-level cycle. Every rule is locally
  // value-preserving, so ANY subset of rewires keeps the graph correct (a
  // genuine bit-level loop resolves to invalid and still fails loudly):
  //  * const / off-cycle value        -> a fresh Get_mask slice node (terminal)
  //  * Or with a unique covering operand disjoint from all others -> descend
  //  * And/Or/Xor (bit-parallel)      -> distribute the slice over every
  //    operand and rebuild the op on the resolved slices
  //  * SHL by a constant k            -> re-based descent (zeros below k)
  //  * Get_mask const [a,b) extraction -> descend at positions [a+lo, a+hi)
  //  * to-unsigned / unary-zext Get_mask -> position-preserving descent
  //  * Sum with no subtrahend and pairwise-DISJOINT operand footprints == Or
  //  * Or operands with footprints outside the requested slice -> exact zero
  //  * EQ control bit -> rebuild only from complete bounded operands
  //  * Concat -> re-based descent into the lane(s) the slice lands in (the
  //    disjointness the Or spelling must PROVE is a cell invariant here)
  auto mask_const
      = [&](int lo, int hi) -> hhds::Pin_class { return livehd::graph_util::create_const(*g, *Dlop::get_mask_value(hi - 1, lo)); };
  // Node-creation budget, split into a PER-READER cap (reset at the reader-loop
  // head below) and a GLOBAL ceiling proportional to the design. The old code
  // had ONE global counter that was never reset: a big def's early readers burnt
  // the whole budget, after which every later reader refused instantly at depth
  // 0 and its slice was left on the cycle -- a silent, def-size-dependent failure
  // (semdiff then read the survivors as a 62%-different "diff" on a bit-identical
  // library). Per-reader budgeting makes each read pay only for its own subtree;
  // the global ceiling is the real anti-blowup net and is loose because distinct
  // sub-slices are memoized and shared across readers.
  //
  // MEASURED 2026-08-17, and the answer is that raising this does NOT help the
  // design that motivated asking. XiangShan `Rob` leaves 914 on-cycle reads
  // undissolved with the node budget reported as exhausted; growing the
  // allowance 8x ran three extra fixpoint rounds, rewired exactly the same
  // 315,791 reads, and cost +5.5% wall and 10.4 GB peak RSS for nothing. The
  // hint that says "raise the split budget" was believed because `cap_hit`
  // conflated every refusal into one bit -- see `Stop_reason` below, which now
  // says which of the three actually fired.
  constexpr int per_reader_cap = 16384;
  const int     global_cap     = 16384 + 8 * static_cast<int>(comb_nodes.size());
  int           created        = 0;  // per-reader (reset at each reader below)
  int           total_created  = 0;  // global (never reset)
  // LIVEHD_SIM_SPLIT_DEBUG=1 traces every reader attempt + the deepest resolve
  // refusal (op, slice) -- the fast way to see WHY a pack did not split.
  const bool    split_dbg      = std::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr;
  absl::flat_hash_map<std::tuple<hhds::Class_index, int, int>, hhds::Pin_class> memo;
  // A (pin,slice) already on the RESOLUTION STACK means this slice depends on
  // itself -- a GENUINE bit-level cycle: fail it permanently. That makes the
  // depth cap a pure safety net (deep-but-acyclic sbox/crypto chains resolve),
  // and a failure caused ONLY by the caps must NOT be memoized -- an earlier
  // capped frame would otherwise poison every later resolution through that
  // slice (the BlockCipherModule 21-bit sbox hit exactly this).
  absl::flat_hash_set<std::tuple<hhds::Class_index, int, int>>                  on_stack;
  // The recursion guard arrives as `max_depth`. `on_stack` above already catches
  // a genuine bit-level self-dependency EXACTLY, so this is purely a
  // stack-exhaustion net and never a semantic limit — which means the right
  // value is "as deep as the stack THIS CALL runs on safely allows", and that is
  // the caller's to decide (kSplitInlineDepth on its own stack,
  // kSplitWorkerDepth on the escalated one), not a constant here.
  //
  // MEASURED on XiangShan `Rob`, 2026-08-17: at 64 this was the ONLY limit that
  // fired (the node budget took the blame for years because both set the same
  // flag; raising the budget 8x dissolved zero reads). 256 changed nothing —
  // the failing descents go far deeper — and 1024 died with a stack overflow on
  // an 8 MB stack. Making `resolve` ITERATIVE would remove the guard entirely
  // and is still the eventual answer.
  bool                                                                          cap_hit      = false;
  // WHICH limit stopped a descent, not merely THAT one did. These were a single
  // bool, and the resulting diagnostic blamed the node budget for every one of
  // the six refusal conditions -- which sent an investigation into raising a
  // budget that turned out to change nothing (see the comment on per_reader_cap
  // above). Depth and an unhandled operand shape are entirely different problems
  // with different fixes.
  unsigned                                                                      stop_reasons = 0;
  auto resolve = [&](auto&& self, const hhds::Pin_class& v, int lo, int hi, int depth) -> hhds::Pin_class {
    const bool over_budget = created > per_reader_cap || total_created > global_cap;
    const bool too_deep    = depth > max_depth;
    const bool bad_shape   = v.is_invalid() || lo < 0 || hi <= lo;
    if (too_deep || bad_shape || over_budget) {
      if (split_dbg) {
        std::print("split[dbg]: refuse depth={} lo={} hi={} created={} total={} invalid={}\n",
                   depth,
                   lo,
                   hi,
                   created,
                   total_created,
                   v.is_invalid());
      }
      stop_reasons |= (over_budget ? kStopBudget : 0u) | (too_deep ? kStopDepth : 0u) | (bad_shape ? kStopShape : 0u);
      cap_hit       = true;
      return {};
    }
    const int w          = hi - lo;
    auto      slice_node = [&](const hhds::Pin_class& val) -> hhds::Pin_class {
      auto n = gu::create_typed_node(*g, Ntype_op::Get_mask);
      ++created;
      ++total_created;
      val.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
      mask_const(lo, hi).connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(2)));
      auto dp = n.create_driver_pin(0);
      gu::set_ubits(dp, w);
      return dp;
    };
    if (gu::is_const_pin(v)) {
      return slice_node(v);  // node_expr folds const operands at emission
    }
    std::tuple<hhds::Class_index, int, int> key{v.get_class_index(), lo, hi};
    if (auto it = memo.find(key); it != memo.end()) {
      return it->second;
    }
    if (!on_stack.insert(key).second) {
      // the slice's own value is on the current resolution path: a genuine
      // bit-level self-dependency -- permanently unresolvable
      if (split_dbg) {
        std::print("split[dbg]: on-stack self-dependency [{},{}) depth={}\n", lo, hi, depth);
      }
      memo.emplace(key, hhds::Pin_class{});
      return {};
    }
    struct Stack_pop {
      absl::flat_hash_set<std::tuple<hhds::Class_index, int, int>>* s;
      std::tuple<hhds::Class_index, int, int>                       k;
      ~Stack_pop() { s->erase(k); }
    } stack_pop{&on_stack, key};
    auto m = v.get_master_node();
    if (!in_cycle.contains(m)) {
      auto r = slice_node(v);
      memo.emplace(key, r);
      return r;
    }
    const bool      cap_before = cap_hit;
    auto            op         = gu::type_op_of(m);
    hhds::Pin_class res{};
    if (op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor || op == Ntype_op::Sum) {
      std::vector<hhds::Pin_class> operands;
      bool                         has_sub = false;
      for (auto e : m.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
          has_sub = true;  // Sum subtrahend (or unexpected port) -> not a pack
          break;
        }
        operands.push_back(e.driver);
      }
      bool usable = !has_sub && !operands.empty();
      // A Sum is only a pack when NO carry can occur: every operand bounded
      // and pairwise disjoint (then Sum == Or, e.g. `low + (a << 2)`). An Or
      // gets the same analysis for the cheap unique-cover descent; And/Xor
      // always distribute (bit-parallel).
      if (usable && (op == Ntype_op::Or || op == Ntype_op::Sum)) {
        std::vector<std::pair<int, int>> fps;
        bool                             bounded = true;
        for (auto& d : operands) {
          auto f = footprint(footprint, d, 0);
          if (f.first < 0) {
            bounded = false;
            break;
          }
          fps.push_back(f);
        }
        bool disjoint = bounded;
        for (size_t i = 0; bounded && i + 1 < fps.size() && disjoint; ++i) {
          for (size_t j = i + 1; j < fps.size() && disjoint; ++j) {
            if (fps[i].second > fps[i].first && fps[j].second > fps[j].first
                && !(fps[i].second <= fps[j].first || fps[j].second <= fps[i].first)) {
              disjoint = false;
            }
          }
        }
        if (op == Ntype_op::Sum && !disjoint) {
          usable = false;  // a real adder -> cannot slice
        }
        if (usable && bounded && disjoint) {
          hhds::Pin_class cover;
          int             covers = 0;
          bool            clean  = true;
          for (size_t i = 0; i < operands.size(); ++i) {
            bool covers_slice = lo >= fps[i].first && hi <= fps[i].second;
            bool overlaps     = !(hi <= fps[i].first || lo >= fps[i].second);
            if (covers_slice) {
              cover = operands[i];
              ++covers;
            } else if (overlaps) {
              clean = false;  // straddles -> distribute below (Or/disjoint-Sum only)
            }
          }
          if (covers == 1 && clean) {
            res = self(self, cover, lo, hi, depth + 1);
          } else if (covers == 0 && clean) {
            res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));  // slice outside every operand
          }
        }
      }
      if (usable && res.is_invalid()) {
        // distribute the slice over every operand; a Sum only reaches here
        // when proven disjoint (rebuilt as Or).
        std::vector<hhds::Pin_class> parts;
        bool                         ok = true;
        for (auto& d : operands) {
          if (op == Ntype_op::Or) {
            // Global pack disjointness is unnecessary for a local OR slice:
            // an operand whose proven nonzero footprint does not overlap
            // [lo,hi) contributes exactly zero here. This is crucial when an
            // unrelated output field has overlapping OR contributors but the
            // requested input field has one clean driver (XSCore LruStateGen's
            // packed io word).
            auto f = footprint(footprint, d, 0);
            if (f.first >= 0 && (hi <= f.first || lo >= f.second)) {
              continue;
            }
          }
          auto r = self(self, d, lo, hi, depth + 1);
          if (r.is_invalid()) {
            ok = false;
            break;
          }
          parts.push_back(r);
        }
        if (ok && parts.empty()) {
          res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
        } else if (ok) {
          auto n = gu::create_typed_node(*g, op == Ntype_op::Sum ? Ntype_op::Or : op);
          ++created;
          for (auto& pp : parts) {
            pp.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
          }
          auto dp = n.create_driver_pin(0);
          gu::set_ubits(dp, w);
          res = dp;
        }
      }
    } else if (op == Ntype_op::SHL) {
      auto kd = drv_at(m, 1);
      if (!kd.is_invalid() && gu::is_const_pin(kd)) {
        auto kc = gu::hydrate_const(kd);
        if (!kc.has_unknowns() && !kc.is_negative() && kc.is_just_i64()) {
          int k = static_cast<int>(kc.to_just_i64());
          if (hi <= k) {
            res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
          } else if (lo >= k) {
            res = self(self, drv_at(m, 0), lo - k, hi - k, depth + 1);
          } else {
            // straddle: bits [lo,k) are zero; the rest is x[0, hi-k) shifted up
            auto low = self(self, drv_at(m, 0), 0, hi - k, depth + 1);
            if (!low.is_invalid()) {
              auto n = gu::create_typed_node(*g, Ntype_op::SHL);
              ++created;
              low.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
              livehd::graph_util::create_const(*g, *Dlop::create_integer(k - lo))
                  .connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(1)));
              auto dp = n.create_driver_pin(0);
              gu::set_ubits(dp, w);
              res = dp;
            }
          }
        }
      }
    } else if (op == Ntype_op::SRA) {
      auto kd = drv_at(m, 1);
      if (!kd.is_invalid() && gu::is_const_pin(kd)) {
        auto kc = gu::hydrate_const(kd);
        if (!kc.has_unknowns() && !kc.is_negative() && kc.is_just_i64()) {
          // bits [lo,hi) of an arithmetic right shift are bits [lo+k,hi+k) of
          // the operand -- exact for ANY sign (Get_mask reads the conceptual
          // two's-complement value, whose sign replication matches sra's fill).
          int k = static_cast<int>(kc.to_just_i64());
          res   = self(self, drv_at(m, 0), lo + k, hi + k, depth + 1);
        } else if (split_dbg) {
          std::print("split[dbg]:   sra amount const but unknowns/neg/wide\n");
        }
      } else if (split_dbg) {
        std::print("split[dbg]:   sra amount NON-CONST (dynamic shift)\n");
      }
    } else if (op == Ntype_op::Mux) {
      // Distribute the slice through the arms: Get_mask(mux(s, xs...), m) ==
      // mux(s, Get_mask(x, m)...) -- the mux picks one arm's bit pattern, so
      // bits [lo,hi) of the result are bits [lo,hi) of the picked arm (the
      // BlockCipherModule/round-chaining shape: a packed word driven by a Mux
      // whose arms pack a field computed from a slice of the same word). The
      // selector is NOT sliced (it picks the same arm either way); an on-cycle
      // selector resolves as its own full-width slice (sound for an unsigned
      // pin: Get_mask[0, sig) of an unsigned value is the value).
      auto            sel = drv_at(m, 0);
      hhds::Pin_class rsel;
      if (!sel.is_invalid()) {
        if (gu::is_const_pin(sel) || !in_cycle.contains(sel.get_master_node())) {
          rsel = sel;
        } else if (gu::is_unsign(sel)) {
          int sb = gu::bits_of(sel);
          rsel   = self(self, sel, 0, std::max(1, sb), depth + 1);
        }
      }
      if (!rsel.is_invalid()) {
        std::vector<std::pair<hhds::Port_id, hhds::Pin_class>> arms;
        bool                                                   ok = true;
        for (auto e : m.inp_edges()) {
          if (static_cast<uint32_t>(e.sink.get_port_id()) == 0) {
            continue;  // selector
          }
          auto r = self(self, e.driver, lo, hi, depth + 1);
          if (r.is_invalid()) {
            ok = false;
            break;
          }
          arms.emplace_back(e.sink.get_port_id(), r);
        }
        if (ok && !arms.empty()) {
          auto n = gu::create_typed_node(*g, Ntype_op::Mux);
          ++created;
          rsel.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
          for (auto& [pid, ap] : arms) {
            ap.connect_sink(n.create_sink_pin(pid));
          }
          auto dp = n.create_driver_pin(0);
          gu::set_ubits(dp, w);
          res = dp;
        }
      }
    } else if (op == Ntype_op::EQ) {
      // Equality is a one-bit control result, but unlike bit-parallel ops its
      // bit depends on every operand bit. Rebuild it only when each on-cycle
      // operand can be resolved as its COMPLETE unsigned value. This covers
      // packed-slice predicates used as Mux selectors (the XSCore Btb/PreDecode
      // tail) without pretending that a partial comparator cone is local.
      if (lo >= 1) {
        res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
      } else {
        std::vector<hhds::Pin_class> operands;
        bool                         ok = true;
        for (auto e : m.inp_edges()) {
          if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
            ok = false;
            break;
          }
          auto d = e.driver;
          if (!gu::is_const_pin(d) && in_cycle.contains(d.get_master_node())) {
            const int db      = gu::bits_of(d);
            int       whole_w = 0;
            if (gu::is_unsign(d) && db > 0) {
              whole_w = std::max(1, db);
            } else {
              // A signed-typed masked value is still exactly reconstructible
              // when footprint proves it nonnegative and zero above hi.
              auto f = footprint(footprint, d, 0);
              if (f.first >= 0) {
                whole_w = std::max(1, f.second);
              }
            }
            if (whole_w == 0) {
              if (split_dbg) {
                std::print("split[dbg]:   EQ operand {} bits={} unsigned={} has no complete bound\n",
                           op_name(gu::type_op_of(d.get_master_node())),
                           db,
                           gu::is_unsign(d));
              }
              ok = false;
              break;
            }
            d = self(self, d, 0, whole_w, depth + 1);
            if (split_dbg) {
              std::print("split[dbg]:   EQ operand complete width={} -> {}\n", whole_w, d.is_invalid() ? "FAIL" : "ok");
            }
          }
          if (d.is_invalid()) {
            ok = false;
            break;
          }
          operands.push_back(d);
        }
        if (ok && !operands.empty()) {
          auto n = gu::create_typed_node(*g, Ntype_op::EQ);
          ++created;
          for (auto& d : operands) {
            d.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
          }
          auto dp = n.create_driver_pin(0);
          gu::set_ubits(dp, 1);  // unsigned boolean
          res = dp;
        }
      }
    } else if (op == Ntype_op::Not) {
      // bits of ~x at [lo,hi) = complement of x's bits there, re-masked to w
      auto r = self(self, drv_at(m, 0), lo, hi, depth + 1);
      if (!r.is_invalid()) {
        auto n1 = gu::create_typed_node(*g, Ntype_op::Not);
        ++created;
        r.connect_sink(n1.create_sink_pin(static_cast<hhds::Port_id>(0)));
        auto np = n1.create_driver_pin(0);
        gu::set_sbits(np, w + 1);
        auto n2 = gu::create_typed_node(*g, Ntype_op::And);
        ++created;
        np.connect_sink(n2.create_sink_pin(static_cast<hhds::Port_id>(0)));
        livehd::graph_util::create_const(*g, *Dlop::get_mask_value(w - 1, 0))
            .connect_sink(n2.create_sink_pin(static_cast<hhds::Port_id>(0)));
        auto dp = n2.create_driver_pin(0);
        gu::set_ubits(dp, w);
        res = dp;
      }
    } else if (op == Ntype_op::Get_mask) {
      // A Get_mask output is a NON-NEGATIVE packed value of its extraction
      // width, so a slice reaching ABOVE that width reads zeros: cap the
      // recursion instead of failing (the sbox chains read bit k of narrower
      // extractions all the time).
      auto md = drv_at(m, 2);
      if (md.is_invalid()) {
        // unary zext: positions [0, sig) preserved; above sig -> zeros
        int b   = gu::bits_of(v);
        int sig = b;
        if (sig > 0) {
          if (lo >= sig) {
            res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
          } else {
            res = self(self, drv_at(m, 0), lo, std::min(hi, sig), depth + 1);
          }
        }
      } else if (gu::is_const_pin(md)) {
        auto mc = gu::hydrate_const(md);
        if (!mc.has_unknowns()) {
          if (mc.is_just_i64() && mc.to_just_i64() == -1) {
            // to-unsigned keeps positions; above the sig width -> zeros
            int b   = gu::bits_of(v);
            int sig = b;
            if (sig > 0 && lo >= sig) {
              res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
            } else if (sig > 0) {
              res = self(self, drv_at(m, 0), lo, std::min(hi, sig), depth + 1);
            } else {
              res = self(self, drv_at(m, 0), lo, hi, depth + 1);
            }
          } else {
            auto [a, b] = mc.get_mask_range();
            if (a >= 0 && b > a) {
              const int width = b - a;  // packed extraction width
              if (lo >= width) {
                res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
              } else {
                res = self(self, drv_at(m, 0), a + lo, a + std::min(hi, width), depth + 1);  // re-base + cap
              }
            }
          }
        }
      }
    } else if (op == Ntype_op::Set_mask) {
      // A Set_mask rewrites ONLY the bits inside its own constant lane, so a
      // slice resolves to exactly ONE source: the base `a` when disjoint from
      // the lane, the written `value` when contained in it. This is the same
      // pair of rules cprop applies while walking a Set_mask `a`-chain
      // (pass/cprop/cprop.cpp) — ported here so an O0 graph, which never runs
      // cprop, can dissolve the packed field-write chain too.
      //
      // The chain is what manufactures the false loop: `c.f0 = …; c.f1 = …`
      // on one packed net lowers to Set_mask(Set_mask(base,…),…), and a read
      // of f0 binds to the LAST version — so if f1's value depends on that
      // read the word-level graph closes a cycle the bit-level DAG does not
      // have. Walking the slice down to the version that wrote its own lane is
      // what the per-leaf (flattened) form would have given for free.
      auto md = drv_at(m, 2);
      if (!md.is_invalid() && gu::is_const_pin(md)) {
        auto mc = gu::hydrate_const(md);
        if (!mc.has_unknowns() && mc.is_positive()) {
          auto [a, b] = mc.get_mask_range();  // {-1,-1} = noncontiguous
          if (a >= 0 && b > a) {
            if (hi <= a || lo >= b) {
              res = self(self, drv_at(m, 0), lo, hi, depth + 1);  // disjoint lane: `a` is untouched here
            } else if (lo >= a && hi <= b) {
              // Contained in the lane: the slice reads back exactly what
              // `value` put there, LSB-aligned to the lane's low bit. Guard on
              // a bounded NON-NEGATIVE footprint — that is what makes the two
              // forms agree ABOVE `value`'s significant width (Get_mask reads
              // 0 there, and a zero-extended `value` is what the lane holds).
              auto vd = drv_at(m, 4);
              if (!vd.is_invalid() && footprint(footprint, vd, 0).first >= 0) {
                res = self(self, vd, lo - a, hi - a, depth + 1);
              }
            }
            // A slice STRADDLING the lane boundary would need a concat of two
            // sources; leave it unresolved rather than grow the graph here.
          }
        }
      }
    } else if (op == Ntype_op::Concat) {
      // The cheapest pack to dissolve, because the cell CARRIES its lane table:
      // bits [lo,hi) come only from the lanes whose windows they land in, at a
      // known re-based position. None of the footprint/disjointness proof the
      // Or-of-SHL spelling needs applies -- lane i owning exactly
      // [offset_i, offset_i + width_i) is a cell invariant.
      //
      // Unlike the Set_mask arm above there is NO "value must be non-negative"
      // guard: a lane holds `value mod 2^w`, i.e. value's low w bits as spelled
      // in two's complement, and Get_mask reads those same conceptual
      // two's-complement bits, so the two forms already agree for a negative or
      // over-wide lane. A straddling read is handled (not refused) so that a
      // frontend emitting Concat is never WEAKER than the hand-spelled Or/SHL
      // pack, whose straddles the SHL arm already splits.
      auto                         lanes = gu::concat_lanes(m);
      std::vector<hhds::Pin_class> parts;                // resolved pieces, each already shifted into [lo,hi)
      bool                         ok = !lanes.empty();  // empty == malformed cell: fail closed
      for (const auto& l : lanes) {
        const int l_lo = static_cast<int>(l.offset);
        const int l_hi = l_lo + static_cast<int>(l.width);
        if (hi <= l_lo || lo >= l_hi) {
          continue;  // this lane's window does not intersect the slice
        }
        const int a = std::max(lo, l_lo);
        const int b = std::min(hi, l_hi);
        auto      r = self(self, l.value, a - l_lo, b - l_lo, depth + 1);
        if (r.is_invalid()) {
          ok = false;
          break;
        }
        if (a > lo) {
          auto n = gu::create_typed_node(*g, Ntype_op::SHL);
          ++created;
          r.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
          livehd::graph_util::create_const(*g, *Dlop::create_integer(a - lo))
              .connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(1)));
          auto dp = n.create_driver_pin(0);
          gu::set_ubits(dp, w);
          r = dp;
        }
        parts.push_back(r);
      }
      if (ok && parts.empty()) {
        // entirely above the top lane: the result is non-negative and bounded
        // by the lane sum, so those bits read as exact zeros
        res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
      } else if (ok && parts.size() == 1) {
        res = parts.front();  // one lane covers the read: the descent IS the answer
      } else if (ok) {
        auto n = gu::create_typed_node(*g, Ntype_op::Or);
        ++created;
        for (auto& pp : parts) {
          pp.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
        }
        auto dp = n.create_driver_pin(0);
        gu::set_ubits(dp, w);
        res = dp;
      }
    }
    if (split_dbg && res.is_invalid()) {
      std::print("split[dbg]: unresolved {} [{},{}) depth={}\n", op_name(op), lo, hi, depth);
    }
    if (!res.is_invalid() || cap_hit == cap_before) {
      // memoize successes always; memoize failures only when NOT tainted by a
      // depth/creation cap in this subtree (a shallower entry may still succeed)
      memo.emplace(key, res);
    }
    return res;
  };

  // ---- collect on-cycle bit-field readers -----------------------------------
  // (a) constant Get_mask slice-reads; (b) `(w >> k) & m` spelled as
  // And(SRA(w, const k), const 2^j-1) -- the other reader form the slang->prp
  // regeneration emits. Mutation is deferred so the analysis sees a stable
  // graph (created helper nodes are new and never on the cycle).
  std::vector<std::tuple<hhds::Node_class, hhds::Pin_class, hhds::Pin_class>> gm_rewires;   // (reader, resolved, new mask)
  std::vector<std::tuple<hhds::Node_class, hhds::Pin_class, hhds::Pin_class>> and_rewires;  // (And, old SRA driver, resolved)
  int      unresolved_on_cycle = 0;  // on-cycle bit-field reads we could not dissolve (diagnostic)
  unsigned any_stop_reasons    = 0;  // ... and WHICH limit stopped them (kStop* bits)
  for (auto& R : comb_nodes) {  // == in_cycle, sorted for a deterministic visit order
    created      = 0;      // per-reader budget: this read pays only for its own subtree
    cap_hit      = false;  // per-reader cap-taint detection (the memoization guard in resolve)
    // Per-reader too, and for the same reason `cap_hit` is: a descent that was
    // refused once and then succeeded from a shallower frame still set its bit,
    // so carrying the bits across readers would attribute an EARLIER read's
    // recovered refusal to whichever later read actually failed -- reintroducing
    // by accumulation exactly the conflation this enum exists to remove.
    stop_reasons = 0;
    auto rop     = gu::type_op_of(R);
    if (rop == Ntype_op::Get_mask) {
      auto md = drv_at(R, 2);
      if (md.is_invalid() || !gu::is_const_pin(md)) {
        continue;  // needs a constant slice mask
      }
      auto mc = gu::hydrate_const(md);
      if (mc.has_unknowns() || (mc.is_just_i64() && mc.to_just_i64() == -1)) {
        continue;  // a full read, not a bit-field slice
      }
      auto [rlo, rhi] = mc.get_mask_range();
      if (rlo < 0 || rhi <= rlo || rhi > (1 << 28)) {
        continue;  // noncontiguous / open slice
      }
      auto vd = drv_at(R, 0);
      if (vd.is_invalid() || gu::is_const_pin(vd)) {
        continue;
      }
      auto res = resolve(resolve, vd, rlo, rhi, 0);
      if (res.is_invalid()) {
        ++unresolved_on_cycle;
        any_stop_reasons |= stop_reasons;
        continue;
      }
      gm_rewires.emplace_back(R, res, mask_const(0, rhi - rlo));
    } else if (rop == Ntype_op::And) {
      // exactly two operands: a const 2^j-1 mask and an SRA(word, const k)
      hhds::Pin_class cpin, other;
      int             nins = 0;
      bool            bad  = false;
      for (auto e : R.inp_edges()) {
        ++nins;
        if (gu::is_const_pin(e.driver)) {
          if (cpin.is_invalid()) {
            cpin = e.driver;
          } else {
            bad = true;
          }
        } else if (other.is_invalid()) {
          other = e.driver;
        } else {
          bad = true;
        }
      }
      if (bad || nins != 2 || cpin.is_invalid() || other.is_invalid()) {
        continue;
      }
      auto cc = gu::hydrate_const(cpin);
      if (cc.has_unknowns() || cc.is_negative()) {
        continue;
      }
      int cl = cc.get_first_bit_set();
      int ch = cc.get_last_bit_set();
      if (cl != 0 || ch < 0 || cc.popcount() != ch - cl + 1) {
        continue;  // need a contiguous-from-0 mask
      }
      int  j  = ch + 1;
      auto sm = other.get_master_node();
      int  k  = 0;
      auto wd = other;  // direct `w & m` read of the packed word (no shift)
      if (gu::type_op_of(sm) == Ntype_op::SRA) {
        auto kd = drv_at(sm, 1);
        if (kd.is_invalid() || !gu::is_const_pin(kd)) {
          continue;
        }
        auto kc = gu::hydrate_const(kd);
        if (kc.has_unknowns() || kc.is_negative() || !kc.is_just_i64()) {
          continue;
        }
        k  = static_cast<int>(kc.to_just_i64());
        wd = drv_at(sm, 0);
        if (wd.is_invalid()) {
          continue;
        }
      }
      auto res = resolve(resolve, wd, k, k + j, 0);
      if (split_dbg) {
        std::print("split[dbg]: And-reader j={} k={} sra={} -> {}\n",
                   j,
                   k,
                   gu::type_op_of(sm) == Ntype_op::SRA,
                   res.is_invalid() ? "FAIL" : "ok");
      }
      if (res.is_invalid()) {
        ++unresolved_on_cycle;
        any_stop_reasons |= stop_reasons;
        continue;
      }
      and_rewires.emplace_back(R, other, res);
    }
  }

  for (auto& [R, res, nm] : gm_rewires) {
    auto edges = R.inp_edges();  // snapshot before mutating
    for (auto e : edges) {
      auto pid = static_cast<uint32_t>(e.sink.get_port_id());
      if (pid == 0 || pid == 2) {
        e.del_edge();
      }
    }
    // resolve() returned the packed-down [0,w) slice, so the reader becomes a
    // low-w identity read: same value, same single-bit clamp semantics.
    res.connect_sink(R.create_sink_pin(static_cast<hhds::Port_id>(0)));
    nm.connect_sink(R.create_sink_pin(static_cast<hhds::Port_id>(2)));
  }
  for (auto& [A, oldd, res] : and_rewires) {
    auto edges = A.inp_edges();  // snapshot before mutating
    for (auto e : edges) {
      if (e.driver == oldd) {
        e.del_edge();
      }
    }
    // And(res, 2^j-1) == res: the const mask stays, other SRA consumers keep
    // their (possibly still cyclic) reads and fail loudly if unresolvable.
    res.connect_sink(A.create_sink_pin(static_cast<hhds::Port_id>(0)));
  }
  const int nrew   = static_cast<int>(gm_rewires.size() + and_rewires.size());
  // Report the survivors to the caller (the iterating wrapper decides whether to
  // warn -- an intermediate round leaves nested reads unresolved only because the
  // next round's rewrites are not applied yet, so warning per pass would spam).
  unresolved_out   = unresolved_on_cycle;
  stop_reasons_out = any_stop_reasons;
  if (nrew > 0) {
    // The edge rewires above only INCREMENTALLY patch forward_class's in-edge
    // counts; its cached Pass-2 deferral order was built while the graph still had
    // the (now-removed) cycle and would otherwise replay that stale, invalid
    // schedule. Re-stamping a touched node's UNCHANGED type forces a full rebuild
    // of the forward-traversal caches on the now-acyclic graph (same effect the
    // node/pin creation in flatten_false_loop_subs has for free), so the scheduler
    // re-derives a correct topological order.
    auto& R = gm_rewires.empty() ? std::get<0>(and_rewires.front()) : std::get<0>(gm_rewires.front());
    R.set_type(R.get_type());
  }
  return nrew;
}

// What one fixpoint run of the splitter ended up with. RETURNED rather than
// reported on the spot: the wrapper may run the walk twice (a shallow attempt on
// the caller's stack, then a deep one on a big-stack worker), and one call must
// not turn into two contradictory warnings.
struct Split_result {
  int      total        = 0;  // reads rewired
  int      unresolved   = 0;  // on-cycle reads the last pass could not dissolve
  unsigned stop_reasons = 0;  // OR of the kStop* bits
  int      rounds       = 0;  // fixpoint rounds ACTUALLY run
  bool     fixpoint     = false;
};

// Is this `Sub` instance a PURE-COMB call -- is its whole callee CLOSURE
// state-free? Such an instance is NOT a scheduling boundary: it is ordinary
// combinational logic that merely happens to be spelled as a hierarchy edge,
// and a cycle running through it is a cycle NOW, not one that appears later
// when somebody dissolves the instance.
//
// Cutting a pure-comb Sub is what used to make lnast.tolg's per-wire splitter
// report NO self-dependency for a packed wire whose feedback threads through an
// instance (tests/equiv/selfref_thru_comb_sub): the backward cone stopped at the
// callee and never reached the wire's own buffer. The cycle then surfaced only
// once a writer flattened the instance, which is why the repair used to be
// re-derived over the whole graph. Seeing through the instance here resolves it
// at bind time instead, where the wire's driver is in hand.
//
// Both cycle questions in this file share this predicate on purpose -- the
// caller asking "did the split finish?" and the one asking "is a genuine self
// dependency left?" must not disagree about what a cycle is (see the header).
//
// A LOOP sub is never transparent: its body is a rolled occurrence standing for
// `count` replicas, not ordinary comb logic in the caller's schedule. A
// body-less black box (liberty cell, external IP) is likewise opaque.
using Sub_comb_cache = absl::flat_hash_map<hhds::Gid, bool>;

static bool sub_closure_is_comb(const std::shared_ptr<hhds::Graph>& cg, Sub_comb_cache& cache);

static bool sub_is_pure_comb(const hhds::Node_class& n, Sub_comb_cache& cache) {
  if (n.is_invalid() || n.is_loop_subnode()) {
    return false;
  }
  const auto gid = n.get_subnode_gid();
  if (auto it = cache.find(gid); it != cache.end()) {
    return it->second;
  }
  // Seed FALSE before recursing: a hierarchy that reaches itself is not a
  // transparent comb call, and the seed doubles as the recursion guard.
  cache.emplace(gid, false);
  const bool ok = sub_closure_is_comb(n.get_subnode_graph(), cache);
  cache[gid]    = ok;
  return ok;
}

static bool sub_closure_is_comb(const std::shared_ptr<hhds::Graph>& cg, Sub_comb_cache& cache) {
  if (!cg) {
    return false;  // body-less black box: nothing to see through
  }
  for (auto n : cg->body().nodes()) {
    const auto op = type_op_of(n);
    if (op == Ntype_op::Memory || op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop) {
      return false;
    }
    if (op == Ntype_op::Sub && !sub_is_pure_comb(n, cache)) {
      return false;
    }
  }
  return true;
}

// Which INPUT port ids of this `Sub` instance does its OUTPUT port `out_pid`
// depend on COMBINATIONALLY? Answered by walking backward inside the callee
// from that output's IO pin, cutting at state and recursing pin-accurately
// through nested instances.
//
// This is the PRECISE version of "see through a comb Sub". Modelling the
// instance as a crossbar (every output depends on every input) is safe when the
// answer only decides whether to ATTEMPT a split -- an over-approximation just
// widens the cone. It is NOT safe for comb_pin_depends_on below, which backs
// lnast.tolg's `combinational loop through wire` ERROR: the
// tests/equiv/sim_sub_nested_comb_feedback shape (`x = leaf(a,b)` independent of
// input `c`, parent feeds `x` back into `c`) has no bit-level cycle at all, and
// a crossbar reports one. Per-output-cone precision reports it correctly as
// acyclic.
//
// A LOOP sub stays opaque: its body is a rolled occurrence standing for `count`
// replicas, so its internal cones do not describe the caller's schedule. A
// body-less black box yields an empty set, i.e. a boundary -- which is exactly
// how every Sub used to be treated.
using Sub_dep_key   = std::pair<hhds::Gid, uint32_t>;
using Sub_dep_cache = absl::flat_hash_map<Sub_dep_key, absl::flat_hash_set<uint32_t>>;

static absl::flat_hash_set<uint32_t> sub_output_deps(const hhds::Node_class& inst, uint32_t out_pid, Sub_dep_cache& cache) {
  absl::flat_hash_set<uint32_t> res;
  if (inst.is_invalid() || inst.is_loop_subnode()) {
    return res;
  }
  const Sub_dep_key key{inst.get_subnode_gid(), out_pid};
  if (auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  // Seed EMPTY before recursing: a hierarchy that reaches itself contributes no
  // new comb dependency, and the seed doubles as the recursion guard.
  cache.emplace(key, absl::flat_hash_set<uint32_t>{});

  auto cg = inst.get_subnode_graph();
  if (!cg) {
    return res;  // body-less black box: opaque, exactly as before
  }
  auto gio = cg->get_io();
  if (!gio) {
    return res;
  }
  absl::flat_hash_map<std::string, uint32_t> in_name2pid;
  for (const auto& d : gio->get_input_pin_decls()) {
    in_name2pid[d.name] = static_cast<uint32_t>(d.port_id);
  }
  std::string oname;
  for (const auto& d : gio->get_output_pin_decls()) {
    if (static_cast<uint32_t>(d.port_id) == out_pid) {
      oname = d.name;
      break;
    }
  }
  if (oname.empty()) {
    // An output the callee does not declare: fall back to the conservative
    // crossbar rather than silently reporting independence.
    for (const auto& [nm, pid] : in_name2pid) {
      res.insert(pid);
    }
    cache[key] = res;
    return res;
  }
  auto opin = cg->get_output_pin(oname);
  if (opin.is_invalid()) {
    cache[key] = res;
    return res;
  }

  absl::flat_hash_set<hhds::Pin_class> seen;
  std::vector<hhds::Pin_class>         work;
  for (const auto& e : opin.inp_edges()) {
    work.push_back(e.driver);
  }
  while (!work.empty()) {
    auto d = work.back();
    work.pop_back();
    if (d.is_invalid() || is_const_pin(d) || !seen.insert(d).second) {
      continue;
    }
    if (is_graph_input_pin(d)) {
      if (auto it = in_name2pid.find(std::string{pin_name_of(d)}); it != in_name2pid.end()) {
        res.insert(it->second);
      }
      continue;
    }
    auto dn = d.get_master_node();
    if (dn.is_invalid()) {
      continue;
    }
    const auto op = type_op_of(dn);
    if (op == Ntype_op::Memory || op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop) {
      continue;  // state cuts the cone, so it contributes no comb dependency
    }
    if (op == Ntype_op::Sub) {
      const auto inner = sub_output_deps(dn, static_cast<uint32_t>(d.get_port_id()), cache);
      for (const auto& e : dn.inp_edges()) {
        if (inner.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
          work.push_back(e.driver);
        }
      }
      continue;
    }
    for (const auto& e : dn.inp_edges()) {
      work.push_back(e.driver);
    }
  }
  cache[key] = res;
  return res;
}

bool comb_pin_depends_on(const hhds::Pin_class& driver, const hhds::Node_class& target) {
  if (driver.is_invalid() || target.is_invalid() || is_const_pin(driver) || is_graph_input_pin(driver)) {
    return false;
  }
  // A PIN worklist, not a node one: crossing a Sub depends on WHICH output pin
  // the walk arrived through, and dedup must therefore be per pin.
  absl::flat_hash_set<hhds::Pin_class> seen;
  Sub_dep_cache                        dep_cache;
  std::vector<hhds::Pin_class>         work{driver};
  while (!work.empty()) {
    auto d = work.back();
    work.pop_back();
    if (d.is_invalid() || is_const_pin(d) || is_graph_input_pin(d) || !seen.insert(d).second) {
      continue;
    }
    auto n = d.get_master_node();
    if (n.is_invalid()) {
      continue;
    }
    if (n == target) {
      return true;
    }
    const auto op = type_op_of(n);
    if (op == Ntype_op::Memory || is_type_register(n)) {
      continue;
    }
    if (op == Ntype_op::Sub) {
      // Follow only the inputs this particular output actually depends on. An
      // empty set (loop sub, black box, state-fed output) leaves the instance a
      // boundary, which is how every Sub used to be treated.
      const auto deps = sub_output_deps(n, static_cast<uint32_t>(d.get_port_id()), dep_cache);
      for (const auto& e : n.inp_edges()) {
        if (deps.contains(static_cast<uint32_t>(e.sink.get_port_id()))) {
          work.push_back(e.driver);
        }
      }
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
  return false;
}

// `max_rounds` is the caller's round budget. The per-wire entry point can afford
// the full 16 because it hands over a scoped buffer/driver pair and stops the
// moment `comb_pin_depends_on` goes false. A caller with NO such stop condition
// (the retired split_packed_selfref_cycles, which re-derived the cycle set
// itself) must ask for ONE round: running the fixpoint blind keeps splitting
// long after the cycle is gone and leaves a chain of identity Get_masks behind.
static Split_result split_packed_selfref_wires_body(hhds::Graph* g, int max_depth,
                                                    const absl::flat_hash_set<hhds::Node_class>* scoped_cycle,
                                                    const hhds::Node_class* scoped_buffer, const hhds::Pin_class* scoped_driver,
                                                    int max_rounds = 16) {
  // Iterate to a fixpoint. Each pass defers its rewrites to the end, so a reader
  // whose value depends on ANOTHER reader (nested slice-of-slice packing, e.g.
  // Phr's io bundle read as `io#[..]#[..]`) can only resolve one nesting level per
  // pass. Loop until a pass rewrites nothing; a hard round cap is the safety net.
  Split_result r;
  for (; r.rounds < max_rounds; ++r.rounds) {
    const int n  = split_selfref_pass(g, r.unresolved, r.stop_reasons, max_depth, scoped_cycle);
    r.total     += n;
    if (scoped_buffer != nullptr && scoped_driver != nullptr && n > 0 && !comb_pin_depends_on(*scoped_driver, *scoped_buffer)) {
      ++r.rounds;
      r.fixpoint = true;
      break;
    }
    if (n == 0) {
      ++r.rounds;         // count the fixpoint round that proved there was nothing left
      r.fixpoint = true;  // ... and record WHY the loop ended
      break;              // cycle gone, or genuinely stuck
    }
  }
  return r;
}

void word_level_cycle_nodes(hhds::Graph* g, bool strict, absl::flat_hash_set<hhds::Node_class>& out,
                            const absl::flat_hash_set<hhds::Class_index>* allowed) {
  namespace gu = livehd::graph_util;
  auto comb    = [strict](const hhds::Node_class& n) {
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::IO || op == Ntype_op::Nconst) {
      return false;
    }
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      return false;  // true state cuts in both models
    }
    if (op == Ntype_op::Memory || op == Ntype_op::Sub) {
      return strict;
    }
    return true;
  };
  std::vector<hhds::Node_class>                                        nodes;
  absl::flat_hash_map<hhds::Node_class, int>                           indeg;
  absl::flat_hash_map<hhds::Node_class, std::vector<hhds::Node_class>> succ;
  for (auto n : g->body().nodes()) {
    if ((allowed == nullptr || allowed->contains(n.get_class_index())) && comb(n)) {
      nodes.push_back(n);
      indeg.try_emplace(n, 0);
    }
  }
  for (auto& n : nodes) {
    for (auto e : n.inp_edges()) {
      auto d = e.driver;
      if (d.is_invalid() || gu::is_const_pin(d)) {
        continue;
      }
      auto m = d.get_master_node();
      if (!indeg.contains(m)) {
        continue;  // the `contains` guard also drops boundary masters fast_class never lists
      }
      ++indeg[n];
      succ[m].push_back(n);
    }
  }
  std::vector<hhds::Node_class> q;
  for (auto& [n, d] : indeg) {
    if (d == 0) {
      q.push_back(n);
    }
  }
  absl::flat_hash_set<hhds::Node_class> removed;
  while (!q.empty()) {
    auto n = q.back();
    q.pop_back();
    removed.insert(n);
    auto it = succ.find(n);
    if (it == succ.end()) {
      continue;
    }
    for (auto& sx : it->second) {
      if (--indeg[sx] == 0) {
        q.push_back(sx);
      }
    }
  }
  for (auto& n : nodes) {
    if (!removed.contains(n)) {
      out.insert(n);
    }
  }
}

void comb_emit_order(hhds::Graph* g, std::vector<hhds::Node_class>& order, absl::flat_hash_set<hhds::Node_class>* cut_subs,
                     std::vector<hhds::Node_class>* residual) {
  order.clear();
  if (cut_subs != nullptr) {
    cut_subs->clear();
  }
  if (residual != nullptr) {
    residual->clear();
  }
  if (g == nullptr) {
    return;
  }

  // The BACKEND's placement model, which is not the splitter's: a `Sub` is an
  // ordinary node here because an instance is one indivisible item in the
  // emitted schedule, and state/Memory/Clock_cell are sources because something
  // else drives their outputs.
  const auto placeable = [](const hhds::Node_class& n) {
    const auto op = type_op_of(n);
    return op != Ntype_op::IO && op != Ntype_op::Nconst && op != Ntype_op::Memory && op != Ntype_op::Clock_cell
           && !is_type_register(n);
  };

  std::vector<hhds::Node_class>              nodes;
  absl::flat_hash_map<hhds::Node_class, int> idx;
  for (auto n : g->body().nodes()) {
    if (placeable(n)) {
      idx.emplace(n, static_cast<int>(nodes.size()));
      nodes.push_back(n);
    }
  }
  const int total = static_cast<int>(nodes.size());
  if (total == 0) {
    return;
  }

  std::vector<int>              indeg(total, 0);
  std::vector<std::vector<int>> succ(total);
  for (int i = 0; i < total; ++i) {
    for (const auto& e : nodes[i].inp_edges()) {
      if (e.driver.is_invalid() || is_const_pin(e.driver)) {
        continue;
      }
      auto d = e.driver.get_master_node();
      if (auto it = idx.find(d); it != idx.end() && it->second != i) {
        ++indeg[i];
        succ[it->second].push_back(i);
      }
    }
  }

  // STABLE Kahn with a min-heap keyed by storage index, the same tie-break hhds
  // uses, so an already-topological input comes back verbatim.
  std::priority_queue<int, std::vector<int>, std::greater<>> ready;
  for (int i = 0; i < total; ++i) {
    if (indeg[i] == 0) {
      ready.push(i);
    }
  }
  std::vector<char> emitted(total, 0);
  int               done = 0;
  while (done < total) {
    if (ready.empty()) {
      // Stalled on a word-level cycle. Force the lowest-index unemitted `Sub`
      // through: an instance boundary is where the emitter can legally break
      // the block, because the module item is a concurrent construct that the
      // simulator schedules for us. Nothing else in the cycle can be cut
      // without changing what the emitted RTL means.
      int cut = -1;
      for (int i = 0; i < total; ++i) {
        if (!emitted[i] && type_op_of(nodes[i]) == Ntype_op::Sub) {
          cut = i;
          break;
        }
      }
      if (cut < 0) {
        break;  // a genuine comb loop with no instance on it: report below
      }
      if (cut_subs != nullptr) {
        cut_subs->insert(nodes[cut]);
      }
      ready.push(cut);
    }
    const int i = ready.top();
    ready.pop();
    if (emitted[i]) {
      continue;
    }
    emitted[i] = 1;
    ++done;
    order.push_back(nodes[i]);
    for (const int sx : succ[i]) {
      if (--indeg[sx] == 0 && emitted[sx] == 0) {
        ready.push(sx);
      }
    }
  }
  if (done < total && residual != nullptr) {
    for (int i = 0; i < total; ++i) {
      if (!emitted[i]) {
        residual->push_back(nodes[i]);
      }
    }
  }
}

int flatten_false_loop_subs(hhds::Graph* g) {
  // A replicated Sub is never a false-loop target: dissolving one keeps a
  // single body copy and drops count-1 replicas (see graph/replica_desc.hpp).
  // Physical backends materialize it only in their private output state.
  namespace gu = livehd::graph_util;  // the lambdas below qualify with it

  // The whole CLOSURE must be state-free: nested comb Subs are fine (the clone
  // below re-instantiates them in `g` as ordinary pure-comb leaf instances --
  // the ExeUnitImp_4/Alu/AluDataModule shape), but any Flop/Latch/Fflop/Memory
  // anywhere makes inlining change state identity, so those are never touched.
  // Same predicate the cycle walks above use, so "can I dissolve this?" and
  // "should I have seen through this?" cannot drift apart.
  Sub_comb_cache sub_cache;

  auto driver_of = [](const hhds::Pin_class& sink) -> hhds::Pin_class {
    if (sink.is_invalid()) {
      return {};
    }
    for (auto e : sink.get_master_node().inp_edges()) {
      if (e.sink.get_port_id() == sink.get_port_id()) {
        return e.driver;
      }
    }
    return {};
  };

  // A Sub S is on a false loop iff a backward COMB walk from one of its input
  // drivers reaches S's own output (stopping at any other state/loop_last).
  //
  // The walk STOPS at every other Sub, deliberately. A ring that closes only
  // through two or three DIFFERENT instances is unschedulable for a consumer
  // whose model makes a comb callee ONE ATOMIC node — but that consumer is no
  // longer this one. `inou.cgen.sim` dissolves those rings by CALLEE
  // PARTITIONING now (per-output-group `__settle_g<k>` methods, the 2026-08-06
  // ruling) and does not call this at all; the sole caller left is
  // `inou.cgen.verilog`, where an instance is not atomic and the ring is not a
  // scheduling problem. Traversing through instances there inlines whole
  // multi-instance rings out of the emitted netlist AND — since this rewrites
  // in place — out of the shared library graph, which silently breaks the
  // hierarchical boundaries hier-LEC pairing and semdiff's sub-cutpoints key on.
  auto on_false_loop = [&](const hhds::Node_class& s) {
    absl::flat_hash_set<hhds::Node_class> seen;
    std::vector<hhds::Pin_class>          stk;
    for (auto e : s.inp_edges()) {
      stk.push_back(e.driver);
    }
    while (!stk.empty()) {
      auto d = stk.back();
      stk.pop_back();
      if (d.is_invalid() || gu::is_const_pin(d)) {
        continue;
      }
      auto m = d.get_master_node();
      if (m == s) {
        return true;
      }
      auto op = gu::type_op_of(m);
      if (op == Ntype_op::Sub || op == Ntype_op::Memory || gu::is_type_register(m) || op == Ntype_op::IO) {
        continue;  // a real state boundary -- the loop does not thread through it
      }
      if (!seen.insert(m).second) {
        continue;
      }
      for (auto e : m.inp_edges()) {
        stk.push_back(e.driver);
      }
    }
    return false;
  };

  // Fixpoint rounds: inlining one level can MOVE the false loop onto a nested
  // instance that just became a direct child (Alu -> AluDataModule), so rescan
  // until no on-false-loop comb-closure Sub remains (bounded).
  int flattened = 0;
  for (int round = 0; round < 8; ++round) {
    std::vector<hhds::Node_class> targets;
    for (auto node : g->body().nodes()) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      if (node.is_loop_subnode()) {
        continue;  // count occurrences, not one — inlining here drops count-1 replicas
      }
      if (!sub_closure_is_comb(node.get_subnode_graph(), sub_cache) || !on_false_loop(node)) {
        continue;
      }
      targets.push_back(node);
    }
    if (targets.empty()) {
      break;
    }
    flattened += static_cast<int>(targets.size());

    for (auto& sub : targets) {
      auto cg  = sub.get_subnode_graph();
      auto sio = sub.get_subnode_io();
      if (!cg || !sio) {
        continue;
      }
      // The Sub's driver for each input port (by port id) -- feeds a callee input.
      absl::flat_hash_map<uint32_t, hhds::Pin_class> sub_in_drv;
      for (auto e : sub.inp_edges()) {
        sub_in_drv[static_cast<uint32_t>(e.sink.get_port_id())] = e.driver;
      }

      // (a) copy every callee comb node into g (consts/IO ports handled on demand).
      absl::flat_hash_map<hhds::Node_class, hhds::Node_class> nmap;
      for (auto cn : cg->body().nodes()) {
        auto op = gu::type_op_of(cn);
        if (op == Ntype_op::IO || op == Ntype_op::Nconst) {
          continue;
        }
        auto neo = gu::create_typed_node(*g, op);
        if (op == Ntype_op::Sub) {
          // a nested comb Sub is re-instantiated in g as an ordinary child
          // instance (the closure check above guarantees it is state-free)
          if (auto loop = cn.subnode_loop()) {
            neo.set_subnode(cn.get_subnode_io(), *loop);
          } else {
            neo.set_subnode(cn.get_subnode_io());
          }
        }
        if (gu::has_name(cn)) {
          neo.attr(hhds::attrs::name).set(std::string{gu::node_name_of(cn)});
        }
        nmap[cn] = neo;
      }

      // A callee driver pin -> the equivalent driver pin in g.
      auto map_driver = [&](const hhds::Pin_class& cdrv) -> hhds::Pin_class {
        if (gu::is_graph_input_pin(cdrv)) {
          // a callee INPUT port -> the Sub's driver for that port (same port id)
          auto it = sub_in_drv.find(static_cast<uint32_t>(cdrv.get_port_id()));
          return it == sub_in_drv.end() ? hhds::Pin_class{} : it->second;
        }
        if (gu::is_const_pin(cdrv)) {
          return gu::create_const(*g, gu::hydrate_const(cdrv));  // recreate the const in g
        }
        auto mit = nmap.find(cdrv.get_master_node());
        if (mit == nmap.end()) {
          return {};  // an uncopied node (should not happen for a pure-comb callee)
        }
        auto neo = mit->second;
        auto np  = neo.create_driver_pin(cdrv.get_port_id());
        if (auto b = gu::bits_of(cdrv); b != 0) {
          gu::set_bits(np, b);
        }
        if (!gu::is_unsign(cdrv)) {
          gu::set_sign(np);
        }
        if (auto pn = gu::pin_name_of(cdrv); !pn.empty()) {
          gu::set_pin_name(np, pn);
        }
        return np;
      };

      // (b) rewire the callee's internal edges onto the copies.
      for (auto cn : cg->body().nodes()) {
        auto it = nmap.find(cn);
        if (it == nmap.end()) {
          continue;
        }
        for (auto e : cn.inp_edges()) {
          auto gdrv = map_driver(e.driver);
          if (gdrv.is_invalid()) {
            continue;
          }
          gdrv.connect_sink(it->second.create_sink_pin(e.sink.get_port_id()));
        }
      }

      // (c) resolve each callee OUTPUT port to its g-driver and the Sub-output's
      // consumer sinks (computed BEFORE deleting the Sub, which still owns them).
      // Walk out_edges instead of probing get_driver_pin per decl: a declared
      // output with no consumer has no materialized pin and hhds asserts.
      absl::flat_hash_map<uint32_t, std::vector<hhds::Pin_class>> out_sinks;
      for (const auto& oe : sub.out_edges()) {
        out_sinks[static_cast<uint32_t>(oe.driver.get_port_id())].push_back(oe.sink);
      }
      std::vector<std::pair<hhds::Pin_class, std::vector<hhds::Pin_class>>> reconnect;
      for (const auto& od : sio->get_output_pin_decls()) {
        auto sit = out_sinks.find(static_cast<uint32_t>(od.port_id));
        if (sit == out_sinks.end()) {
          continue;
        }
        auto internal = driver_of(cg->get_output_pin(od.name));  // driver inside the callee
        if (internal.is_invalid()) {
          continue;
        }
        auto gdrv = map_driver(internal);
        if (gdrv.is_invalid()) {
          continue;
        }
        reconnect.emplace_back(gdrv, std::move(sit->second));
      }

      sub.del_node();  // drops the Sub + all its boundary edges

      // (d) drive the former Sub-output consumers from the inlined logic.
      for (auto& [gdrv, sinks] : reconnect) {
        for (auto& sk : sinks) {
          gdrv.connect_sink(sk);
        }
      }
    }
  }  // fixpoint rounds
  return flattened;
}

// The pass walks packed bit-slices RECURSIVELY, one frame per nesting level, at
// roughly kSplitFrameBytes a frame. A stack that only reaches ~1000 frames is
// not deep enough for XiangShan's packed structs, and the residual "word-level
// combinational cycle" it leaves behind makes the occurrence color scheduler
// refuse the module, taking the whole design's simulation with it.
//
// Depth is therefore a STACK budget. Rather than cap the design, ESCALATE: run
// the walk on the caller's own stack first under the conservative
// kSplitInlineDepth guard, and only when a descent actually reports
// `recursion-depth` re-run it on a private kSplitStackBytes worker under
// kSplitWorkerDepth. The common shallow wire bind then costs neither a thread
// nor a 512 MB mapping.
//
// Running it twice is safe: the walk is a fixpoint over the graph it is handed,
// so the deep run simply continues from what the shallow one left.
//
// pthreads directly, not std::thread, because the stack size is the whole point
// and std::thread cannot set it.
namespace {
struct Split_job {
  hhds::Graph*                                 g             = nullptr;
  const absl::flat_hash_set<hhds::Node_class>* scoped_cycle  = nullptr;
  const hhds::Node_class*                      scoped_buffer = nullptr;
  const hhds::Pin_class*                       scoped_driver = nullptr;
  int                                          max_rounds    = 16;
  Split_result                                 result;
  std::exception_ptr                           error;  // rethrown on the CALLER's thread after the join
};

void* split_worker(void* arg) {
  auto* job = static_cast<Split_job*>(arg);
  // An exception must never escape a pthread start routine: that would call
  // std::terminate instead of returning the lowering error on the caller.
  try {
    job->result = split_packed_selfref_wires_body(job->g,
                                                 kSplitWorkerDepth,
                                                 job->scoped_cycle,
                                                 job->scoped_buffer,
                                                 job->scoped_driver,
                                                 job->max_rounds);
  } catch (...) {
    job->error = std::current_exception();
  }
  return nullptr;
}

// Run the deep walk on a private big stack. Returns false when no such stack
// could be had (an unusual rlimit, thread exhaustion); the caller then keeps the
// shallow result rather than running the deep guard on a stack too small to hold
// it.
bool run_deep_on_big_stack(hhds::Graph* g, Split_result& out, const absl::flat_hash_set<hhds::Node_class>* scoped_cycle,
                           const hhds::Node_class* scoped_buffer, const hhds::Pin_class* scoped_driver, int max_rounds = 16) {
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) {
    return false;
  }
  Split_job job{.g             = g,
                .scoped_cycle  = scoped_cycle,
                .scoped_buffer = scoped_buffer,
                .scoped_driver = scoped_driver,
                .max_rounds    = max_rounds,
                .result        = {},
                .error         = {}};
  pthread_t tid{};
  int       rc = pthread_attr_setstacksize(&attr, kSplitStackBytes);
  if (rc == 0) {
    rc = pthread_create(&tid, &attr, split_worker, &job);
  }
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    return false;
  }
  pthread_join(tid, nullptr);
  if (job.error) {
    std::rethrow_exception(job.error);
  }
  out = job.result;
  return true;
}
}  // namespace

int split_packed_selfref_cycles(hhds::Graph* g) {
  // ── RETIRED 2026-08-20 — TRIP-WIRE, no callers ───────────────────────────
  // This whole-graph entry point is retired and every call site was removed.
  // The body is kept for a soak period only; if nothing trips this in a few
  // days it goes away with the rest of the function.
  //
  // WHY it must never run: a graph leaving lnast.tolg cannot carry a packed
  // self-reference -- resolve_wire_selfref splits each `wire` against its
  // COMPLETE driver over the scoped buffer/driver cone, and a residual
  // dependency is raised there as `combinational loop through wire`. The only
  // way one appeared afterwards was a mutation dissolving a comb `Sub`, and
  // the repair for that belongs to the mutator, over the region it actually
  // changed -- not to a 16-round Kahn peel over every node in the design.
  // Emitted Verilog re-enters through lnast.tolg on read-back, so even a
  // false loop that escaped into a file is broken again on the way in.
  //
  // diag::err().fatal() rather than I(false, ...) deliberately: iassert's I()
  // expands to nothing under NDEBUG, so an `-c opt` bench -- exactly where
  // this would be exercised -- would call it in complete silence.
  {
    std::string gname{"<null>"};
    if (g != nullptr) {
      gname = std::string{g->get_name()};
    }
    livehd::diag::err("split-selfref", "retired-entry-point", "internal")
        .msg("split_packed_selfref_cycles('{}') was called, but this whole-graph scan is RETIRED and has no callers",
             gname)
        .hint(
            "whoever dissolved a comb Sub owns the repair over the region it changed; see split_packed_selfref_wire "
            "(upass/tolg, cone-scoped) for the model")
        .fatal();
  }
  if (g == nullptr) {
    return 0;
  }
  // Re-derive the cycle every round and stop as soon as none is left. The
  // per-wire entry point gets that stop condition for free -- it hands
  // split_packed_selfref_wires_body a scoped buffer/driver pair, and the body
  // breaks the moment `comb_pin_depends_on` goes false. There is no single
  // buffer here, so drive the fixpoint from the cycle set itself; running the
  // body's 16 rounds blind instead keeps splitting long after the cycle is
  // gone and leaves a chain of identity Get_masks behind (measured: 13 on a
  // 3-statement design, versus the 2 rounds it actually needs).
  //
  // `strict=false` is the splitter's own scheduling model, so this asks exactly
  // the question the per-wire entry point asks -- but over the graph AS IT IS
  // NOW, after the boundary was dissolved.
  constexpr int max_rounds   = 16;
  int           total        = 0;
  int           unresolved   = 0;
  unsigned      stop_reasons = 0;
  const bool    debug        = std::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr;

  absl::flat_hash_set<hhds::Node_class> cyc;
  int                                   round = 0;
  for (; round < max_rounds; ++round) {
    cyc.clear();
    word_level_cycle_nodes(g, /*strict=*/false, cyc);
    if (cyc.empty()) {
      break;  // an acyclic word-level schedule exists; nothing left to do
    }
    unsigned  pass_stop = 0;
    const int n         = split_selfref_pass(g, unresolved, pass_stop, kSplitInlineDepth, &cyc);
    stop_reasons       |= pass_stop;
    if (n == 0) {
      // Nothing dissolved. A deep nest is the one recoverable reason; retry it
      // on the worker stack, then stop either way.
      if ((pass_stop & kStopDepth) != 0) {
        Split_result deep;
        // ONE round: the outer loop re-derives the cycle set and re-enters, so
        // the deep walk must not run its own blind fixpoint (see the body's
        // `max_rounds` note).
        if (run_deep_on_big_stack(g, deep, &cyc, nullptr, nullptr, /*max_rounds=*/1) && deep.total > 0) {
          total += deep.total;
          continue;
        }
      }
      break;  // genuinely stuck: a real bit-level loop, or a shape with no rule
    }
    total += n;
  }
  if (debug) {
    std::print("split[cycles]: rounds={} rewired={} unresolved={} stop={:#x}\n", round, total, unresolved, stop_reasons);
  }
  // NEVER fail silently. This entry point has no source-located owner the way
  // the per-wire one does (upass/tolg raises the `combinational loop` error
  // there), so a cycle that survives here is reported by the mutator that
  // exposed it -- otherwise the only symptom is a downstream refusal with no
  // link back to the instance that was dissolved ("operand has no encodable
  // driver", verilator ALWCOMBORDER, or a sim schedule that rejects the graph).
  if (!cyc.empty()) {
    std::string why;
    for (const auto& [bit, name] : {
             std::pair{kStopBudget,     "node-budget"},
             std::pair{ kStopDepth, "recursion-depth"},
             std::pair{ kStopShape,   "operand-shape"}
    }) {
      if (stop_reasons & bit) {
        why += why.empty() ? name : std::string(",") + name;
      }
    }
    if (why.empty()) {
      why = "no-rule";  // every descent was refused by an unhandled operator, not by a limit
    }
    livehd::diag::warn("split-selfref", "unresolved-cycle", "internal")
        .msg(
            "'{}': {} node(s) remain on a word-level combinational cycle after {} split pass(es) ({} read(s) rewired, {} "
            "unresolved; stopped by {})",
            g->get_name(),
            cyc.size(),
            round,
            total,
            unresolved,
            why)
        .hint(stop_reasons == kStopBudget
                  ? "only the node-creation budget stopped it; the budget scales with the on-cycle node count"
                  : (stop_reasons & (kStopDepth | kStopShape)) != 0
                        ? "a descent hit the recursion guard or an operand shape this pass cannot split -- run with "
                          "LIVEHD_SIM_SPLIT_DEBUG=1 to see the refusing slice"
                        : "likely a genuine bit-level self-dependency (e.g. w = w + 1)")
        .emit();
  }
  return total;
}


int split_packed_selfref_wire(hhds::Graph* g, const hhds::Node_class& buffer, const hhds::Pin_class& driver,
                              const std::vector<hhds::Node_class>& early_readers) {
  // Guard BEFORE the trace: debug_name() dereferences the handle, so tracing an
  // invalid buffer/driver would crash exactly the runs that turned tracing on.
  if (g == nullptr || buffer.is_invalid() || driver.is_invalid() || early_readers.empty()) {
    return 0;
  }
  const bool debug = std::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr;
  if (debug) {
    std::print("split[wire]: buffer={} driver={} early={}\n",
               debug_name(buffer),
               debug_name(driver.get_master_node()),
               early_readers.size());
  }

  // The defining edge is already present. Find only nodes in the driver's
  // backward cone, then retain the portion reachable from a read that existed
  // before the wire write. Their intersection is precisely the local cycle
  // candidate closed by this one edge. Later readers never enter the seed set.
  absl::flat_hash_set<hhds::Node_class> ancestors;
  std::vector<hhds::Node_class>         work;
  Sub_comb_cache                        sub_cache;
  // A pure-comb Sub is TRANSPARENT: letting the node into the walk is all the
  // transparency needed, since the generic inp_edges()/out_edges() traversal
  // then crosses the boundary in whichever direction it is walking.
  const auto is_comb = [&sub_cache](const hhds::Node_class& n) {
    const auto op = livehd::graph_util::type_op_of(n);
    if (op == Ntype_op::IO || op == Ntype_op::Nconst || op == Ntype_op::Memory
        || livehd::graph_util::is_type_register(n)) {
      return false;
    }
    if (op == Ntype_op::Sub) {
      return sub_is_pure_comb(n, sub_cache);
    }
    return true;
  };
  auto root = driver.get_master_node();
  if (root.is_invalid() || !is_comb(root)) {
    return 0;
  }
  work.push_back(root);
  while (!work.empty()) {
    auto n = work.back();
    work.pop_back();
    if (n.is_invalid() || !ancestors.insert(n).second) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      if (e.driver.is_invalid() || livehd::graph_util::is_const_pin(e.driver) || livehd::graph_util::is_graph_input_pin(e.driver)) {
        continue;
      }
      auto pred = e.driver.get_master_node();
      if (!pred.is_invalid() && is_comb(pred)) {
        work.push_back(pred);
      }
    }
  }
  if (!ancestors.contains(buffer)) {
    if (debug) {
      std::print("split[wire]: no dependency (ancestors={})\n", ancestors.size());
    }
    return 0;  // attaching the driver did not close a self dependency
  }

  absl::flat_hash_set<hhds::Node_class> scoped_cycle;
  for (const auto& reader : early_readers) {
    if (!reader.is_invalid() && ancestors.contains(reader)) {
      work.push_back(reader);
    }
  }
  while (!work.empty()) {
    auto n = work.back();
    work.pop_back();
    if (n.is_invalid() || !ancestors.contains(n) || !scoped_cycle.insert(n).second) {
      continue;
    }
    for (const auto& e : n.out_edges()) {
      auto succ = e.sink.get_master_node();
      if (!succ.is_invalid() && ancestors.contains(succ)) {
        work.push_back(succ);
      }
    }
  }
  scoped_cycle.insert(buffer);
  scoped_cycle.insert(root);
  if (debug) {
    std::print("split[wire]: ancestors={} scoped={}\n", ancestors.size(), scoped_cycle.size());
  }

  Split_result r = split_packed_selfref_wires_body(g, kSplitInlineDepth, &scoped_cycle, &buffer, &driver);
  if ((r.stop_reasons & kStopDepth) != 0) {
    Split_result deep;
    if (run_deep_on_big_stack(g, deep, &scoped_cycle, &buffer, &driver)) {
      deep.total  += r.total;
      deep.rounds += r.rounds;
      r            = deep;
    }
  }
  // The TolG caller owns the source-located diagnostic when a genuine cycle
  // remains; do not emit the legacy global splitter warning here.
  if (debug) {
    std::print("split[wire]: rewired={} unresolved={} rounds={}\n", r.total, r.unresolved, r.rounds);
  }
  return r.total;
}

}  // namespace livehd::graph_util
