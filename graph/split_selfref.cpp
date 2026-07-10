// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "split_selfref.hpp"

#include <algorithm>
#include <cstdlib>
#include <print>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"       // Ntype / Ntype_op
#include "node_util.hpp"  // livehd::graph_util::* helpers

namespace livehd::graph_util {

namespace {
// Debug-print helper (mirrors the cgen_sim local; only used by split[dbg] lines).
const char* op_name(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum: return "Sum";
    case Ntype_op::Mult: return "Mult";
    case Ntype_op::Div: return "Div";
    case Ntype_op::And: return "And";
    case Ntype_op::Or: return "Or";
    case Ntype_op::Xor: return "Xor";
    case Ntype_op::Not: return "Not";
    case Ntype_op::LT: return "LT";
    case Ntype_op::GT: return "GT";
    case Ntype_op::EQ: return "EQ";
    case Ntype_op::SHL: return "SHL";
    case Ntype_op::SRA: return "SRA";
    case Ntype_op::Mux: return "Mux";
    case Ntype_op::Hotmux: return "Hotmux";
    case Ntype_op::Get_mask: return "Get_mask";
    case Ntype_op::Set_mask: return "Set_mask";
    case Ntype_op::Sext: return "Sext";
    case Ntype_op::Nconst: return "Nconst";
    default: return "op?";
  }
}
}  // namespace

// Break a FALSE word-level combinational loop through a PACKED wire. A single net
// `W` driven by an `Or` (a bit-field pack) whose operands occupy DISJOINT constant
// bit ranges is really a concat: a constant Get_mask slice of `W` reads only ONE
// operand. inou.cgen.sim schedules `W` as one atomic object, so a slice-read of
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
int split_packed_selfref_wires(hhds::Graph* g) {
  namespace gu = livehd::graph_util;

  auto is_comb = [](const hhds::Node_class& n) {
    auto op = gu::type_op_of(n);
    return !(op == Ntype_op::IO || op == Ntype_op::Nconst || op == Ntype_op::Sub || op == Ntype_op::Memory
             || gu::is_type_register(n));
  };

  // --- gate: which comb nodes sit on a word-level cycle (Kahn peel of sources) ---
  std::vector<hhds::Node_class>                                       comb_nodes;
  absl::flat_hash_map<hhds::Node_class, int>                          indeg;
  absl::flat_hash_map<hhds::Node_class, std::vector<hhds::Node_class>> succ;
  for (auto n : g->fast_class()) {
    if (is_comb(n)) {
      comb_nodes.push_back(n);
      indeg.try_emplace(n, 0);
    }
  }
  for (auto& n : comb_nodes) {
    for (auto e : n.inp_edges()) {
      auto d = e.driver;
      if (d.is_invalid() || gu::is_const_pin(d)) {
        continue;
      }
      auto m = d.get_master_node();
      if (!is_comb(m) || !indeg.contains(m)) {
        // state element / Sub / IO / const -> not a comb edge. The contains()
        // guard also drops BOUNDARY masters fast_class never enumerates (a
        // graph-input pin's master reports a non-IO "invalid" type): counting
        // those left every input-fed node at indeg>0, so the peel removed
        // NOTHING and the whole module looked on-cycle.
        continue;
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
  if (std::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr) {
    std::print("split[dbg]: peel seeds={} of {}\n", q.size(), indeg.size());
    int shown = 0;
    for (auto& [n, d] : indeg) {
      if (shown++ >= 200) {
        break;
      }
      std::string drvs;
      for (auto e : n.inp_edges()) {
        drvs += e.driver.is_invalid()             ? " inv"
                : gu::is_const_pin(e.driver)      ? " const"
                : !is_comb(e.driver.get_master_node()) ? " noncomb"
                                                       : (" " + std::string(gu::debug_name(e.driver.get_master_node())));
      }
      std::print("split[dbg]:   indeg {} = {} <-{}\n", gu::debug_name(n), d, drvs);
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
    for (auto& s : it->second) {
      if (--indeg[s] == 0) {
        q.push_back(s);
      }
    }
  }
  absl::flat_hash_set<hhds::Node_class> in_cycle;
  for (auto& n : comb_nodes) {
    if (!removed.contains(n)) {
      in_cycle.insert(n);
    }
  }
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
      return (gu::is_unsign(p) && b > 1) ? std::pair<int, int>{0, b - 1} : kBail;
    }
    // generic value: an over-approximation is sound only when UNSIGNED (its top
    // sign bit is always 0, so the significant width is bits-1; a 1-bit
    // unsigned pin is {0,1}).
    int b = gu::bits_of(p);
    if (gu::is_unsign(p)) {
      return b > 1 ? std::pair<int, int>{0, b - 1} : std::pair<int, int>{0, 1};
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
  auto mask_const = [&](int lo, int hi) -> hhds::Pin_class {
    return livehd::graph_util::create_const(*g, *Dlop::get_mask_value(hi - 1, lo));
  };
  int created = 0;
  // LIVEHD_SIM_SPLIT_DEBUG=1 traces every reader attempt + the deepest resolve
  // refusal (op, slice) -- the fast way to see WHY a pack did not split.
  const bool split_dbg = std::getenv("LIVEHD_SIM_SPLIT_DEBUG") != nullptr;
  absl::flat_hash_map<std::tuple<hhds::Class_index, int, int>, hhds::Pin_class> memo;
  // A (pin,slice) already on the RESOLUTION STACK means this slice depends on
  // itself -- a GENUINE bit-level cycle: fail it permanently. That makes the
  // depth cap a pure safety net (deep-but-acyclic sbox/crypto chains resolve),
  // and a failure caused ONLY by the caps must NOT be memoized -- an earlier
  // capped frame would otherwise poison every later resolution through that
  // slice (the BlockCipherModule 21-bit sbox hit exactly this).
  absl::flat_hash_set<std::tuple<hhds::Class_index, int, int>> on_stack;
  bool cap_hit = false;
  auto resolve = [&](auto&& self, const hhds::Pin_class& v, int lo, int hi, int depth) -> hhds::Pin_class {
    if (depth > 64 || v.is_invalid() || lo < 0 || hi <= lo || created > 16384) {
      if (split_dbg) {
        std::print("split[dbg]: refuse depth={} lo={} hi={} created={} invalid={}\n", depth, lo, hi, created, v.is_invalid());
      }
      cap_hit = true;
      return {};
    }
    const int w          = hi - lo;
    auto      slice_node = [&](const hhds::Pin_class& val) -> hhds::Pin_class {
      auto n = gu::create_typed_node(*g, Ntype_op::Get_mask);
      ++created;
      val.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
      mask_const(lo, hi).connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(2)));
      auto dp = n.create_driver_pin(0);
      gu::set_bits(dp, w + 1);
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
    const bool cap_before = cap_hit;
    auto            op = gu::type_op_of(m);
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
          auto r = self(self, d, lo, hi, depth + 1);
          if (r.is_invalid()) {
            ok = false;
            break;
          }
          parts.push_back(r);
        }
        if (ok) {
          auto n = gu::create_typed_node(*g, op == Ntype_op::Sum ? Ntype_op::Or : op);
          ++created;
          for (auto& pp : parts) {
            pp.connect_sink(n.create_sink_pin(static_cast<hhds::Port_id>(0)));
          }
          auto dp = n.create_driver_pin(0);
          gu::set_bits(dp, w + 1);
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
              gu::set_bits(dp, w + 1);
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
          rsel   = self(self, sel, 0, std::max(1, sb - 1), depth + 1);
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
          gu::set_bits(dp, w + 1);
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
        gu::set_bits(np, w + 1);
        auto n2 = gu::create_typed_node(*g, Ntype_op::And);
        ++created;
        np.connect_sink(n2.create_sink_pin(static_cast<hhds::Port_id>(0)));
        livehd::graph_util::create_const(*g, *Dlop::get_mask_value(w - 1, 0))
            .connect_sink(n2.create_sink_pin(static_cast<hhds::Port_id>(0)));
        auto dp = n2.create_driver_pin(0);
        gu::set_bits(dp, w + 1);
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
        int b = gu::bits_of(v);
        if (gu::is_unsign(v) && b > 1) {
          if (lo >= b - 1) {
            res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
          } else {
            res = self(self, drv_at(m, 0), lo, std::min(hi, b - 1), depth + 1);
          }
        }
      } else if (gu::is_const_pin(md)) {
        auto mc = gu::hydrate_const(md);
        if (!mc.has_unknowns()) {
          if (mc.is_just_i64() && mc.to_just_i64() == -1) {
            // to-unsigned keeps positions; above the sig width -> zeros
            int b = gu::bits_of(v);
            if (gu::is_unsign(v) && b > 1 && lo >= b - 1) {
              res = livehd::graph_util::create_const(*g, *Dlop::create_integer(0));
            } else if (gu::is_unsign(v) && b > 1) {
              res = self(self, drv_at(m, 0), lo, std::min(hi, b - 1), depth + 1);
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
  for (auto& R : comb_nodes) {
    if (!in_cycle.contains(R)) {
      continue;
    }
    auto rop = gu::type_op_of(R);
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
        std::print("split[dbg]: And-reader j={} k={} sra={} -> {}\n", j, k, gu::type_op_of(sm) == Ntype_op::SRA,
                   res.is_invalid() ? "FAIL" : "ok");
      }
      if (res.is_invalid()) {
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
  const int nrew = static_cast<int>(gm_rewires.size() + and_rewires.size());
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

}  // namespace livehd::graph_util
