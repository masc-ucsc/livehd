//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_cprop.hpp"
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::debug_name;
using livehd::graph_util::find_sink_pin;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::type_op_of;

#define TRACE(x)
// #define TRACE(x) x

namespace {

void sort_inp(livehd::graph_util::Edge_vec& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    if (a.sink.get_port_id() != b.sink.get_port_id()) {
      return a.sink.get_port_id() < b.sink.get_port_id();
    }
    const auto an = a.driver.get_master_node().get_debug_nid();
    const auto bn = b.driver.get_master_node().get_debug_nid();
    if (an != bn) {
      return an < bn;
    }
    return a.driver.get_port_id() < b.driver.get_port_id();
  });
}

livehd::graph_util::Edge_vec ordered_inp_edges(const hhds::Node_class& node) {
  auto e = node.inp_edges();
  sort_inp(e);
  return e;
}

// Materialize HHDS's forward order once. HHDS owns the topological traversal;
// rebuilding it here used to add another indegree map, heap, and edge walk
// before cprop could inspect a single node. TolG is responsible for producing
// a valid graph, so cprop does not run a second cycle-management algorithm.
std::vector<hhds::Node_class> stable_nodes(hhds::Graph* g) {
  std::vector<hhds::Node_class> nodes;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    nodes.push_back(node);
  }
  return nodes;
}

using livehd::graph_util::hydrate_const;
using livehd::graph_util::setup_sink_by_name;

// A finite Get_mask is an EXPLICIT precision-changing operation: its value has
// exactly popcount(mask) packed magnitude bits, independent of the width of the
// source or of an enclosing typed expression.  Keep that fact on the result pin
// even when a frontend conservatively stamped the destination wider -- that is
// the operation's range CONTRACT in unbounded LGraph semantics, not a
// range-analysis guess, and node_util's debug_check_pin_hint asserts on it
// (`Get_mask pin 'get_mask_48' selects 64 bits but is hinted at 75`). pass.
// bitwidth runs later and cannot retroactively unbreak a graph cprop's own
// rules already read at the wrong width.
void restamp_finite_get_mask(hhds::Node_class& node, const Dlop& mask) {
  if (mask.is_negative() || mask.has_unknowns()) {
    return;
  }
  const auto magnitude_bits = mask.popcount();
  I(magnitude_bits < static_cast<size_t>(std::numeric_limits<int32_t>::max()));
  auto       out      = node.create_driver_pin(0);
  const auto capacity = std::max<int32_t>(1, static_cast<int32_t>(magnitude_bits));
  const auto current  = bits_of(out);
  // A preceding range pass may prove that fewer carrier bits suffice. Keep
  // that stronger fact; this repair is only for a stale enclosing/source width
  // that exceeds the explicit selection's capacity.
  if (current == 0 || current > capacity) {
    livehd::graph_util::set_bits(out, capacity);
  }
  // Do not rewrite signedness here. Get_mask is also the real finite-width
  // landing for a typed signed wire: tolg deliberately stamps that result
  // signed so its selected top bit is the sign bit. Ordinary slice results are
  // already stamped unsigned by their producer. Turning every finite mask
  // unsigned here changed a signed field value such as 8'hff from -1 to 255.
}

// Copy propagation can replace a narrow operand with a wider equivalent
// carrier (most visibly when a packed-field read folds to a constant), and the
// finite Get_mask normalization above can also correct an input's stale width.
// Keep ordinary unlimited-precision operations lossless after those rewrites:
// their result carrier may be wider than the range estimate, never narrower
// than a value input. HLOP repeats this as a consteval assertion in generated
// C++; this graph repair makes the producer metadata satisfy the same contract.
//
// pass.bitwidth running after cprop does NOT subsume this. It re-derives from
// RANGES, and a range that is sound for the operation is not the same fact as
// "this carrier can represent every input it reads" -- the sign-slot case in
// particular (a uW literal needs W+1 bits in a SIGNED carrier) is a
// representation fact, not a range fact. Dropping the repair refuted
// //inou/prp:prp-equiv-rt_typecast and the signed packed-struct LEC.
//
// Explicitly reducing/selecting operations are intentionally absent: And may
// be a mask, SRA drops shifted bits, and Get_mask/Ror/Rem select or reduce.
void enforce_lossless_carriers(hhds::Graph* g) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
      const auto op = type_op_of(node);
      if (op != Ntype_op::Sum && op != Ntype_op::Mult && op != Ntype_op::Or && op != Ntype_op::Xor && op != Ntype_op::Not
          && op != Ntype_op::SHL && op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
        continue;
      }
      auto out = node.create_driver_pin(0);
      if (out.is_invalid()) {
        continue;
      }
      const bool signed_output = !livehd::graph_util::is_unsign(out);
      int        required      = std::max(bits_of(out), 1);
      for (const auto& e : node.inp_edges()) {
        const auto pid = e.sink.get_port_id();
        if ((op == Ntype_op::SHL && pid != 0) || ((op == Ntype_op::Mux || op == Ntype_op::Hotmux) && pid == 0)) {
          continue;  // shift count / selector widths are independent
        }
        int  input_bits   = bits_of(e.driver);
        bool non_negative = livehd::graph_util::is_unsign(e.driver);
        if (is_const_pin(e.driver)) {
          // ONE hydrate per edge: hydrating twice (once for the width, once for
          // the sign below) doubled the deserialization cost of the hottest
          // loop in this pass.
          const auto value = hydrate_const(e.driver);
          non_negative     = !value.is_negative();
          int literal_bits;
          if (value.is_numeric() && !value.has_unknowns() && !value.is_negative()) {
            // Dlop::get_bits() reports a signed carrier width for a positive
            // constant. The graph hint is literal unsigned width, so the
            // carrier's leading zero is not part of this requirement.
            literal_bits = value.is_known_zero() ? 1 : std::max(1, static_cast<int>(value.get_bits()) - 1);
          } else {
            literal_bits = std::max(1, static_cast<int>(value.get_bits()));
          }
          input_bits = std::max(input_bits, literal_bits);
        }
        // Literal uW needs W+1 bits when an unlimited-precision operation
        // lands in a signed carrier. This matters after copy propagation
        // bypasses a finite slice: Not(u8) needs s9, and Or(s1,u1) needs s2.
        // Unsigned outputs keep their literal W-bit realization.
        if (signed_output && non_negative) {
          ++input_bits;
        }
        required = std::max(required, input_bits);
      }
      if (required > bits_of(out)) {
        livehd::graph_util::set_bits(out, required);
        changed = true;
      }
    }
  }
}

// "Does `p` (a driver pin) or `n` (a node) have EXACTLY one consumer edge?"
// Stops at the second edge instead of walking the whole fan-out.
template <typename T>
[[nodiscard]] bool has_single_consumer(const T& p) {
  size_t consumers = 0;
  for ([[maybe_unused]] const auto& e : p.out_edges()) {
    if (++consumers > 1) {
      return false;
    }
  }
  return consumers == 1;
}

// "Is there an edge from `driver` to `sink`?"
[[nodiscard]] bool is_driver_connected_to_sink(const hhds::Pin_class& driver, const hhds::Pin_class& sink) {
  if (driver.is_invalid() || sink.is_invalid()) {
    return false;
  }
  for (const auto& e : driver.out_edges()) {
    if (e.sink == sink) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string sink_pin_name(const hhds::Pin_class& spin) {
  if (spin.is_invalid()) {
    return {};
  }
  auto master = spin.get_master_node();
  auto op     = type_op_of(master);
  return Ntype::get_sink_name(op, spin.get_port_id());
}

[[nodiscard]] hhds::Pin_class drv_at(const hhds::Node_class& n, uint32_t pid);

struct Bool_condition {
  hhds::Pin_class base;
  bool            true_when_base = true;
};

[[nodiscard]] bool same_pin(const hhds::Pin_class& a, const hhds::Pin_class& b) {
  return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
}

[[nodiscard]] std::optional<bool> const_truth(const hhds::Pin_class& p) {
  if (p.is_invalid() || !is_const_pin(p)) {
    return std::nullopt;
  }
  auto c = hydrate_const(p);
  if (c.has_unknowns()) {
    return std::nullopt;
  }
  return !c.is_known_zero();
}

// Reduce the boolean materializations emitted by tolg and by a slang round
// trip: `s ? 1 : 0`, its inverse, and `x == 0` chains. This is deliberately a
// structural decoder, not a general boolean-equivalence proof.
[[nodiscard]] std::optional<Bool_condition> decode_bool_condition_impl(const hhds::Pin_class& p, int depth) {
  if (p.is_invalid()) {
    return std::nullopt;
  }
  if (depth >= 32 || is_const_pin(p) || is_graph_input_pin(p)) {
    return Bool_condition{p, true};
  }
  auto n = p.get_master_node();
  if (type_op_of(n) == Ntype_op::Mux) {
    auto sel  = drv_at(n, 0);
    auto arm0 = drv_at(n, 1);
    auto arm1 = drv_at(n, 2);
    auto v0   = const_truth(arm0);
    auto v1   = const_truth(arm1);
    if (!sel.is_invalid() && v0.has_value() && v1.has_value() && *v0 != *v1) {
      auto result = decode_bool_condition_impl(sel, depth + 1);
      if (result.has_value() && !*v1) {  // arm1 false, arm0 true => !selector
        result->true_when_base = !result->true_when_base;
      }
      return result;
    }
  } else if (type_op_of(n) == Ntype_op::EQ) {
    // The lowering spells truth tests as `(x == 0) == 0`; peel each equality
    // against known zero and carry its inversion bit. EQ's `as` port is
    // multi-driver, so inspect edges rather than drv_at().
    hhds::Pin_class value;
    int             zeros  = 0;
    int             values = 0;
    for (const auto& e : n.inp_edges()) {
      auto truth = const_truth(e.driver);
      if (truth.has_value() && !*truth) {
        ++zeros;
      } else {
        value = e.driver;
        ++values;
      }
    }
    if (zeros == 1 && values == 1) {
      auto result = decode_bool_condition_impl(value, depth + 1);
      if (result.has_value()) {
        result->true_when_base = !result->true_when_base;
      }
      return result;
    }
  }
  return Bool_condition{p, true};
}

[[nodiscard]] std::optional<Bool_condition> decode_bool_condition(const hhds::Pin_class& p) {
  return decode_bool_condition_impl(p, 0);
}

struct Hold_mux_match {
  hhds::Node_class mux;
  hhds::Pin_class  data;
};

[[nodiscard]] bool is_latch_hold_value(hhds::Pin_class p, const hhds::Pin_class& q) {
  for (int depth = 0; depth < 16 && !p.is_invalid(); ++depth) {
    if (same_pin(p, q)) {
      return true;
    }
    if (is_const_pin(p) || is_graph_input_pin(p)) {
      return false;
    }
    auto n  = p.get_master_node();
    auto op = type_op_of(n);
    if (op != Ntype_op::Get_mask && op != Ntype_op::Sext) {
      return false;  // Q+1 and all other real feedback stay visible
    }
    p = drv_at(n, 0);  // value input; the remaining pins describe the coercion
  }
  return false;
}

// Find an enable-qualified `Q` hold mux anywhere in the latch's D cone. Slang
// can wrap a generated hold mux in Get_mask/Sext nodes and then add another
// hold mux while re-reading `always_latch`, so looking only at din's immediate
// driver misses the nested form. A candidate must have a single consumer: its
// disabled value is irrelevant only on this latch-D path, not to arbitrary
// other observers of a shared mux.
[[nodiscard]] std::optional<Hold_mux_match> find_latch_hold_mux(const hhds::Pin_class& start, const hhds::Pin_class& q,
                                                                const Bool_condition& open) {
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{start};
  int                                    visited = 0;
  while (!work.empty() && visited++ < 256) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || is_const_pin(p) || is_graph_input_pin(p) || !seen.insert(p.get_class_index()).second) {
      continue;
    }
    auto n = p.get_master_node();
    if (livehd::graph_util::is_type_register(n) || type_op_of(n) == Ntype_op::Sub) {
      continue;
    }
    if (type_op_of(n) == Ntype_op::Mux) {
      auto       sel  = drv_at(n, 0);
      auto       arm0 = drv_at(n, 1);
      auto       arm1 = drv_at(n, 2);
      const bool q0   = is_latch_hold_value(arm0, q);
      const bool q1   = is_latch_hold_value(arm1, q);
      if (q0 != q1) {
        auto selected = decode_bool_condition(sel);
        if (selected.has_value()) {
          if (!q0) {  // q on arm1 => data selected while selector is false
            selected->true_when_base = !selected->true_when_base;
          }
          if (has_single_consumer(n) && same_pin(selected->base, open.base) && selected->true_when_base == open.true_when_base) {
            return Hold_mux_match{n, q0 ? arm1 : arm0};
          }
        }
      }
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool cone_reaches_q(const hhds::Pin_class& start, const hhds::Pin_class& q) {
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{start};
  int                                    visited = 0;
  while (!work.empty() && visited++ < 256) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || is_const_pin(p) || is_graph_input_pin(p) || !seen.insert(p.get_class_index()).second) {
      continue;
    }
    if (same_pin(p, q)) {
      return true;
    }
    auto n = p.get_master_node();
    if (livehd::graph_util::is_type_register(n) || type_op_of(n) == Ntype_op::Sub) {
      continue;
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
  return false;
}

// Pair `gate ? 1 : data_enable` with a din override on the same gate. The
// override branch must not reach Q, while the normal branch must contain the
// hold path. This proves that the extra aggregate-enable cause is reset-like
// priority logic and does not qualify normal latch data.
[[nodiscard]] bool has_guarded_data_override(const hhds::Pin_class& din, const hhds::Pin_class& q, const Bool_condition& gate,
                                             bool override_when_base) {
  absl::flat_hash_set<hhds::Class_index> seen;
  std::vector<hhds::Pin_class>           work{din};
  int                                    visited = 0;
  while (!work.empty() && visited++ < 256) {
    auto p = work.back();
    work.pop_back();
    if (p.is_invalid() || is_const_pin(p) || is_graph_input_pin(p) || !seen.insert(p.get_class_index()).second) {
      continue;
    }
    auto n = p.get_master_node();
    if (livehd::graph_util::is_type_register(n) || type_op_of(n) == Ntype_op::Sub) {
      continue;
    }
    if (type_op_of(n) == Ntype_op::Mux) {
      auto selected = decode_bool_condition(drv_at(n, 0));
      auto arm0     = drv_at(n, 1);
      auto arm1     = drv_at(n, 2);
      if (selected.has_value() && same_pin(selected->base, gate.base)) {
        const bool override_on_sel1 = selected->true_when_base == override_when_base;
        auto       override_arm     = override_on_sel1 ? arm1 : arm0;
        auto       normal_arm       = override_on_sel1 ? arm0 : arm1;
        if (!cone_reaches_q(override_arm, q) && cone_reaches_q(normal_arm, q)) {
          return true;
        }
      }
    }
    for (const auto& e : n.inp_edges()) {
      work.push_back(e.driver);
    }
  }
  return false;
}

// Slang represents an async-reset latch's aggregate write-enable as
// `reset ? 1 : data_enable`. Recover the normal data enable only when din has
// the matching priority override. Reset value and priority remain untouched.
[[nodiscard]] std::optional<Bool_condition> latch_data_open_condition(const hhds::Node_class& latch, const hhds::Pin_class& q,
                                                                      const hhds::Pin_class& din, const hhds::Pin_class& enable) {
  auto open = decode_bool_condition(enable);
  if (!open.has_value() || !open->true_when_base || open->base.is_invalid() || is_const_pin(open->base)
      || is_graph_input_pin(open->base)) {
    return open;
  }
  auto mux = open->base.get_master_node();
  if (type_op_of(mux) != Ntype_op::Mux) {
    return open;
  }
  auto sel  = drv_at(mux, 0);
  auto arm0 = drv_at(mux, 1);
  auto arm1 = drv_at(mux, 2);
  auto gate = decode_bool_condition(sel);
  auto v0   = const_truth(arm0);
  auto v1   = const_truth(arm1);
  if (!gate.has_value()) {
    return open;
  }
  const bool arm0_override = v0.has_value() && *v0;
  const bool arm1_override = v1.has_value() && *v1;
  if (arm0_override == arm1_override) {
    return open;
  }
  const bool override_on_sel1   = arm1_override;
  const bool override_when_base = override_on_sel1 ? gate->true_when_base : !gate->true_when_base;
  if (!has_guarded_data_override(din, q, *gate, override_when_base)) {
    return open;
  }

  // If a dedicated reset pin exists, require it to agree too. Older slang
  // latch lowering lacks that pin; the paired data override above is then the
  // sound structural evidence.
  auto reset = livehd::graph_util::get_driver_of_sink_name(latch, "reset_pin");
  if (!reset.is_invalid()) {
    auto reset_active = decode_bool_condition(reset);
    if (!reset_active.has_value()) {
      return open;
    }
    auto negreset = livehd::graph_util::get_driver_of_sink_name(latch, "negreset");
    if (!negreset.is_invalid()) {
      auto negative = const_truth(negreset);
      if (!negative.has_value()) {
        return open;
      }
      if (*negative) {
        reset_active->true_when_base = !reset_active->true_when_base;
      }
    }
    if (!same_pin(gate->base, reset_active->base) || override_when_base != reset_active->true_when_base) {
      return open;
    }
  }
  return decode_bool_condition(override_on_sel1 ? arm0 : arm1);
}

// ---- packed-wire slice fold helpers ---------------------------------------
// A firtool/Chisel bundle-as-UInt writes a wide net as an Or of constant-shifted
// disjoint fields and reads fields back as constant slices. At WORD level the net
// is one node, so write-field-A/read-field-B looks like a combinational cycle even
// though the bit ranges are disjoint. Resolving each constant slice to the one
// operand that drives it removes the false edge (and is a plain win regardless of
// any cycle). See graph/split_selfref.cpp for the cycle-gated, node-CREATING
// rules (Mux/EQ/distribute) this deliberately does NOT duplicate.

[[nodiscard]] hhds::Pin_class drv_at(const hhds::Node_class& n, uint32_t pid) {
  // Cprop only asks for required single-driver ports of a well-formed builtin.
  // Go directly through that sink's fan-in instead of materializing and
  // filtering every input edge of the node for each port lookup.
  auto sink    = n.get_sink_pin(static_cast<hhds::Port_id>(pid));
  auto drivers = sink.get_driver_pins();
  if (drivers.size() == 1) {
    return drivers.front();
  }
  return {};
}

constexpr std::pair<int, int> kFpBail{-1, -1};
constexpr int                 kPackedSliceWalkLimit  = 64;
constexpr int                 kPackedSliceFanInLimit = 64;
// Canonicalizing one flat, disjoint Or/SHL pack is a bounded linear scan, not
// the recursive slice walk guarded above. Chisel-generated state bundles can
// legitimately carry hundreds of lanes (Rob: 520); refusing them leaves pure
// wiring for ABC to bit-blast independently. Keep a generous hard ceiling for
// malformed/adversarial graphs while covering normal generated aggregates.
constexpr int                 kConcatPackFanInLimit  = 4096;

// The CONSTANT shift amount of a SHL, or <0 when not a bounded constant.
[[nodiscard]] int const_shl_amount(const hhds::Node_class& m) {
  auto kd = drv_at(m, 1);
  if (kd.is_invalid() || !is_const_pin(kd)) {
    return -1;
  }
  auto kc = hydrate_const(kd);
  if (kc.has_unknowns() || kc.is_negative() || !kc.is_just_i64()) {
    return -1;
  }
  auto k = kc.to_just_i64();
  return (k < 0 || k > (1 << 28)) ? -1 : static_cast<int>(k);
}

// The half-open [begin,end) bit range of a Get_mask's CONSTANT mask, or kFpBail.
// Rejects the `-1` to-unsigned idiom and noncontiguous/negative masks.
[[nodiscard]] std::pair<int, int> const_mask_range(const hhds::Node_class& m) {
  auto md = drv_at(m, 2);
  if (md.is_invalid() || !is_const_pin(md)) {
    return kFpBail;
  }
  auto mc = hydrate_const(md);
  if (mc.has_unknowns() || !mc.is_positive()) {
    return kFpBail;  // includes mask == -1
  }
  auto [b, e] = mc.get_mask_range();  // {-1,-1} also signals noncontiguous
  if (b < 0 || e <= b) {
    return kFpBail;
  }
  return {b, e};
}

// footprint(p): a sound OVER-approximation [lo,hi) of the bit positions where `p`
// can be nonzero; kFpBail = "cannot bound". Over-approximating only makes the
// disjointness test below HARDER to satisfy (it refuses), so it is the safe
// direction; UNDER-approximating would pick the wrong operand and miscompile.
//
// An unsigned pin's `bits` is its literal significant width.
[[nodiscard]] std::pair<int, int> footprint(const hhds::Pin_class& p, int depth) {
  if (p.is_invalid() || depth > 16) {
    return kFpBail;
  }
  if (is_const_pin(p)) {
    auto c = hydrate_const(p);
    if (c.is_negative()) {
      return kFpBail;
    }
    if (c.has_unknowns()) {
      return {0, c.get_bits()};  // a '?'-const still occupies its declared width
    }
    int fb = c.get_first_bit_set();
    int lb = c.get_last_bit_set();
    if (fb < 0 || lb < 0) {
      return {0, 0};  // known zero contributes no bits
    }
    return {fb, lb + 1};
  }
  if (is_graph_input_pin(p)) {
    if (!livehd::graph_util::is_unsign(p)) {
      return kFpBail;
    }
    const int bits = bits_of(p);
    return bits > 0 ? std::pair<int, int>{0, bits} : kFpBail;
  }
  auto m = p.get_master_node();
  if (m.is_invalid()) {
    return kFpBail;
  }
  auto op = type_op_of(m);
  if (op == Ntype_op::SHL) {
    int k = const_shl_amount(m);
    if (k < 0) {
      return kFpBail;
    }
    auto fx = footprint(drv_at(m, 0), depth + 1);
    if (fx.first < 0) {
      return kFpBail;
    }
    if (fx.second <= fx.first) {
      return {0, 0};
    }
    return {fx.first + k, fx.second + k};
  }
  if (op == Ntype_op::Get_mask) {
    auto r = const_mask_range(m);
    if (r.first >= 0) {
      int w = r.second - r.first;  // extracted bits are packed down to [0,w)
      return w <= 1 ? std::pair<int, int>{0, 1} : std::pair<int, int>{0, w};
    }
    // else fall through to the generic bound
  }
  if (!livehd::graph_util::is_unsign(p)) {
    return kFpBail;  // a signed x makes shl(x,k) set every bit above k
  }
  int b = bits_of(p);
  if (b == 0) {
    return kFpBail;  // unsized (Nconst/Sub/Memory/IO stay exempt) -> [k,k) would
                     // read as EMPTY and fold a live value to constant 0
  }
  return {0, b};
}

// The width n of a low-contiguous mask 2^n-1 (n>=1), or -1 for anything else
// (negative, unknown-bit, zero, or a run not anchored at bit 0).
[[nodiscard]] int low_mask_width(const Dlop& mask) {
  if (mask.is_negative() || mask.has_unknowns()) {
    return -1;
  }
  auto [mb, me] = mask.get_mask_range();  // {-1,-1} = noncontiguous
  if (mb != 0 || me <= 0) {
    return -1;
  }
  return me;
}

// "Is `op` a COMPUTED combinational cell (as opposed to an IO/state/Sub/const/
// attr boundary)?"
//
// TRAP: cprop spells this question six times as the ORDERED test
// `op <= Ntype_op::Hotmux` (38), because 39..56 used to be exactly the
// boundary band. That reading is no longer true of the enum: Concat (58) is a
// plain combinational op that sits ABOVE the band only because 58 was the last
// free EVEN slot when it was added (cell.hpp -- even is forced, bit 0 is
// is_loop_last, and 40..48 are reserved as the opposite-polarity twins of
// Memory/Flop/Latch/Fflop/Sub). Left as a bare comparison, every one of those
// six sites SILENTLY classified a concat as state: it was excluded from CSE, from
// the copy-prop/fold sweep, and it was handed the declared-width door that only
// boundary pins may use.
//
// Ordinal-identical to the old test for every other op, so no other cell's
// classification moves. Rem (54) is combinational too but stays outside on
// purpose: each of these sites has its own measured reason to exclude it (see
// is_bool01 below and canonicalize_and_masks).
[[nodiscard]] constexpr bool is_computed_comb_op(Ntype_op op) { return op <= Ntype_op::Hotmux || op == Ntype_op::Concat; }

// TRUE only when the realization hint proves the value is in {0,1}. With one
// literal-width contract there is no producer-specific exception: u1 means
// exactly [0,1]. Constants carry their proof in the value itself.
[[nodiscard]] bool is_bool01(const hhds::Pin_class& p) {
  if (p.is_invalid()) {
    return false;
  }
  auto const01 = [](const hhds::Pin_class& c_pin) -> bool {
    if (c_pin.is_invalid() || !is_const_pin(c_pin)) {
      return false;
    }
    auto c = hydrate_const(c_pin);
    if (c.has_unknowns()) {
      return false;
    }
    return c.is_known_zero() || (c.is_just_i64() && c.to_just_i64() == 1);
  };
  if (is_const_pin(p)) {
    return const01(p);
  }
  return livehd::graph_util::is_unsign(p) && bits_of(p) == 1;
}

// Decode a 2-input EQ against a specific constant: returns the non-const
// operand iff the node has exactly two input edges and exactly one of them is
// the constant `against`. EQ's `as` port is multi-driver, so scan edges.
[[nodiscard]] hhds::Pin_class eq_against_const(const hhds::Node_class& n, int64_t against) {
  if (type_op_of(n) != Ntype_op::EQ) {
    return {};
  }
  hhds::Pin_class value;
  int             matches = 0;
  int             total   = 0;
  for (const auto& e : n.inp_edges()) {
    ++total;
    if (total > 2) {
      return {};
    }
    if (is_const_pin(e.driver)) {
      auto c = hydrate_const(e.driver);
      if (c.is_just_i64() && !c.has_unknowns() && c.to_just_i64() == against) {
        ++matches;
        continue;
      }
    }
    value = e.driver;
  }
  if (total != 2 || matches != 1 || value.is_invalid()) {
    return {};
  }
  return value;
}

// ---- hand-spelled concat -> one Ntype_op::Concat ---------------------------
//
// Two idioms SPELL a bit concatenation out of general-purpose cells, and both
// are the dominant shape in real designs: a Set_mask chain that writes one
// field at a time over a zero base, and an Or of constant-shifted disjoint
// fields. Measured on minion (prop_slow.md 3.1): 1,943 Set_mask chain heads
// (405 of them pure ascending 1-bit lanes over a zero base, ~30 nodes each --
// a Verilog concat built one node per BIT) and 1,052 Or-of-disjoint-SHL pack
// trees. Both arms are ON -- see kOrPackEnabled at the bottom of this block for
// what the Or arm's flip depended on.
//
// Both rewrites are node-NON-INCREASING: the pack HEAD is retyped in place and
// the interior cells are swept, so nothing new is created except the per-lane
// width constants, which are leaves on the shared const node.
//
// Why here and not in each backend: cprop's own scalar_get_mask_packed,
// graph/split_selfref, sim_color_plan's slice matcher and the lec/semdiff
// encoders each re-derive lane structure FROM THE SPELLING, with three
// different sets of bailouts (this file's is the "straddles the lane boundary"
// break below). A Concat hands every one of them the lane table instead.
//
// The value-preservation argument, and the guard that carries each half:
//   * every window is a CONSTANT CONTIGUOUS bit range (const_mask_range /
//     const_shl_amount refuse otherwise);
//   * the windows are pairwise DISJOINT, so overwrite order (Set_mask) and
//     bitwise-or order are both irrelevant and the assembly is a plain sum;
//   * every bit OUTSIDE the windows is provably zero (a known-zero Set_mask
//     base, or a bounded operand width), so the gaps are spelled as
//     constant-zero lanes and the result is exactly sum(w_i) bits wide;
//   * the head driver is UNSIGNED. A Concat is non-negative by cell contract;
//     had the head been stamped signed, its consumers were reading the top
//     window bit as a SIGN, and handing them an unsigned value instead changes
//     it (u4 0b1010 reads 10, not -6).
// A lane whose value pin is signed or wider than its window is fine and needs
// no guard: Set_mask, Or-of-SHL and Concat all keep exactly `value mod 2^w`.

struct Pack_lane {
  hhds::Pin_class value;
  int             lo{0};
  int             hi{0};  // half-open
};

// The measured worst case is a full-word pack of 47-64 single-bit writes;
// these are headroom, not tuning knobs.
constexpr int kPackChainLimit = 256;
constexpr int kPackMaxLanes   = 1024;
constexpr int kPackMaxWidth   = 1 << 20;

// Delete `n` and, transitively, every input whose last consumer it was. Same
// job as Cprop::bwd_del_node, re-spelled here because the canonicalizations
// below are free functions and the class members are not reachable from this
// namespace.
void sweep_dead_node(const hhds::Node_class& n) {
  std::deque<hhds::Node_class>           work;
  // ENQUEUED-ONCE, like Cprop::bwd_del_node's `potential_set`. Without it a node
  // that drives the swept one through TWO edges (`x*x`, a value written into two
  // lanes) is queued twice: the first pop deletes it and the second pops a stale
  // handle straight into has_out_edges()/del_node().
  absl::flat_hash_set<hhds::Class_index> queued;
  work.push_back(n);
  queued.insert(n.get_class_index());
  for (int budget = 4 * kPackMaxLanes; !work.empty() && budget > 0; --budget) {
    auto cur = work.front();
    work.pop_front();
    if (cur.is_invalid() || Ntype::is_loop_last(type_op_of(cur)) || livehd::graph_util::is_builtin_node(cur)
        || cur.has_out_edges()) {
      continue;
    }
    for (const auto& e : cur.inp_edges()) {
      if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
        continue;
      }
      auto m = e.driver.get_master_node();
      if (!livehd::graph_util::is_builtin_node(m) && queued.insert(m.get_class_index()).second) {
        work.push_back(m);
      }
    }
    cur.del_node();
  }
}

// Sort the collected windows by position, PROVE they are pairwise disjoint, and
// fill every hole (including the one below the lowest window) with a
// constant-zero lane, so the list tiles [0,W) exactly -- which is the only
// layout a Concat can encode. `lanes` is left LSB-first.
//
// Refuses on overlap rather than picking a winner: for a Set_mask chain the
// head-most write would win and for an Or the bits would merge, so the two
// spellings do not even agree on what an overlap MEANS.
[[nodiscard]] bool tile_pack_lanes(hhds::Graph& g, std::vector<Pack_lane>& lanes) {
  if (lanes.empty() || lanes.size() > kPackMaxLanes) {
    return false;
  }
  std::sort(lanes.begin(), lanes.end(), [](const Pack_lane& a, const Pack_lane& b) { return a.lo < b.lo; });

  std::vector<Pack_lane> tiled;
  tiled.reserve(2 * lanes.size() + 1);
  int pos = 0;
  for (const auto& l : lanes) {
    if (l.hi <= l.lo || l.lo < pos) {
      return false;  // empty or overlapping window
    }
    if (l.lo > pos) {
      tiled.push_back(Pack_lane{create_const(g, *Dlop::create_integer(0)), pos, l.lo});
    }
    tiled.push_back(l);
    pos = l.hi;
  }
  if (pos <= 0 || pos > kPackMaxWidth || tiled.size() > kPackMaxLanes) {
    return false;
  }
  lanes = std::move(tiled);
  return true;
}

// Retype the pack head into the Concat its lane table spells. `tiled` arrives
// LSB-first (as tile_pack_lanes leaves it) and the cell is MSB-first, hence the
// reverse indexing.
void emit_concat(hhds::Graph& g, hhds::Node_class& node, const std::vector<Pack_lane>& tiled) {
  auto edges = node.inp_edges();  // snapshot: del_edge invalidates the lazy view
  for (auto e : edges) {
    e.del_edge();
  }
  livehd::graph_util::set_type_op(node, Ntype_op::Concat);

  int32_t total = 0;
  for (size_t i = 0; i < tiled.size(); ++i) {
    const auto&   l = tiled[tiled.size() - 1 - i];
    const int32_t w = l.hi - l.lo;
    node.create_sink_pin(static_cast<hhds::Port_id>(2 * i)).connect_driver(l.value);
    node.create_sink_pin(static_cast<hhds::Port_id>(2 * i + 1)).connect_driver(create_const(g, *Dlop::create_integer(w)));
    total += w;
  }

  // This is intrinsic construction metadata, not graph-wide width repair: a
  // Concat's literal carrier is exactly sum(w_i). Written here rather than left
  // to pass/bitwidth because the DEFAULT recipe (O1) is cprop with no bitwidth
  // after it (see recipe_graph_passes), so this stamp is the one that ships.
  // Narrowing a head that was stamped wider is sound in the same breath: the
  // tiling proves the value is below 2^total.
  auto out = node.create_driver_pin(0);
  livehd::graph_util::set_ubits(out, total);
}

// A Set_mask chain over a KNOWN-ZERO base: `set_mask(set_mask(0,m0,v0),m1,v1)…`
bool canonicalize_set_mask_pack(hhds::Graph& g, hhds::Node_class& node) {
  auto head_out = node.get_driver_pin(0);
  if (head_out.is_invalid() || !livehd::graph_util::is_unsign(head_out)) {
    return false;
  }

  std::vector<Pack_lane>                 lanes;
  std::vector<hhds::Node_class>          chain;  // head first
  absl::flat_hash_set<hhds::Class_index> in_chain;
  auto                                   cur        = node;
  bool                                   zero_based = false;
  while (true) {
    if (chain.size() >= kPackChainLimit) {
      return false;
    }
    if (!in_chain.insert(cur.get_class_index()).second) {
      return false;  // cycle through the `a` pins (structurally representable)
    }
    auto a_pin = drv_at(cur, 0);
    auto v_pin = drv_at(cur, 4);
    auto r     = const_mask_range(cur);  // rejects non-const, -1 and noncontiguous
    if (a_pin.is_invalid() || v_pin.is_invalid() || r.first < 0) {
      return false;
    }
    lanes.push_back(Pack_lane{v_pin, r.first, r.second});
    chain.push_back(cur);

    if (is_const_pin(a_pin)) {
      // Only a KNOWN-ZERO base makes the bits outside the lanes provably zero.
      // A nonzero constant base would need residual lanes carved out of it, and
      // a NEGATIVE one has infinitely many set bits above the top lane, which
      // no finite concat can spell.
      zero_based = hydrate_const(a_pin).is_known_zero();
      break;
    }
    auto am = a_pin.get_master_node();
    if (am.is_invalid() || type_op_of(am) != Ntype_op::Set_mask) {
      return false;
    }
    // Interior links must be PRIVATE to this chain: the rewrite drops every
    // intermediate word, so another reader of a partial version would lose its
    // value (and the rewrite would stop being node-non-increasing).
    if (!has_single_consumer(a_pin)) {
      return false;
    }
    cur = am;
  }
  if (!zero_based) {
    return false;
  }
  // A lane fed from inside the chain is a self-reference; rewiring it onto the
  // retyped head would close the loop onto the node being rewritten.
  for (const auto& l : lanes) {
    if (in_chain.contains(l.value.get_master_node().get_class_index())) {
      return false;
    }
  }
  if (!tile_pack_lanes(g, lanes)) {
    return false;
  }

  emit_concat(g, node, lanes);
  for (size_t i = 1; i < chain.size(); ++i) {  // 0 is the head, now the Concat
    sweep_dead_node(chain[i]);
  }
  return true;
}

// An Or whose operands occupy DISJOINT constant bit windows: the classic
// `(a<<8) | (b<<4) | c` pack tree. An operand whose bit span cannot be pinned
// down refuses the whole rewrite.
//
// An UPPER bound on the low bits an operand can occupy, or -1 when there is
// none. Deliberately NOT footprint(): footprint exists to prove DISJOINTNESS,
// where an over-approximation merely refuses. Under the unified hint contract,
// every stamped unsigned pin carries its literal finite width.
//
// Over-stating is safe in both roles -- the lane just covers provably-zero high
// bits and the mask stays an identity -- so every uncertain case rounds UP.
[[nodiscard]] int pack_lane_width(const hhds::Pin_class& p) {
  if (p.is_invalid()) {
    return -1;
  }
  if (is_const_pin(p)) {
    auto c = hydrate_const(p);
    if (c.is_negative()) {
      return -1;
    }
    if (c.has_unknowns()) {
      return c.get_bits();  // a '?'-const still occupies its declared width
    }
    const int lb = c.get_last_bit_set();
    return lb < 0 ? 0 : lb + 1;  // 0 == known zero: contributes nothing
  }
  // A Concat answers EXACTLY from its lane table, so it must be asked before
  // either stamp gate below. Both of those read the pin's `bits`/`sign`, and an
  // unstamped Concat -- precisely what pass.bitfuzz produces, since collect()
  // strips the attrs off every non-const, non-state driver pin -- would take the
  // `bits <= 0` refusal even though the cell contract answers with no stamp at
  // all. (A stamped Concat is always `unsign`, so the sign gate never bit here.)
  if (!is_graph_input_pin(p) && !is_graph_output_pin(p)) {
    if (auto cm = p.get_master_node(); !cm.is_invalid() && type_op_of(cm) == Ntype_op::Concat) {
      const int total = livehd::graph_util::concat_total_width(cm);
      if (total > 0) {
        return total;  // exact, by cell contract
      }
      return -1;  // malformed lane table: refuse, never guess from the stamp
    }
  }
  if (!livehd::graph_util::is_unsign(p)) {
    return -1;  // a signed value's sign extension has no finite top bit
  }
  const int bits = bits_of(p);
  if (bits <= 0) {
    return -1;  // unsized: [0,0) would read as EMPTY and delete a live value
  }
  return bits;
}

bool canonicalize_or_pack(hhds::Graph& g, hhds::Node_class& node) {
  auto out = node.get_driver_pin(0);
  if (out.is_invalid() || !livehd::graph_util::is_unsign(out)) {
    return false;
  }

  std::vector<Pack_lane>        lanes;
  std::vector<hhds::Node_class> shifts;
  int                           fan_in = 0;
  for (const auto& e : node.inp_edges()) {
    if (++fan_in > kConcatPackFanInLimit) {
      return false;
    }
    if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
      return false;  // Or is n-ary on ONE sink; anything else is not this shape
    }
    auto value = e.driver;
    int  shift = 0;
    auto m     = value.get_master_node();
    if (!m.is_invalid() && type_op_of(m) == Ntype_op::SHL) {
      const int k = const_shl_amount(m);
      auto      x = drv_at(m, 0);
      if (k < 0 || x.is_invalid()) {
        return false;  // runtime shift amount: no constant window
      }
      value = x;
      shift = k;
      shifts.push_back(m);
    }
    // The window is [shift, shift + w): its LOW end starts at `shift` even when
    // the operand has no low bits of its own, because that is where the concat
    // lane has to start.
    const int w = pack_lane_width(value);
    if (w < 0) {
      return false;  // unbounded / signed operand: no provable window
    }
    if (w == 0) {
      continue;  // provably zero: contributes nothing to an Or
    }
    lanes.push_back(Pack_lane{value, shift, shift + w});
  }
  if (lanes.size() < 2) {
    return false;  // a 1-operand Or is a forward, handled by the scalar sweep
  }
  if (!tile_pack_lanes(g, lanes)) {
    return false;
  }

  emit_concat(g, node, lanes);
  for (auto& s : shifts) {
    sweep_dead_node(s);
  }
  return true;
}

// The Or arm is ON. It was never a correctness doubt -- `(a<<8)|(b<<4)|c ->
// Concat` was LEC-proven on the fixtures (including the u5-instance-output lane
// bug pack_lane_width now exists for) -- it was a CONSUMER gap, and one test
// named it exactly: //inou/prp:prp-sim-packed_bus_bit_ring, whose two modules
// exchange a packed bus each way and are only schedulable because
// inou/cgen/sim_color_plan.cpp proves that the bits of one bus depend on
// disjoint bits of the other and splits the def into slices. That matcher (and
// graph/split_selfref's false-loop breaker) read the Or/SHL/Get_mask SPELLING
// and had no Concat arm, so turning this on used to make `lhd sim` refuse the
// module outright ("fine-color dependency cycle remains after state and
// compact-loop carry cuts"). Both now decode concat_lanes() directly, which is
// what unblocked the flip; if either arm is ever removed, this goes back to
// false rather than growing a cprop-local guard -- inside `deva` the pack is
// plainly acyclic, and the ring exists only in the PARENT, at Sub port level.
//
// The Set_mask arm below never had that consumer and is on by measurement; it
// is also the larger population (1,943 chain heads vs 1,052 Or trees).
constexpr bool kOrPackEnabled = true;

// Is `node` a candidate pack head/link? Collected during the fused sweep and
// rewritten afterwards, CONSUMER-side first (see canonicalize_concat_pack).
[[nodiscard]] bool is_concat_pack_candidate(const hhds::Node_class& node) {
  const auto op = type_op_of(node);
  return op == Ntype_op::Set_mask || (kOrPackEnabled && op == Ntype_op::Or);
}

// Rewrite ONE pack candidate. Callers must visit candidates in reverse
// topological (consumer-side first) order: the outermost Set_mask of a write
// chain is the one that must become the Concat. Converting an inner link first
// would leave the outer walk staring at a Concat where it needs a constant-zero
// base, and the chain would only fragment.
//
// Do NOT try to pre-classify a link as "interior" by looking for a same-op
// consumer: a Set_mask feeding another Set_mask's `value` port (pid 4), or an
// Or pack feeding an unrelated non-pack Or, is a real head whose consumer never
// absorbs it -- skipping those left the pack un-canonicalized forever.
void canonicalize_concat_pack(hhds::Graph* g, hhds::Node_class& node) {
  if (node.is_invalid() || !node.has_out_edges()) {
    return;
  }
  const auto op = type_op_of(node);
  if (op == Ntype_op::Set_mask) {
    canonicalize_set_mask_pack(*g, node);
  } else if (kOrPackEnabled && op == Ntype_op::Or) {
    canonicalize_or_pack(*g, node);
  }
}

}  // namespace

void Cprop::collapse_forward_same_op(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);

  // out_edges() is a lazy view; rewriting an edge (connect_driver/del_edge)
  // invalidates an in-flight iterator. Rather than snapshot the (possibly huge)
  // fan-out into a vector, re-scan from the front after each rewrite: find the
  // next same-op consumer, splice this node's inputs into it, drop the edge, and
  // restart. Edges to a different op (or mismatched port) are left in place;
  // once none remain the node is fully collapsed and can be deleted.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const auto& out : node.out_edges()) {
      if (type_op_of(out.sink.get_master_node()) != op) {
        continue;
      }
      if (out.driver.get_port_id() != out.sink.get_port_id()) {
        continue;
      }
      // Parallel-edge refusal (same hazard collapse_forward_always_pin0 and
      // collapse_forward_for_pin guard against): splicing an operand into a
      // consumer that ALREADY reads it needs a second parallel edge, which
      // hhds overflow-mode sink storage (a set) silently DEDUPS -- Mult would
      // square the shared operand away (Mult(Mult(a,b),a) = a*a*b collapsing
      // to a*b). And/Or are idempotent so the dedup is value-neutral there,
      // and Xor is handled by the explicit parity cancel below.
      if (op != Ntype_op::And && op != Ntype_op::Or && op != Ntype_op::Xor) {
        bool shares_operand = false;
        for (auto& inp : inp_edges_ordered) {
          if (is_driver_connected_to_sink(inp.driver, out.sink)) {
            shares_operand = true;
            break;
          }
        }
        if (shares_operand) {
          continue;  // leave this consumer's edge in place
        }
      }

      for (auto& inp : inp_edges_ordered) {
        if (op == Ntype_op::Xor) {
          if (is_driver_connected_to_sink(inp.driver, out.sink)) {
            out.sink.del_sink(inp.driver);
          } else {
            out.sink.connect_driver(inp.driver);
          }
        } else if (op == Ntype_op::Or || op == Ntype_op::And) {
          out.sink.connect_driver(inp.driver);
        } else {
          I(op != Ntype_op::Sum);
          out.sink.connect_driver(inp.driver);
        }
      }

      out.del_edge();
      progressed = true;
      break;  // iterator invalidated by the rewrite; restart the scan
    }
  }
  if (!node.has_out_edges()) {  // every consumer collapsed -> node is dead
    node.del_node();
  }
}

void Cprop::collapse_forward_sum(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  if (inp_edges_ordered.size() > 32) {
    return;
  }

  I(type_op_of(node) == Ntype_op::Sum);
  // Restart-scan the lazy out_edges view after each rewrite (no fan-out copy):
  // splice into the next Sum consumer, drop its edge, restart. Non-Sum consumers
  // stay; once all Sum edges are gone the node is fully merged forward.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (const auto& out : node.out_edges()) {
      auto next_sum_node = out.sink.get_master_node();
      if (type_op_of(next_sum_node) != Ntype_op::Sum) {
        continue;
      }
      // Parallel-edge refusal: a Sum operand spliced into a consumer that
      // already reads it needs a second parallel edge, and hhds overflow-mode
      // sink storage (a set) silently DEDUPS it -- Sum(Sum(a,b),a) = 2a+b
      // would collapse to a+b. Both `as` and `bs` are candidate destinations,
      // so screen both.
      {
        auto as_sink        = find_sink_pin(next_sum_node, "as");
        auto bs_sink        = find_sink_pin(next_sum_node, "bs");
        bool shares_operand = false;
        for (auto& inp : inp_edges_ordered) {
          if (is_driver_connected_to_sink(inp.driver, as_sink) || is_driver_connected_to_sink(inp.driver, bs_sink)) {
            shares_operand = true;
            break;
          }
        }
        if (shares_operand) {
          continue;  // leave this consumer's edge in place
        }
      }

      for (auto& inp : inp_edges_ordered) {
        // Sum(A,Sum(B,C))  = Sum(A+C,B)
        // Sum(Sum(A,B),C)) = Sum(A+C,B)
        if (inp.sink.get_port_id() == 0 && out.sink.get_port_id() == 0) {
          out.sink.connect_driver(inp.driver);
        } else if (inp.sink.get_port_id() == 0 && out.sink.get_port_id() == 1) {
          out.sink.connect_driver(inp.driver);
        } else if (inp.sink.get_port_id() == 1 && out.sink.get_port_id() == 0) {
          setup_sink_by_name(next_sum_node, "bs").connect_driver(inp.driver);
        } else {
          setup_sink_by_name(next_sum_node, "as").connect_driver(inp.driver);
        }
      }
      out.del_edge();
      progressed = true;
      break;  // iterator invalidated by the rewrite; restart the scan
    }
  }

  if (!node.has_out_edges()) {  // every Sum consumer merged forward -> node dead
    bwd_del_node(node);
  }
}

void Cprop::collapse_forward_always_pin0(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);

  for (const auto& out : node.out_edges()) {  // read-only width check
    auto out_bits = bits_of(out.driver);
    for (auto& inp : inp_edges_ordered) {
      auto inp_bits = bits_of(inp.driver);
      if (out_bits > 0 && inp_bits > 0 && out_bits != inp_bits) {
        return;
      }
    }
  }

  // Parallel-edge pre-scan: forwarding an operand into a consumer that
  // ALREADY reads that operand needs a second parallel edge, which hhds
  // overflow-mode sink storage (a set) silently dedups -- the consumer would
  // drop one operand (Sum halves, Mult squares away). And/Or consumers are
  // idempotent so the dedup is value-neutral, and Xor-into-Xor is handled by
  // the explicit parity cancel below. Anything else: leave the node alone.
  for (const auto& out : node.out_edges()) {
    const auto consumer_op = type_op_of(out.sink.get_master_node());
    if (consumer_op == Ntype_op::And || consumer_op == Ntype_op::Or || (op == Ntype_op::Xor && consumer_op == Ntype_op::Xor)) {
      continue;
    }
    for (auto& inp : inp_edges_ordered) {
      if (is_driver_connected_to_sink(inp.driver, out.sink)) {
        return;
      }
    }
  }

  // Splice this node's inputs onto every consumer sink, then bwd_del_node
  // deletes the node in one shot (del_node bulk-drops its edges — no per-edge
  // del_edge lookup). Iterating the live out_edges while connecting is safe:
  // connect_driver/del_sink only touch the input/consumer pins, never node's
  // out-edge storage, and no node/pin is created to realloc the tables.
  for (const auto& out : node.out_edges()) {
    for (auto& inp : inp_edges_ordered) {
      if (op == Ntype_op::Xor && type_op_of(out.sink.get_master_node()) == Ntype_op::Xor) {
        // Parity cancel is only correct INTO another Xor: a shared operand
        // pair drops out of that consumer's reduction. For every other
        // consumer the pre-scan above guarantees a plain reconnect suffices.
        if (is_driver_connected_to_sink(inp.driver, out.sink)) {
          out.sink.del_sink(inp.driver);
        } else {
          out.sink.connect_driver(inp.driver);
        }
      } else {
        out.sink.connect_driver(inp.driver);
      }
    }
  }

  bwd_del_node(node);
}

// Redirect every consumer of `node` to `new_dpin` and delete `node`. Returns
// FALSE without touching the graph when a consumer's width disagrees with
// new_dpin's — a blind reconnect would change the value that consumer reads. The
// bool is not cosmetic: a caller that fabricated `new_dpin`'s node before calling
// (rather than passing an existing operand) MUST check it and clean up the orphan
// on false, or it leaks. Existing callers pass an existing pin, so a false there
// is just a missed collapse (the node is left for later folds).
bool Cprop::collapse_forward_for_pin(hhds::Node_class& node, hhds::Pin_class new_dpin) {
  auto new_bits = bits_of(new_dpin);
  for (const auto& out : node.out_edges()) {  // read-only width check
    // Parallel-edge refusal: if new_dpin ALREADY drives this consumer sink,
    // the reconnect needs a second parallel edge -- and hhds overflow-mode
    // sink storage is a set that silently DEDUPS it, so the consumer would
    // lose one operand (Sum(a,a')=2a halves to a; Xor parity flips). And/Or
    // consumers are idempotent, so a dropped duplicate is value-neutral
    // there; refuse everywhere else.
    const auto consumer_op = type_op_of(out.sink.get_master_node());
    if (consumer_op != Ntype_op::And && consumer_op != Ntype_op::Or) {
      for (const auto& d : out.sink.get_driver_pins()) {
        if (d == new_dpin) {
          return false;
        }
      }
    }
    auto       out_bits = bits_of(out.driver);
    // LGraph values are unlimited-precision integers; `bits` is materialization
    // metadata. Replacing a wider unsigned identity wrapper with its narrower
    // non-negative source loses no value bits—the source's range is already
    // exact. Still reject every narrowing of the source range and every signed
    // mismatch, where the removed node may have supplied a sign boundary.
    //
    // is_unsign() is attribute ABSENCE, so this rule is only as sound as the
    // frontends' promise to stamp every SIGNED driver pin: an unstamped signed
    // pin reads as a non-negative range here and its wrapper is dropped. That
    // promise is the IR contract (`unsign` == value-range guarantee), not an
    // assumption to be re-derived per pass — fix a violation at its producer
    // (see lgyosys_tolg's instance-output stamping) rather than by weakening it.
    const bool lossless_unsigned_widen
        = livehd::graph_util::is_unsign(new_dpin) && new_bits > 0 && out_bits > 0 && new_bits <= out_bits;
    if (out_bits > 0 && new_bits > 0 && out_bits != new_bits && !lossless_unsigned_widen) {
      return false;
    }
  }

  // Redirect every consumer to new_dpin, then bwd_del_node deletes the node in
  // one shot (del_node bulk-drops its edges — no per-edge find). connect_sink
  // only grows new_dpin/sink storage, so the live walk stays valid.
  for (const auto& out : node.out_edges()) {
    new_dpin.connect_sink(out.sink);
  }

  bwd_del_node(node);
  return true;
}

bool Cprop::try_constant_prop(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  int n_inputs_constant = 0;
  int n_inputs          = 0;
  for (auto& e : inp_edges_ordered) {
    n_inputs++;
    if (!is_const_pin(e.driver)) {
      continue;
    }
    n_inputs_constant++;
  }

  if (n_inputs == n_inputs_constant && n_inputs) {
    replace_all_inputs_const(node, inp_edges_ordered);
    return true;
  } else if (n_inputs && n_inputs_constant >= 1) {
    replace_part_inputs_const(node, inp_edges_ordered);
    return true;
  }

  return false;
}

void Cprop::try_collapse_forward(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);

  if (inp_edges_ordered.size() == 1) {
    auto       prev_op      = type_op_of(inp_edges_ordered[0].driver.get_master_node());
    // A single-input Sum whose lone driver is on the SUBTRACT (b) pin is a
    // negation (0 - x = -x). collapse_forward_always_pin0 forwards the driver
    // UNCHANGED, which would drop the sign (turning -x into +x). Leave the Sum
    // node so cgen renders it as -(x).
    const bool sum_subtract = op == Ntype_op::Sum && sink_pin_name(inp_edges_ordered[0].sink) == "bs";
    if ((op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::Div || op == Ntype_op::And || op == Ntype_op::Or
         || op == Ntype_op::Xor)
        && !sum_subtract) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
      return;
    }
    if (prev_op == Ntype_op::Get_mask) {
      if (op == Ntype_op::Get_mask) {
        collapse_forward_always_pin0(node, inp_edges_ordered);
        return;
      }
    }
  }

  if (op == Ntype_op::Sum) {
    collapse_forward_sum(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mult || op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    collapse_forward_same_op(node, inp_edges_ordered);
  } else if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
    // All data arms identical -> the selector is irrelevant (a Hotmux's
    // zero/multi-hot error case stays a runtime property; cprop assumes the
    // unique-if assume holds, same as folding a Mux assumes an in-range sel).
    if (inp_edges_ordered.size() <= 1) {
      node.del_node();
      return;
    }
    auto& a_pin = inp_edges_ordered[1].driver;
    for (auto i = 2u; i < inp_edges_ordered.size(); ++i) {
      if (a_pin != inp_edges_ordered[i].driver) {
        return;
      }
    }
    collapse_forward_for_pin(node, a_pin);
  }
}

void Cprop::replace_part_inputs_const(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::Mux) {
    auto& s_pin = inp_edges_ordered[0].driver;
    if (!is_const_pin(s_pin)) {
      return;
    }
    auto s_const = hydrate_const(s_pin);
    if (s_const.has_unknowns()) {
      return;
    }

    I(s_const.is_just_i64());
    size_t sel = s_const.to_just_i64();

    hhds::Pin_class a_pin;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == 0) {
        continue;
      }
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        a_pin = e.driver;
        break;
      }
    }

    if (a_pin.is_invalid()) {
#ifndef NDEBUG
      Pass::info("WARNING: mux selector:{} for a disconnected pin in mux. Using zero\n", sel);
#endif
      a_pin = create_const(*current_graph, *Dlop::create_integer(0));
    }

    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::Hotmux) {
    // Constant one-hot selector: collapse to the selected arm (bit i ->
    // p(i+1)). A zero/multi-hot constant violates the unique-if assume; warn
    // and keep the cell (cgen's case default models the runtime error).
    auto& s_pin = inp_edges_ordered[0].driver;
    if (!is_const_pin(s_pin)) {
      return;
    }
    auto s_const = hydrate_const(s_pin);
    if (s_const.has_unknowns() || !s_const.is_just_i64()) {
      return;
    }
    auto sel = s_const.to_just_i64();
    if (sel <= 0 || (sel & (sel - 1)) != 0) {
      Pass::info("WARNING: hotmux selector:{} is not one-hot (unique-if assume violated); not folding\n", sel);
      return;
    }
    size_t arm = 0;  // bit position of the hot bit
    while ((sel >> arm) != 1) {
      ++arm;
    }

    hhds::Pin_class a_pin;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(arm + 1)) {
        a_pin = e.driver;
        break;
      }
    }
    if (a_pin.is_invalid()) {
#ifndef NDEBUG
      Pass::info("WARNING: hotmux selector:{} for a disconnected pin in hotmux. Using zero\n", sel);
#endif
      a_pin = create_const(*current_graph, *Dlop::create_integer(0));
    }
    collapse_forward_for_pin(node, a_pin);
  } else if (op == Ntype_op::EQ) {
    // FIXME: 1- eq(X,0) = not(ror(x))
  } else if (op == Ntype_op::Sum || op == Ntype_op::Or || op == Ntype_op::And || op == Ntype_op::Xor) {
    hhds::Edge_class first_const_edge;
    int              nconstants = 0;
    int              npending   = 0;

    // Seed the accumulator with the op's identity (0 for Sum/Or, -1 for And).
    // A default Dlop is Invalid, which used to act as the additive identity but
    // now propagates to nil (hlop's non-numeric guard) — poisoning the fold.
    Dlop result;
    result = Dlop::create_integer(op == Ntype_op::And ? -1 : 0);

    livehd::graph_util::Edge_vec edge_it2;
    for (auto& i : inp_edges_ordered) {
      if (!is_const_pin(i.driver)) {
        if (npending == 0) {
          edge_it2.push_back(i);
        }
        npending++;
        continue;
      }

      auto c = hydrate_const(i.driver);

      ++nconstants;

      if (op == Ntype_op::Sum) {
        if (sink_pin_name(i.sink) == "as") {
          result = result.add_op(c);
        } else {
          I(sink_pin_name(i.sink) == "bs");
          result = result.sub_op(c);
        }
      } else if (op == Ntype_op::Or) {
        result = result.or_op(c);
      } else if (op == Ntype_op::Xor) {
        result = result.xor_op(c);
      } else {
        I(op == Ntype_op::And);
        result = result.and_op(c);
      }

      if (nconstants == 1) {
        first_const_edge = i;
      } else {
        i.del_edge();
      }
    }

    if (op == Ntype_op::And && result.is_known_zero()) {
      // `x & 0` is 0 for EVERY x: a zero fold is And's ANNIHILATOR, not its
      // identity. Sharing the "zero result => drop the constants and forward
      // the remaining operand" path below (which is right for Sum's 0 and Or's
      // 0) silently deleted the `& 0` and returned x — `(~&sb) & (~|3'b101)`
      // emitted the bare comparison where the answer is a constant 0. A SINGLE
      // constant 0 annihilates just the same (it used to fall through every
      // branch here and survive to codegen). replace_node, not collapse: it
      // width-adjusts the constant per consumer instead of refusing.
      replace_node(node, result);
    } else if (op == Ntype_op::Or && result.is_just_i64() && result.to_just_i64() == -1) {
      // Or's annihilator: x | -1 is the all-ones -1 for EVERY x.
      replace_node(node, result);
    } else if (nconstants > 1) {
      first_const_edge.del_edge();
      if (!result.is_known_zero()) {
        if (op == Ntype_op::Sum && !result.is_positive()) {
          // `result` is the SIGNED fold (add_op for `as`, sub_op for `bs`), so a
          // negative result is already "minus that much". The `bs` sink negates
          // whatever it is given, so it must receive the MAGNITUDE — handing it
          // the negative value negated it a second time and flipped the sign of
          // the whole constant term: `0 - ua - 5` folded to result=-5, went to
          // `bs`, and the node computed `-ua + 5`.
          Dlop zero;
          zero = Dlop::create_integer(0);
          Dlop mag;
          mag = zero.sub_op(result);
          setup_sink_by_name(node, "bs").connect_driver(create_const(*current_graph, mag));
        } else {
          // Or/And/Xor have no subtract sink, so their fold always joins `as`.
          setup_sink_by_name(node, "as").connect_driver(create_const(*current_graph, result));
        }
      } else if (npending == 1 && !(op == Ntype_op::Sum && sink_pin_name(edge_it2[0].sink) == "bs")) {
        // Same guard as the nconstants==0 case below: a lone pending operand on a
        // Sum's subtract (b) pin is `0 - x` = -x, so forwarding x unchanged would
        // drop the sign. Leave the Sum node (it now holds only the b driver).
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (nconstants == 1 && npending >= 1
               && ((op == Ntype_op::And && result.is_just_i64() && result.to_just_i64() == -1)
                   || ((op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Sum) && result.is_known_zero()))) {
      // Identity element: and(x.., -1) == and(x..), or(x.., 0) == or(x..),
      // xor(x.., 0) == xor(x..), sum(x.., +0) == sum(x..). Dropping it matters
      // for codegen too: cgen renders -1 as `1'sh1`, which only sign-extends in
      // an all-signed Verilog expression — in a mixed/unsigned context it reads
      // as +1 and masks everything away. (Sum's lone `+0` used to fall through
      // every branch here — the nconstants>1 fold path needs two constants —
      // so `add(add(0,a),b)` survived all the way into the generated sim C++.)
      first_const_edge.del_edge();
      if (npending == 1 && !(op == Ntype_op::Sum && sink_pin_name(edge_it2[0].sink) == "bs")) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 0 && nconstants == 1) {
      collapse_forward_always_pin0(node, inp_edges_ordered);
    } else if (npending == 1 && nconstants == 0) {
      if (!(op == Ntype_op::Sum && sink_pin_name(edge_it2[0].sink) == "bs")) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    }
  } else if (op == Ntype_op::Mult) {
    hhds::Edge_class first_const_edge;
    int              nconstants = 0;
    int              npending   = 0;

    Dlop result;
    result = Dlop::create_integer(1);  // multiplicative identity

    livehd::graph_util::Edge_vec edge_it2;
    for (auto& i : inp_edges_ordered) {
      if (!is_const_pin(i.driver)) {
        if (npending == 0) {
          edge_it2.push_back(i);
        }
        npending++;
        continue;
      }
      auto c = hydrate_const(i.driver);
      ++nconstants;
      result = result.mult_op(c);
      if (nconstants == 1) {
        first_const_edge = i;
      } else {
        i.del_edge();
      }
    }
    I(nconstants >= 1 && npending >= 1);  // try_constant_prop routes all-const to replace_all

    if (result.is_known_zero()) {
      replace_node(node, result);  // x * 0 == 0: annihilator (width-adjusts per consumer)
    } else if (result.is_just_i64() && result.to_just_i64() == 1) {
      // Multiplicative identity: drop the constant.
      first_const_edge.del_edge();
      if (npending == 1) {
        collapse_forward_always_pin0(node, edge_it2);
      }
    } else if (npending == 1 && result.is_just_i64() && result.to_just_i64() > 1
               && (result.to_just_i64() & (result.to_just_i64() - 1)) == 0) {
      // Strength reduction: Mult(x, 2^k) -> SHL(x, k). Exact for any sign of x
      // at unlimited precision. Mult's multi-driver `as` shares pid 0 with
      // SHL's `a`, so the live operand stays put and only the amount is wired.
      const int64_t v = result.to_just_i64();
      int           k = 0;
      while ((v >> k) != 1) {
        ++k;
      }
      first_const_edge.del_edge();
      livehd::graph_util::set_type_op(node, Ntype_op::SHL);
      setup_sink_by_name(node, "b").connect_driver(create_const(*current_graph, *Dlop::create_integer(k)));
    } else if (nconstants > 1) {
      // Reattach the folded product as the one surviving constant.
      first_const_edge.del_edge();
      setup_sink_by_name(node, "as").connect_driver(create_const(*current_graph, result));
    }
  } else if (op == Ntype_op::SRA) {
    auto& amt_pin = inp_edges_ordered[1].driver;
    if (is_const_pin(amt_pin) && hydrate_const(amt_pin).is_known_zero()) {
      collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
    }
  } else if (op == Ntype_op::SHL) {
    if (inp_edges_ordered.size() == 2) {
      auto& amt_pin = inp_edges_ordered[1].driver;
      if (is_const_pin(amt_pin) && hydrate_const(amt_pin).is_known_zero()) {
        collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
      }
    }
  }
}

void Cprop::replace_all_inputs_const(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  auto op = type_op_of(node);
  if (op == Ntype_op::SHL) {
    // SHL b is single-driver (the one-hot multi-shift form was removed).
    auto a_pin = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto b_pin = livehd::graph_util::get_driver_of_sink_name(node, "b");
    if (a_pin.is_invalid() || b_pin.is_invalid()) {
      return;
    }
    Dlop val = hydrate_const(a_pin);
    Dlop amt = hydrate_const(b_pin);

    Dlop result;
    result = Dlop::create_integer(0);
    result = result.or_op(val.shl_op(amt));  // val << amt
    replace_node(node, result);

  } else if (op == Ntype_op::Ror) {
    Dlop result;
    result = Dlop::create_integer(0);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.ror_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Set_mask) {
    auto a_pin     = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
    auto value_pin = livehd::graph_util::get_driver_of_sink_name(node, "value");

    if (a_pin.is_invalid()) {
      return;
    }
    Dlop val = hydrate_const(a_pin);

    if (!mask_pin.is_invalid() && !value_pin.is_invalid()) {
      auto mask  = hydrate_const(mask_pin);
      auto value = hydrate_const(value_pin);
      replace_node(node, val.set_mask_op(mask, value));
    } else {
      replace_node(node, val);
    }
  } else if (op == Ntype_op::Sum) {
    Dlop result;
    result = Dlop::create_integer(0);  // additive identity (Invalid no longer folds as 0)
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      if (i.sink.get_port_id() == 0) {
        result = result.add_op(c);
      } else {
        result = result.sub_op(c);
      }
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Or) {
    Dlop result;
    result = Dlop::create_integer(0);  // or identity (Invalid no longer folds as 0)
    for (auto& e : inp_edges_ordered) {
      auto c = hydrate_const(e.driver);
      result = result.or_op(c);
    }

    replace_logic_node(node, result);

  } else if (op == Ntype_op::And) {
    Dlop result;
    result = Dlop::create_integer(-1);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.and_op(c);
    }

    replace_node(node, result);

  } else if (op == Ntype_op::EQ) {
    // EQ is a single-port, multi-edge "all drivers on port a are equal" cell.
    // When both comparator operands resolve to the SAME driver pin (e.g.
    // const==const), HHDS collapses the duplicate driver->sink edges into one,
    // leaving a single input edge. A 1-input "all-equal" is trivially true, and
    // the loop below already yields that (it starts at index 1), so no >1
    // assertion is warranted (try_constant_prop only calls this with n>=1).
    //
    // Dlop::eq_op is THREE-valued: a known/known bit mismatch decides `false`,
    // all-agree decides `true`, and an otherwise-agreeing pair with unknown bits
    // is UNDECIDABLE (unknown_bool). Collapsing that third case with
    // `!is_known_false()` folded `0ub0? == 0` to a definite 1 — which then made
    // `c != 0` a definite 0 and comptime-DECIDED the enclosing if/match arm (the
    // `match` form went further: two arms both folding to 1 built a non-one-hot
    // Hotmux selector, which the unique-if check then reported as overlapping).
    // Leave the cell alone instead — the same three-valued discipline the
    // LT/GT fold below already follows. A DEFINITE mismatch still folds: it
    // decides the all-equal regardless of unknown bits elsewhere.
    auto first   = hydrate_const(inp_edges_ordered[0].driver);
    bool eq      = true;
    bool any_unk = false;
    for (auto i = 1u; i < inp_edges_ordered.size(); ++i) {
      auto c = hydrate_const(inp_edges_ordered[i].driver);
      auto r = first.eq_op(c);
      if (r->is_known_false()) {
        eq = false;
        break;
      }
      any_unk = any_unk || r->has_unknowns();
    }
    if (eq && any_unk) {
      return;  // undecidable on ?-bits: keep the node
    }

    Dlop result;
    result = Dlop::create_integer(eq ? 1 : 0);

    replace_node(node, result);
  } else if (op == Ntype_op::Mux) {
    auto sel_const = hydrate_const(inp_edges_ordered[0].driver);
    if (!sel_const.is_just_i64()) {
      return;  // unknown-bit selector (0sb? poison cond): keep the mux as-is
    }

    size_t sel = sel_const.to_just_i64();

    Dlop result;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == 0) {
        continue;
      }
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(sel + 1)) {
        result = hydrate_const(e.driver);
        break;
      }
    }

    if (result.get_bits() == 0) {
      result = Dlop::create_integer(0);
#ifndef NDEBUG
      Pass::info("WARNING: mux:{} selector:{} goes for disconnected pin in mux. Using zero\n", debug_name(node), sel);
#endif
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Hotmux) {
    // All-const Hotmux: the selector must be one-hot (bit i -> p(i+1)).
    // A zero/multi-hot constant violates the unique-if assume; keep the cell
    // so cgen's case default models the runtime error.
    auto sel_const = hydrate_const(inp_edges_ordered[0].driver);
    I(sel_const.is_just_i64());

    auto sel = sel_const.to_just_i64();
    if (sel <= 0 || (sel & (sel - 1)) != 0) {
      Pass::info("WARNING: hotmux:{} selector:{} is not one-hot (unique-if assume violated); not folding\n", debug_name(node), sel);
      return;
    }
    size_t arm = 0;
    while ((sel >> arm) != 1) {
      ++arm;
    }

    Dlop result;
    for (auto& e : inp_edges_ordered) {
      if (e.sink.get_port_id() == static_cast<hhds::Port_id>(arm + 1)) {
        result = hydrate_const(e.driver);
        break;
      }
    }
    if (result.get_bits() == 0) {
      result = Dlop::create_integer(0);
#ifndef NDEBUG
      Pass::info("WARNING: hotmux:{} selector:{} goes for disconnected pin in hotmux. Using zero\n", debug_name(node), sel);
#endif
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Mult) {
    Dlop result;
    result = Dlop::create_integer(1);
    for (auto& i : inp_edges_ordered) {
      auto c = hydrate_const(i.driver);
      result = result.mult_op(c);
    }

    replace_node(node, result);
  } else if (op == Ntype_op::Div) {
    I(inp_edges_ordered.size() == 2);
    Dlop a = hydrate_const(inp_edges_ordered[0].driver);
    Dlop b = hydrate_const(inp_edges_ordered[1].driver);

    auto result = a.div_op(b);

    replace_node(node, result);
  } else if (op == Ntype_op::Rem) {
    I(inp_edges_ordered.size() == 2);
    Dlop a = hydrate_const(inp_edges_ordered[0].driver);
    Dlop b = hydrate_const(inp_edges_ordered[1].driver);

    auto result = a.rem_op(b);  // truncated remainder; invalid on rem-by-zero

    replace_node(node, result);
  } else if (op == Ntype_op::Not) {
    if (inp_edges_ordered.size() != 1) {
      return;
    }
    replace_node(node, hydrate_const(inp_edges_ordered[0].driver).not_op());
  } else if (op == Ntype_op::Xor) {
    Dlop result;
    result = Dlop::create_integer(0);
    for (auto& e : inp_edges_ordered) {
      result = result.xor_op(hydrate_const(e.driver));
    }
    replace_logic_node(node, result);
  } else if (op == Ntype_op::SRA) {
    auto a_pin = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto b_pin = livehd::graph_util::get_driver_of_sink_name(node, "b");
    if (a_pin.is_invalid() || b_pin.is_invalid()) {
      return;
    }
    Dlop amt = hydrate_const(b_pin);
    // is_just_i64 also rejects >62-bit amounts, whose sra_op yields nil.
    if (amt.has_unknowns() || amt.is_negative() || !amt.is_just_i64()) {
      return;
    }
    replace_node(node, hydrate_const(a_pin).sra_op(amt));
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    // as/bs are multi-driver reduce ports; fold only the plain 2-operand form.
    hhds::Pin_class a_pin, b_pin;
    for (auto& e : inp_edges_ordered) {
      auto& slot = e.sink.get_port_id() == 0 ? a_pin : b_pin;
      if (!slot.is_invalid()) {
        return;  // multi-driver reduce compare: leave it alone
      }
      slot = e.driver;
    }
    if (a_pin.is_invalid() || b_pin.is_invalid()) {
      return;
    }
    Dlop a   = hydrate_const(a_pin);
    Dlop b   = hydrate_const(b_pin);
    auto cmp = op == Ntype_op::LT ? a.lt_op(b) : a.gt_op(b);
    if (cmp->has_unknowns()) {
      return;  // three-valued compare on ?-bits: keep the node
    }
    Dlop result;
    result = Dlop::create_integer(cmp->is_known_true() ? 1 : 0);
    replace_node(node, result);
  } else if (op == Ntype_op::Get_mask) {
    auto a_pin    = livehd::graph_util::get_driver_of_sink_name(node, "a");
    auto mask_pin = livehd::graph_util::get_driver_of_sink_name(node, "mask");
    if (a_pin.is_invalid() || mask_pin.is_invalid()) {
      return;
    }
    Dlop a    = hydrate_const(a_pin);
    Dlop mask = hydrate_const(mask_pin);
    if (mask.is_negative()) {
      // The -1 to-positive idiom: identity on a non-negative value only.
      if (mask.is_just_i64() && mask.to_just_i64() == -1 && a.is_positive()) {
        replace_node(node, a);
      }
      return;
    }
    auto v = a.get_mask_op(mask);
    if (v->is_integer() && !v->has_unknowns() && v->is_negative()) {
      // Dlop's single-set-bit quirk returns the signed 1-bit -1; the cell
      // zero-extends, so the selected set bit is the unsigned 1 (same fix as
      // try_find_single_driver_pin and pass/bitwidth's `gm`).
      auto [qb, qe] = mask.get_mask_range();
      if (qb < 0 || qe != qb + 1) {
        return;  // negative pack from a multi-bit mask: unexpected, keep the node
      }
      v = Dlop::create_integer(1);
    }
    replace_node(node, *v);
  } else if (op == Ntype_op::Concat) {
    // Every lane VALUE is constant, so the whole assembly is one non-negative
    // sum(w_i)-bit constant.
    //
    // The lane table MUST come from graph_util::concat_lanes, never from a hand
    // walk of inp_edges: the width lives on the ODD sink pids and a lane's
    // window is not recoverable from its driver, so a decoder that re-derived
    // widths would shift every lane above the one it got wrong. An empty table
    // means the cell is malformed and must fail CLOSED -- folding it as "zero
    // lanes" would put a constant 0 where a real value was due.
    const auto lanes = livehd::graph_util::concat_lanes(node);
    if (lanes.empty()) {
      return;
    }
    // Fixed-size (no push_back): the Dlop::Concat_lane span below holds RAW
    // pointers into this storage, so it must not reallocate.
    std::vector<Dlop> values(lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i) {
      if (!is_const_pin(lanes[i].value)) {
        return;  // a width operand is always const, so "all inputs constant"
                 // does NOT imply every lane VALUE is one
      }
      values[i]   = hydrate_const(lanes[i].value);
      const int w = lanes[i].width;
      // Dlop::concat_op DEBUG-asserts that a lane fits its window -- an
      // over-wide lane is a caller bug there, even though
      // the CELL truncates it. get_bits() over-approximates once the sign bit
      // itself is unknown, so this refuses slightly more than the assert would:
      // the safe direction, since an unfolded Concat still computes.
      if (values[i].get_bits() > (values[i].is_negative() ? w : w + 1)) {
        return;
      }
    }
    std::vector<Dlop::Concat_lane> hl;
    hl.reserve(lanes.size());
    for (size_t i = 0; i < lanes.size(); ++i) {
      // The n-ary form is the ONLY correct one here. The binary member
      // concat_op(other) sizes the low operand by its SIGNIFICANT bits, which
      // is precisely the width a concat may not be sized by (and it cannot
      // express `{a, b}` at all).
      hl.push_back(Dlop::Concat_lane{&values[i], lanes[i].width});
    }
    auto result = Dlop::concat_op(std::span<const Dlop::Concat_lane>(hl.data(), hl.size()));
    if (!result->is_integer()) {
      return;  // nil: a non-numeric lane has no bit window -- do not fold
    }
    replace_node(node, *result);
  } else {
#ifndef NDEBUG
    Pass::info("FIXME: cprop still does not copy prop node:{}\n", debug_name(node));
#endif
  }
}

void Cprop::replace_node(hhds::Node_class& node, const Dlop& result) {
  auto dpin     = create_const(*current_graph, result);
  auto new_bits = bits_of(dpin);

  // Reconnect every consumer to the const, then bulk-delete the node (del_node
  // drops all of node's edges with no per-edge del_edge lookup). Width-mismatched
  // consumers need a freshly bit-adjusted const, but creating one mid-walk could
  // realloc the node/pin tables and invalidate the live iterator — so defer those
  // (rare) sinks and wire them after the walk. The deferred buffer is bounded by
  // the number of mismatches, never the full fan-out.
  absl::InlinedVector<std::pair<hhds::Pin_class, int32_t>, 2> mismatched;
  for (const auto& out : node.out_edges()) {
    auto out_bits = bits_of(out.driver);
    // A NEGATIVE result must connect exactly: Dlop::adjust_bits is a plain
    // both-planes truncation with no re-signing, so adjust_bits(-1, 8) reads
    // back as +255 -- and under unlimited-precision signed semantics that is
    // a different value (the Not/SRA folds and the Or annihilator produce
    // negatives by construction). Constants are width-free leaves; consumers
    // read the exact value.
    if (new_bits == out_bits || out_bits == 0 || result.is_negative()) {
      dpin.connect_sink(out.sink);
    } else {
      mismatched.emplace_back(out.sink, out_bits);
    }
  }
  for (const auto& [sink, out_bits] : mismatched) {
    auto result2 = result.adjust_bits(out_bits);
    auto dpin2   = create_const(*current_graph, *result2);
    dpin2.connect_sink(sink);
  }

  node.del_node();
}

void Cprop::replace_logic_node(hhds::Node_class& node, const Dlop& result) {
  // Create the shared const up front (NOT lazily mid-walk: a create_const there
  // could realloc the node/pin tables and invalidate the live iterator), then
  // reconnect every consumer and delete the node in one shot — del_node drops
  // all of node's edges with no per-edge del_edge lookup.
  auto dpin_0 = create_const(*current_graph, result);
  for (const auto& out : node.out_edges()) {
    dpin_0.connect_sink(out.sink);
  }

  node.del_node();
}

bool Cprop::scalar_mux(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  if (inp_edges_ordered.size() != 3) {
    return false;
  }

  if (inp_edges_ordered[1].driver == inp_edges_ordered[2].driver) {
    return collapse_forward_for_pin(node, inp_edges_ordered[1].driver);
  }

  // Absorb a negated selector: Mux(EQ(x,0), f, t) == Mux(x, t, f). The 2-arm
  // Mux cell is boolean ("Y = (pid0 == true) ? pid2 : pid1", cell.cpp), and
  // EQ(x,0) inverts exactly that truth. Gate on x provably 0/1 so the rewired
  // selector honors the same 1-bit contract the EQ output did; the EQ then
  // dies by DCE when this was its last consumer.
  for (int guard = 0; guard < 8; ++guard) {
    // The swap below rewires by POSITION, so the three edges must be exactly
    // the 2-arm layout (sel=0, arm0=1, arm1=2). A partially connected wider
    // Hotmux-shaped Mux (say pids 0,1,3) also has three edges, and renumbering
    // its arms would silently select a different one.
    if (inp_edges_ordered[0].sink.get_port_id() != 0 || inp_edges_ordered[1].sink.get_port_id() != 1
        || inp_edges_ordered[2].sink.get_port_id() != 2) {
      break;
    }
    const auto& sel = inp_edges_ordered[0].driver;
    if (sel.is_invalid() || is_const_pin(sel)) {
      break;
    }
    auto m = sel.get_master_node();
    if (m.is_invalid() || type_op_of(m) != Ntype_op::EQ) {
      break;
    }
    auto x = eq_against_const(m, 0);
    if (x.is_invalid() || !is_bool01(x)) {
      break;
    }
    auto arm0 = inp_edges_ordered[1].driver;
    auto arm1 = inp_edges_ordered[2].driver;
    for (auto& e : inp_edges_ordered) {
      e.del_edge();
    }
    node.create_sink_pin(0).connect_driver(x);
    node.create_sink_pin(1).connect_driver(arm1);
    node.create_sink_pin(2).connect_driver(arm0);
    // The fused sweep visits in forward order, so the EQ was already swept: nothing
    // downstream of here deletes it, and a dead cell survives into cgen/sim as
    // a dead def. Collect it now (bwd_del_node also drops its dead fan-in).
    if (!m.is_invalid() && !m.has_out_edges()) {
      bwd_del_node(m);
    }
    inp_edges_ordered = ordered_inp_edges(node);  // refreshes the caller's vector too
    if (inp_edges_ordered.size() != 3) {
      return false;
    }
  }

  // Constant 0/1 arms are a boolean materialization of the selector itself.
  if (is_const_pin(inp_edges_ordered[1].driver) && is_const_pin(inp_edges_ordered[2].driver)) {
    auto c0 = hydrate_const(inp_edges_ordered[1].driver);
    auto c1 = hydrate_const(inp_edges_ordered[2].driver);
    if (!c0.has_unknowns() && !c1.has_unknowns()) {
      const bool  zero0 = c0.is_known_zero();
      const bool  one1  = c1.is_just_i64() && c1.to_just_i64() == 1;
      const auto& sel   = inp_edges_ordered[0].driver;
      // Mux(s, 0, 1) == s -- only when s is already a 0/1 VALUE (the cell
      // treats any nonzero s as true, so a wide s must keep the mux).
      // The inverted-arm sibling Mux(s,1,0) -> EQ(s,0) was measured at only
      // ~67 sites and its EQ spelling gets inlined into latch enable guards
      // (churning the pinned canonical emission), so it is deliberately NOT
      // rewritten.
      if (zero0 && one1 && is_bool01(sel) && collapse_forward_for_pin(node, sel)) {
        return true;
      }
    }
  }

  bool false_path_zero = false;
  if (is_const_pin(inp_edges_ordered[1].driver)) {
    auto v          = hydrate_const(inp_edges_ordered[1].driver);
    false_path_zero = v.is_known_zero() || v.is_string();
  }

  bool true_path_sel = inp_edges_ordered[0].driver == inp_edges_ordered[2].driver;

  // Mux selectors are 0/1 (port = sel+1), so mux(s,0,s) == s. The old
  // -1-as-true folds (mux(s,0,-1)->s, mux(s,s,-1)->s, mux(s,-1,s)->-1,
  // mux(s,s,0)->Not(s)) are only bit-accurate when the consumer reads a
  // single bit; for wider consumers they swap -1 (all ones) for 1 — e.g. a
  // yosys-consolidated 8-bit write-enable mux(reset,0,-1) must yield 0xff,
  // not 1 (caught by lgcheck BMC on mem_reset). Keep only the sound rule.
  if (false_path_zero && true_path_sel) {
    return collapse_forward_for_pin(node, inp_edges_ordered[0].driver);
  }

  return false;
}

void Cprop::scalar_sext(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  const auto& pos_dpin = inp_edges_ordered[1].driver;
  if (!is_const_pin(pos_dpin)) {
    return;
  }

  int64_t self_pos;
  {
    auto v = hydrate_const(pos_dpin);
    if (!v.is_just_i64()) {
      return;
    }
    self_pos = v.to_just_i64();
  }

  const auto& wire_dpin = inp_edges_ordered[0].driver;

  // Sext(X,1) feeding a Mux can be bypassed (a Mux selector/arm is contractually
  // 1 bit) ONLY when X is already 1 bit. For a wider X (e.g. the offset-0 select
  // of a bmux mux-tree, where X is the full ~addr signal) bypassing connects a
  // multi-bit value to the 1-bit Mux selector, which cgen then emits as a
  // reduction-OR (`if (x)` instead of `if (x[0])`) — selecting the wrong arm.
  if (self_pos == 1 && bits_of(wire_dpin) == 1) {
    // Restart-scan the lazy view: bypass each Mux consumer (rewrite + drop edge)
    // and restart; non-Mux consumers stay. No fan-out copy.
    bool progressed = true;
    while (progressed) {
      progressed = false;
      for (const auto& e : node.out_edges()) {
        if (type_op_of(e.sink.get_master_node()) == Ntype_op::Mux) {
          e.sink.connect_driver(wire_dpin);
          e.del_edge();
          progressed = true;
          break;  // iterator invalidated by the rewrite; restart the scan
        }
      }
    }
  }

  auto wire_master = wire_dpin.get_master_node();
  if (type_op_of(wire_master) != Ntype_op::Sext) {
    return;
  }

  // Sext(Sext(X,a),b) == Sext(X, min(a,b))
  auto parent_pos_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "b");
  if (!is_const_pin(parent_pos_dpin)) {
    return;
  }

  auto parent_pos_const = hydrate_const(parent_pos_dpin);
  if (!parent_pos_const.is_just_i64()) {
    return;
  }
  auto parent_pos = parent_pos_const.to_just_i64();

  auto b = std::min(self_pos, parent_pos);
  if (b != self_pos) {
    auto new_const_dpin = create_const(*current_graph, *Dlop::create_integer(b));
    inp_edges_ordered[1].del_edge();
    setup_sink_by_name(node, "b").connect_driver(new_const_dpin);
  }

  auto parent_wire_dpin = livehd::graph_util::get_driver_of_sink_name(wire_master, "a");
  inp_edges_ordered[0].del_edge();
  setup_sink_by_name(node, "a").connect_driver(parent_wire_dpin);
}

// Boolean EQ-chain folds. The lowering spells `!x` as EQ(x,0) and to-bool as
// EQ(EQ(x,0),0); on an already-0/1 value both nodes of the double chain are
// the identity. Forward order guarantees the inner EQ was visited first, so a
// triple chain EQ(EQ(EQ(z,0),0),0) reduces in two visits to EQ(z,0).
bool Cprop::scalar_eq(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  if (inp_edges_ordered.size() != 2) {
    return false;
  }

  // EQ(b, 1) == b for b in {0,1}.
  auto b_pin = eq_against_const(node, 1);
  if (!b_pin.is_invalid() && is_bool01(b_pin)) {
    return collapse_forward_for_pin(node, b_pin);
  }

  auto x_pin = eq_against_const(node, 0);
  if (x_pin.is_invalid() || is_const_pin(x_pin)) {
    return false;
  }
  auto x_master = x_pin.get_master_node();
  if (x_master.is_invalid() || type_op_of(x_master) != Ntype_op::EQ) {
    return false;
  }
  // node = EQ(EQ(y,0), 0). The inner EQ output is 0/1 by contract, and for a
  // 0/1 value the double compare-to-zero is the identity: fold to y when y is
  // itself provably 0/1. (When y is wide this chain is a genuine to-bool
  // coercion and must stay.)
  auto y_pin = eq_against_const(x_master, 0);
  if (y_pin.is_invalid() || !is_bool01(y_pin)) {
    return false;
  }
  return collapse_forward_for_pin(node, y_pin);  // inner EQ dies via DCE when unused
}

// Compose chained constant shifts. All rules are exact under the unlimited-
// precision signed semantics (SHL never drops bits, SRA is floor division):
//   SRA(SRA(x,a),b) -> SRA(x,a+b)      SHL(SHL(x,a),b) -> SHL(x,a+b)
//   SRA(SHL(x,a),b) -> SHL(x,a-b) | x | SRA(x,b-a)   (by sign of a-b)
//   SHL(SRA(x,a),a) -> And(x, -2^a)    (clears the low a bits in place)
// Returns true when the node was rewired (caller re-reads its input edges).
bool Cprop::scalar_shift(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  const auto op = type_op_of(node);
  I(op == Ntype_op::SHL || op == Ntype_op::SRA);
  if (inp_edges_ordered.size() != 2) {
    return false;
  }
  const int outer = const_shl_amount(node);  // shared helper: constant `b` sink or -1
  if (outer < 0) {
    return false;
  }
  auto x_pin = drv_at(node, 0);
  if (x_pin.is_invalid() || is_const_pin(x_pin)) {
    return false;
  }
  auto inner_node = x_pin.get_master_node();
  if (inner_node.is_invalid() || inner_node.get_class_index() == node.get_class_index()) {
    return false;
  }
  const auto inner_op = type_op_of(inner_node);
  if (inner_op != Ntype_op::SHL && inner_op != Ntype_op::SRA) {
    return false;
  }
  const int inner = const_shl_amount(inner_node);
  if (inner < 0) {
    return false;
  }
  auto src = drv_at(inner_node, 0);
  if (src.is_invalid()) {
    return false;
  }

  // The fused sweep visits in forward order, so `inner_node` was already swept:
  // once this node stops reading it nothing downstream of here collects it and
  // a dead shift survives into cgen/sim as a dead def.
  auto drop_dead_inner = [&]() {
    if (!inner_node.is_invalid() && !inner_node.has_out_edges()) {
      bwd_del_node(inner_node);
    }
  };

  auto retarget = [&](Ntype_op new_op, int amount) {
    for (auto e : node.inp_edges()) {  // snapshot; two edges at most
      e.del_edge();
    }
    if (new_op != op) {
      livehd::graph_util::set_type_op(node, new_op);  // SHL/SRA share the a=0,b=1 sink layout
    }
    setup_sink_by_name(node, "a").connect_driver(src);
    setup_sink_by_name(node, "b").connect_driver(create_const(*current_graph, *Dlop::create_integer(amount)));
    drop_dead_inner();
  };

  constexpr int kAmountCap = 1 << 28;  // same bound const_shl_amount enforces
  if (inner_op == op) {                // SRA(SRA) or SHL(SHL): amounts add
    if (inner + outer > kAmountCap) {
      return false;
    }
    retarget(op, inner + outer);
    return true;
  }
  if (op == Ntype_op::SRA) {  // SRA(SHL(x,inner), outer)
    if (outer >= inner) {
      retarget(Ntype_op::SRA, outer - inner);  // amount 0 collapses in the const sweep below
    } else {
      retarget(Ntype_op::SHL, inner - outer);
    }
    return true;
  }
  // SHL(SRA(x,inner), outer): SRA drops the low `inner` bits, so only the
  // exact-rebuild case outer == inner is a pure mask: x & ~(2^inner - 1).
  if (outer == inner && inner == 0) {
    // Both shifts are identities. Dlop::get_mask_value(0) returns 1 (not 0),
    // so the And rewrite below would build ~1 = -2 and clear bit 0; compose
    // to a zero-amount shift instead and let the constant sweep collapse it.
    retarget(op, 0);
    return true;
  }
  if (outer == inner) {
    for (auto e : node.inp_edges()) {
      e.del_edge();
    }
    livehd::graph_util::set_type_op(node, Ntype_op::And);
    // ~(2^a - 1) == -(2^a): the infinite mask with the low a bits clear.
    auto neg_mask = Dlop::get_mask_value(inner)->not_op();
    setup_sink_by_name(node, "as").connect_driver(src);
    setup_sink_by_name(node, "as").connect_driver(create_const(*current_graph, *neg_mask));
    drop_dead_inner();
    return true;
  }
  return false;
}

// Verilog `{N{bit}}` replication survives lowering as Or(SHL(x,k1),...,x): one
// 0/1 source copied to N constant positions (the same shape also appears
// AND-ed with a value as the mux-free select idiom `{N{bit}} & y`). All N
// copies carry the same truth, so the whole tree is Mux(x, 0, sum(2^ki)) --
// which cgen emits as a lazy ternary instead of N-1 word-wide Or/SHL calls.
bool Cprop::try_broadcast_or(hhds::Node_class& node, livehd::graph_util::Edge_vec& inp_edges_ordered) {
  if (inp_edges_ordered.size() < 2 || inp_edges_ordered.size() > 128) {
    return false;
  }

  // Candidate bases for x, from the first operand: the operand itself, or --
  // when that operand is a constant SHL -- the SHL's own source (the bare-x
  // leaf may sit anywhere in the operand list, or not exist at all).
  absl::InlinedVector<hhds::Pin_class, 2> candidates;
  {
    const auto& first = inp_edges_ordered[0].driver;
    if (first.is_invalid() || is_const_pin(first)) {
      return false;
    }
    auto m = first.get_master_node();
    if (!m.is_invalid() && type_op_of(m) == Ntype_op::SHL && const_shl_amount(m) >= 0) {
      auto s = drv_at(m, 0);
      if (!s.is_invalid()) {
        candidates.push_back(s);
      }
    }
    candidates.push_back(first);
  }

  for (const auto& x : candidates) {
    if (!is_bool01(x)) {
      continue;
    }
    auto C  = Dlop::create_integer(0);
    bool ok = true;
    for (auto& e : inp_edges_ordered) {
      if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
        ok = false;
        break;
      }
      const auto& d = e.driver;
      int         k = -1;
      if (same_pin(d, x)) {
        k = 0;
      } else if (!d.is_invalid() && !is_const_pin(d)) {
        auto m = d.get_master_node();
        if (!m.is_invalid() && type_op_of(m) == Ntype_op::SHL) {
          int a = const_shl_amount(m);
          if (a >= 0 && a <= 4096 && same_pin(drv_at(m, 0), x)) {
            k = a;
          }
        } else if (!m.is_invalid() && type_op_of(m) == Ntype_op::Mux) {
          // A partial broadcast this rule already folded on an inner Or of the
          // same chain (forward order visits the inner node first) arrives
          // here as Mux(x, 0, C'): absorb its constant instead of giving up.
          auto sel  = drv_at(m, 0);
          auto arm0 = drv_at(m, 1);
          auto arm1 = drv_at(m, 2);
          if (same_pin(sel, x) && !arm0.is_invalid() && !arm1.is_invalid() && is_const_pin(arm0) && is_const_pin(arm1)) {
            auto c0 = hydrate_const(arm0);
            auto c1 = hydrate_const(arm1);
            if (c0.is_known_zero() && !c1.has_unknowns() && !c1.is_negative()) {
              C = C->or_op(c1);
              continue;  // matched; the bit union came from C' directly
            }
          }
        }
      }
      if (k < 0) {
        ok = false;
        break;
      }
      C = C->or_op(*Dlop::get_mask_value(k, k));  // bit k alone; Or dedups repeated positions
    }
    if (!ok) {
      continue;
    }

    // Decide the replacement width BEFORE minting anything: the Mux is
    // stamped at max(this Or's stamp, C's capacity), and
    // collapse_forward_for_pin only accepts a same-or-narrower unsigned
    // driver, so a C wider than the Or's stamp is refused for certain. Bail
    // here instead of building a Mux plus two consts just to delete them.
    auto      out      = node.create_driver_pin(0);
    const int out_bits = bits_of(out);
    const int bits     = std::max({out_bits, static_cast<int>(C->get_bits()), 1});
    if (out_bits > 0 && bits > out_bits) {
      return false;
    }

    auto mux = create_typed_node(*current_graph, Ntype_op::Mux);
    mux.create_sink_pin(0).connect_driver(x);
    mux.create_sink_pin(1).connect_driver(create_const(*current_graph, *Dlop::create_integer(0)));
    mux.create_sink_pin(2).connect_driver(create_const(*current_graph, *C));
    auto md = mux.create_driver_pin(0);
    livehd::graph_util::set_bits(md, bits);
    livehd::graph_util::set_unsign(md);  // result is 0 or C, both non-negative
    if (collapse_forward_for_pin(node, md)) {
      return true;
    }
    mux.del_node();  // consumer width refused the swap; keep the original tree
    return false;
  }
  return false;
}

hhds::Pin_class Cprop::try_find_single_driver_pin(hhds::Node_class& node, int64_t pos) {
  I(type_op_of(node) == Ntype_op::Set_mask);

  // Follow the Set_mask `a`-driver chain iteratively (the old recursion at the
  // tail was a non-terminating stack-overflow risk: a combinational cycle
  // through the `a` pins is structurally representable and the front-end does
  // not guarantee acyclicity). `pos` is invariant along the chain, so this is a
  // plain loop; the visited set breaks any `a`-pin cycle deterministically.
  absl::flat_hash_set<hhds::Class_index> visited;
  hhds::Node_class                       cur = node;

  while (true) {
    if (!visited.insert(cur.get_class_index()).second) {
      return {};  // cycle in the Set_mask `a`-chain
    }

    auto a_pin    = livehd::graph_util::get_driver_of_sink_name(cur, "a");
    auto mask_pin = livehd::graph_util::get_driver_of_sink_name(cur, "mask");
    if (a_pin.is_invalid() || mask_pin.is_invalid()) {
      return {};
    }
    if (!is_const_pin(mask_pin)) {
      return {};
    }

    auto mask_const               = hydrate_const(mask_pin);
    auto [range_begin, range_end] = mask_const.get_mask_range();
    if (pos >= range_end || pos < range_begin) {
      if (is_const_pin(a_pin)) {
        // get_mask is Pyrope's default-ZEXT bit-select: a non-negative mask packs
        // the selected bits LSB-first as an UNSIGNED value. Dlop::get_mask_op has a
        // single-bit quirk that returns the signed 1-bit -1 for a lone set bit;
        // `#[N]` zero-extends (a set bit is the unsigned 1), so correct it here to
        // match cgen's plain `a[N]` part-select — same fix as upass get_mask_zext
        // and pass/bitwidth's `gm`.
        const auto pos_mask = Dlop::get_mask_value(pos);
        auto       v        = hydrate_const(a_pin).get_mask_op(*pos_mask);
        if (!pos_mask->is_negative() && v->is_integer() && !v->has_unknowns() && v->is_negative()) {
          v = Dlop::create_integer(1);
        }
        return create_const(*current_graph, *v);
      }
      auto a_master = a_pin.get_master_node();
      if (type_op_of(a_master) != Ntype_op::Set_mask) {
        return {};
      }
      cur = a_master;  // walk to the next Set_mask in the chain
      continue;
    }
    if (range_begin == pos && range_end == (pos + 1)) {
      return livehd::graph_util::get_driver_of_sink_name(cur, "value");
    }

    return {};
  }
}

// Resolve a CONSTANT slice read through shifts and packed wires to the operand
// that actually drives it. In particular, this is the canonical backend-neutral
// collapse:
//
//   Get_mask(SRA(x,k), mask[lo,hi)) -> Get_mask(x, mask[lo+k,hi+k))
//
// It also handles packed values:
//   Get_mask(Or(.. shl(x_j,k_j) ..), mask[lo,hi)) -> Get_mask(x_j, ..).
// The firtool/Chisel bundle-as-UInt idiom writes a wide net as an Or of
// constant-shifted disjoint fields and reads fields back with constant slices;
// at word level that reads as a combinational cycle even though the bit ranges
// never overlap. Rewiring the read to its one real driver deletes the false
// edge. Every rule is node-NON-increasing and locally value-preserving, so this
// is an unconditional win and needs no cycle gate and no creation budget (unlike
// the node-creating Mux/EQ/distribute rules in graph/split_selfref.cpp).
//
// Returns true iff `node` was deleted (folded to a constant). A `false` return
// may still have REWIRED `node`, so callers must re-read its input edges.
bool Cprop::scalar_get_mask_packed(hhds::Node_class& node, const Dlop& mask_const) {
  if (!mask_const.is_positive()) {
    return false;  // -1 (to-unsigned) is consumed by Rule 4 before we get here
  }
  auto [lo, hi] = mask_const.get_mask_range();  // half-open; {-1,-1} = noncontiguous
  if (lo < 0 || hi <= lo) {
    return false;
  }

  auto cur = drv_at(node, 0);

  // No decreasing measure exists: the Or rule descends with [lo,hi) UNCHANGED, so
  // an Or->operand->Or chain inside a word-level SCC can revisit the same slice
  // and spin forever. A repeat means the slice depends on itself -- a GENUINE
  // bit-level cycle -- so stop and leave the node alone.
  absl::flat_hash_set<std::tuple<hhds::Class_index, int, int>> seen;
  int                                                          walk_depth = 0;

  for (;;) {
    if (++walk_depth > kPackedSliceWalkLimit) {
      break;  // keep the original graph instead of charging an unbounded chain walk
    }
    if (cur.is_invalid() || is_const_pin(cur)) {
      break;
    }
    if (!seen.insert({cur.get_class_index(), lo, hi}).second) {
      break;
    }
    auto m = cur.get_master_node();
    if (m.is_invalid()) {
      break;
    }
    auto op = type_op_of(m);

    if (op == Ntype_op::Or) {
      // Or is N-ary on ONE sink; a driver on any other port is not this shape.
      hhds::Pin_class overlapper;
      int             n_over  = 0;
      int             fan_in  = 0;
      bool            bad_pid = false;
      for (const auto& e : m.inp_edges()) {
        if (++fan_in > kPackedSliceFanInLimit) {
          bad_pid = true;  // conservative bailout: do not fold a very wide Or
          break;
        }
        if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
          bad_pid = true;
          break;
        }
        auto f = footprint(e.driver, 0);
        // An unbounded (kFpBail) operand counts as an OVERLAPPER: soundness rests
        // ONLY on the others being provably disjoint from [lo,hi).
        if (f.first < 0 || !(hi <= f.first || lo >= f.second)) {
          ++n_over;
          overlapper = e.driver;
        }
      }
      if (bad_pid || n_over > 1) {
        break;
      }
      if (n_over == 0) {  // no operand reaches this slice: it reads as zero
        replace_node(node, *Dlop::create_integer(0));
        return true;
      }
      cur = overlapper;  // unique cover: descend, range unchanged
      continue;
    }

    if (op == Ntype_op::SHL) {
      int k = const_shl_amount(m);
      if (k < 0) {
        break;
      }
      if (hi <= k) {  // entirely below the shifted value: zeros
        replace_node(node, *Dlop::create_integer(0));
        return true;
      }
      if (lo < k) {
        break;  // straddles the shift boundary; splitting would create nodes
      }
      auto x = drv_at(m, 0);
      if (x.is_invalid()) {
        break;
      }
      cur  = x;
      lo  -= k;
      hi  -= k;
      continue;
    }

    if (op == Ntype_op::SRA) {
      int k = const_shl_amount(m);
      if (k < 0) {
        break;
      }
      auto x = drv_at(m, 0);
      if (x.is_invalid()) {
        break;
      }
      cur  = x;
      lo  += k;
      hi  += k;
      continue;
    }

    if (op == Ntype_op::Get_mask) {
      auto r = const_mask_range(m);
      if (r.first < 0) {
        break;
      }
      int w = r.second - r.first;
      // Get_mask is an explicit zero-extension/select operation, so even a
      // one-bit result is the non-negative magnitude 0/1. It composes with a
      // following low slice exactly like every wider Get_mask. Dlop's legacy
      // one-bit -1 representation is corrected by bitwidth and must not leak
      // into the LGraph transform (cgen likewise emits the 0/1 magnitude).
      if (w <= 0 || hi > w) {
        break;
      }
      auto x = drv_at(m, 0);
      if (x.is_invalid()) {
        break;
      }
      cur  = x;
      lo  += r.first;
      hi  += r.first;
      continue;
    }

    if (op == Ntype_op::Set_mask) {
      // A Set_mask rewrites ONLY the bits inside its own constant lane, so a
      // slice DISJOINT from that lane reads exactly what the base `a` already
      // held and the write can be skipped.
      //
      // Skipping it is what dissolves the word-level cycle that a per-field
      // struct write chain creates. `c.f0 = ..; c.f1 = ..; c.f2 = ..` on one
      // packed net lowers to Set_mask(Set_mask(Set_mask(base,..),..),..), and a
      // later read of f0 binds to the LAST version -- so if f2's value depends
      // on that read (through anything at all), the word-level graph closes a
      // loop that does not exist bit-wise, because the fields never alias.
      // Walking the read down to the version that actually wrote its lane is
      // what the per-leaf (flattened) form gives for free.
      //
      // A PARTIAL overlap would need a concat of two sources, which creates
      // nodes and would break this function's node-non-increasing contract, so
      // that case stops the walk.
      auto r = const_mask_range(m);
      if (r.first < 0) {
        break;  // non-constant / noncontiguous lane: cannot prove disjointness
      }
      if (hi <= r.first || lo >= r.second) {
        auto x = drv_at(m, 0);  // `a`: the packed value before this field write
        if (x.is_invalid()) {
          break;
        }
        cur = x;  // [lo,hi) unchanged -- the slice means the same bits in `a`
        continue;
      }
      if (lo >= r.first && hi <= r.second) {
        // The slice lies ENTIRELY inside this lane, so it reads back exactly
        // what `value` put there: re-base onto `value` and drop the dependency
        // on `a` altogether.
        //
        // This leg is the one that matters for a struct FIELD read. Skipping
        // the disjoint writes alone is not enough: the read would still land on
        // the Set_mask that wrote its own lane, and that node's `a` operand
        // keeps the WHOLE earlier write chain -- and everything those writes
        // depend on -- upstream of the read. That is how the false cycle
        // survives one round of skipping.
        //
        // Guard: `value` must be provably NON-NEGATIVE with a bounded bit
        // footprint. footprint() bails on a signed or unbounded pin, and a bail
        // refuses here -- the safe direction. Non-negative is what makes the
        // two forms agree ABOVE `value`'s significant width: Get_mask reads 0
        // there, and a zero-extended `value` is what the lane holds.
        //
        // Deliberately NOT required: that `value` fit inside the lane. Set_mask
        // truncates any excess. Those truncated positions are outside [lo,hi)
        // by construction
        // (the slice is contained in the lane), so the read never observes
        // them. An earlier version of this guard demanded `fv.second <= lane
        // width` and refused EVERY real field write for exactly that off-by-one
        // reason; it fired on none of minion's vpu_ctrl reads.
        auto v = drv_at(m, 4);  // `value`
        if (v.is_invalid()) {
          break;
        }
        auto fv = footprint(v, 0);
        if (fv.first < 0) {
          break;
        }
        cur  = v;
        lo  -= r.first;  // `value` is LSB-aligned to the lane's low bit
        hi  -= r.first;
        continue;
      }
      break;  // straddles the lane boundary: would need a concat
    }

    if (op == Ntype_op::Concat) {
      // The cheapest pack to resolve, because the cell CARRIES its lane table:
      // none of the footprint/disjointness proof the Or/SHL/Set_mask arms above
      // need applies -- lane i owning exactly [offset_i, offset_i + width_i) is
      // a cell invariant. This is the arm that keeps a canonicalized graph at
      // least as resolvable as the hand-spelled one it replaced.
      //
      // NO "value must be non-negative" guard, unlike the Set_mask arm: a lane
      // holds `value mod 2^w`, i.e. value's low w bits as spelled in two's
      // complement, and a Get_mask below the window reads those same
      // conceptual bits, so the forms already agree for a negative or over-wide
      // lane (same ruling as graph/split_selfref.cpp's Concat arm).
      const auto lanes = livehd::graph_util::concat_lanes(m);
      if (lanes.empty()) {
        break;  // malformed cell: fail closed, never as "zero lanes"
      }
      const int32_t total = livehd::graph_util::concat_total_width(lanes);  // already decoded above
      if (lo >= total) {
        // Entirely above the top lane. A Concat is non-negative and strictly
        // below 2^total, so those positions read as exact zeros.
        replace_node(node, *Dlop::create_integer(0));
        return true;
      }
      const livehd::graph_util::Concat_lane* hit = nullptr;
      for (const auto& l : lanes) {
        if (lo >= l.offset && hi <= l.offset + l.width) {
          hit = &l;
          break;
        }
      }
      if (hit == nullptr) {
        break;  // straddles two lanes: splitting would create nodes
      }
      cur  = hit->value;
      lo  -= hit->offset;  // the lane value is LSB-aligned to its window
      hi  -= hit->offset;
      continue;
    }

    break;
  }

  auto a_now = drv_at(node, 0);
  if (cur.is_invalid() || cur == a_now) {
    return false;  // nothing resolved
  }
  I(hi > lo, "packed-slice fold produced an empty range");

  // Selecting every declared bit of an unsigned source is an identity in the
  // signed unlimited-width IR: its range already guarantees a zero sign. This
  // is the terminal form of a one-bit read walked through SRA/Set_mask chains.
  const int cur_bits = bits_of(cur);
  if (lo == 0 && cur_bits > 0 && hi >= cur_bits && livehd::graph_util::is_unsign(cur) && collapse_forward_for_pin(node, cur)) {
    return true;
  }

  // Every rule preserves (hi-lo), so the mask popcount is invariant and the
  // pin's existing literal width stays correct: do NOT re-stamp bits/sign.
  auto old_master = a_now.get_master_node();
  auto edges      = node.inp_edges();  // snapshot before mutating
  for (auto e : edges) {
    auto pid = static_cast<uint32_t>(e.sink.get_port_id());
    if (pid == 0 || pid == 2) {
      e.del_edge();
    }
  }
  setup_sink_by_name(node, "a").connect_driver(cur);
  setup_sink_by_name(node, "mask").connect_driver(create_const(*current_graph, *Dlop::get_mask_value(hi - 1, lo)));

  // The bypassed pack/wire-buffer chain is usually dead now; bwd_del_node also
  // sweeps the inputs that become dead behind it.
  if (!old_master.is_invalid() && !livehd::graph_util::is_builtin_node(old_master) && !Ntype::is_loop_last(type_op_of(old_master))
      && !old_master.has_out_edges()) {
    bwd_del_node(old_master);
  }
  return false;  // rewired in place, not deleted
}

bool Cprop::scalar_get_mask(hhds::Node_class& node) {
  auto a_pin    = drv_at(node, 0);
  auto mask_pin = drv_at(node, 2);
  if (a_pin.is_invalid() || mask_pin.is_invalid() || !node.has_out_edges()) {
    node.del_node();
    return true;
  }
  if (!is_const_pin(mask_pin)) {
    return false;
  }

  auto mask_const = hydrate_const(mask_pin);

  restamp_finite_get_mask(node, mask_const);

  // Rule 4: get_mask(a, -1) == a — only when `a` is provably non-negative.
  // get_mask always yields a non-negative value (it zero-extends the selected
  // bits), so it is the to-positive wrapper for signed-read pins (e.g. module
  // ports, which cgen declares `signed`). Bypassing it around a pin that can
  // go negative changes the value: u3 a=0b101 must read 5, not -3 (caught by
  // LEC once the lgcheck BMC stage became sound).
  if (mask_const.is_just_i64() && mask_const.to_just_i64() == -1) {
    bool nonneg = false;
    if (is_const_pin(a_pin)) {
      auto v = hydrate_const(a_pin);
      // is_positive() is exact at any width (an is_just_i64 gate would treat
      // a >62-bit non-negative constant as "maybe negative"); an unknown sign
      // bit reads negative, which stays conservative for Rule 4.
      nonneg = v.is_positive();
    } else if (is_graph_input_pin(a_pin)) {
      // `unsign` is a value-range guarantee in LGraph. The physical W-bit port
      // representation is a backend boundary concern, not an IR Get_mask.
      nonneg = livehd::graph_util::is_unsign(a_pin);
    } else {
      auto a_master = a_pin.get_master_node();
      nonneg = (!a_master.is_invalid() && type_op_of(a_master) == Ntype_op::Get_mask) || livehd::graph_util::is_unsign(a_pin);
    }
    if (!nonneg) {
      return false;
    }
    return collapse_forward_for_pin(node, a_pin);
  }

  // Rule 5 (WIDTH NO-OP): get_mask(a, 2^n-1) == a when `a` provably has no bit
  // at or above n, so the mask clears nothing.
  //
  // An LGraph cell is an unbounded-precision integer op and `bits` is DERIVED
  // metadata (pass/bitfuzz strips it and recomputes it). An UNSIGNED pin of
  // literal width W holds a value < 2^W: if W <= n the cell is pure overhead.
  //
  // THREE conditions, each load-bearing:
  //  * is_unsign(a) -- for a SIGNED `a` this mask is the to-positive coercion
  //    (it clears the sign extension), so dropping it would read -3 as a large
  //    positive. Rule 4 above is the signed counterpart.
  //  * a LOW-CONTIGUOUS mask [0,n) -- a bit-extract mask that does not start at
  //    bit 0 repositions bits and is never an identity.
  {
    const int me = low_mask_width(mask_const);  // <0 unless a low-contiguous 2^n-1
    if (me > 0) {
      const int abits = bits_of(a_pin);
      if (abits > 0 && livehd::graph_util::is_unsign(a_pin) && abits <= me) {
        if (collapse_forward_for_pin(node, a_pin)) {
          return true;
        }
        // collapse refuses when a consumer's declared width disagrees; leave the
        // cell in place rather than restamp somebody else's pin.
      }
    }
  }

  // Low window of a complement: Get_mask(Not(Get_mask(y, ones n)), ones m) with
  // m <= n reads only bits [0,m) of the complement, and each of those bits
  // agrees with Not(y)'s (Not is a per-bit complement of the infinite string,
  // and the inner mask only rewrites bits >= n). The logical-NOT lowering
  // And(Not(And(x,1)),1) is this shape once the And masks canonicalize to
  // Get_mask. Rewiring the Not to y is only safe when this node is the Not's
  // sole consumer -- any other reader may observe bits >= n.
  {
    const int m_w = low_mask_width(mask_const);
    if (m_w > 0 && !is_const_pin(a_pin) && !is_graph_input_pin(a_pin)) {
      auto not_node = a_pin.get_master_node();
      if (!not_node.is_invalid() && type_op_of(not_node) == Ntype_op::Not) {
        auto w_pin = drv_at(not_node, 0);
        if (has_single_consumer(a_pin) && !w_pin.is_invalid() && !is_const_pin(w_pin) && !is_graph_input_pin(w_pin)) {
          auto inner = w_pin.get_master_node();
          if (!inner.is_invalid() && type_op_of(inner) == Ntype_op::Get_mask && inner.get_class_index() != node.get_class_index()) {
            auto inner_mask_pin = drv_at(inner, 2);
            if (!inner_mask_pin.is_invalid() && is_const_pin(inner_mask_pin)
                && low_mask_width(hydrate_const(inner_mask_pin)) >= m_w) {
              auto y = drv_at(inner, 0);
              if (!y.is_invalid() && !same_pin(y, a_pin)) {  // a self-feeding Not must stay
                auto not_sink = find_sink_pin(not_node, "a");
                if (!not_sink.is_invalid()) {
                  not_sink.del_sink();
                  y.connect_sink(not_sink);
                  if (!inner.has_out_edges()) {
                    bwd_del_node(inner);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  auto a_master = a_pin.get_master_node();
  if (type_op_of(a_master) != Ntype_op::Set_mask) {
    // `a` is not a Set_mask writer, so the Set_mask path below cannot fire and
    // the packed-wire fold (Or / SHL / Get_mask operands) is the complement.
    // Running it here keeps `a_pin`/`mask_const` fresh: the fold may rewire this
    // node's `a`+`mask` in place, which would invalidate both.
    return scalar_get_mask_packed(node, mask_const);
  }

  auto [range_begin, range_end] = mask_const.get_mask_range();

  if ((range_begin + 1) == range_end) {
    auto dpin = try_find_single_driver_pin(a_master, range_begin);
    if (!dpin.is_invalid()) {
      return collapse_forward_for_pin(node, dpin);
    }
  }

  // A MULTI-BIT read of a Set_mask chain (or a single-bit one the exact-lane
  // rule above could not resolve) used to return here unconditionally, which is
  // why a packed struct's per-field write chain kept every read pinned to the
  // LAST version -- the word-level false cycle. The packed-slice walker now has
  // a Set_mask arm that skips the writes whose lane is provably disjoint from
  // this slice, so hand the node to it instead of giving up.
  return scalar_get_mask_packed(node, mask_const);
}

bool Cprop::scalar_set_mask(hhds::Node_class& node) {
  auto base_pin  = livehd::graph_util::get_driver_of_sink_name(node, "a");
  auto mask_pin  = livehd::graph_util::get_driver_of_sink_name(node, "mask");
  auto value_pin = livehd::graph_util::get_driver_of_sink_name(node, "value");
  if (base_pin.is_invalid() || mask_pin.is_invalid() || value_pin.is_invalid() || !is_const_pin(base_pin)
      || !is_const_pin(mask_pin)) {
    return false;
  }

  const auto base = hydrate_const(base_pin);
  const auto mask = hydrate_const(mask_pin);
  if (!base.is_known_zero() || mask.has_unknowns() || mask.is_negative()) {
    return false;
  }

  const auto [mb, me] = mask.get_mask_range();
  if (mb != 0 || me <= 0 || !livehd::graph_util::is_unsign(value_pin) || is_const_pin(value_pin) || is_graph_input_pin(value_pin)) {
    return false;
  }

  // set_mask(0, ones[0,n), value) == value when the value is already known to
  // fit below bit n. An unsigned bits=W hint means value < 2^W, so the mask
  // clears nothing when W <= n. Boundary/state pins remain excluded because
  // this rewrite is limited to computed expressions.
  auto       vm       = value_pin.get_master_node();
  const auto vmo      = type_op_of(vm);
  const bool computed = !vm.is_invalid() && vmo != Ntype_op::Invalid && is_computed_comb_op(vmo);
  const int  vbits    = bits_of(value_pin);
  if (!computed || vbits <= 0 || vbits > me) {
    return false;
  }

  return collapse_forward_for_pin(node, value_pin);
}

// And(x, 2^n-1) [binary, exactly one constant] is value-identical to
// Get_mask(x, 2^n-1) for ANY sign of x: both keep the low n bits and yield the
// non-negative packed value. Retype so every low-mask truncation shares the
// ONE canonical Get_mask shape: the packed-slice walker, the width no-op rules
// (scalar_get_mask Rule 4/5) and the slice normalizer all key on Get_mask, so
// an And-spelled truncation was invisible to every one of those folds -- the
// dominant miss being the And(SRA(x,k),1) bit-extract, which as
// Get_mask(SRA(x,k),1) resolves through Or/SHL/Set_mask pack chains and
// vanishes outright when x is already known 0/1.
void Cprop::canonicalize_and_mask(hhds::Node_class& node) {
  if (node.is_invalid() || type_op_of(node) != Ntype_op::And || !node.has_out_edges()) {
    return;
  }
  // Only an UNSIGNED-stamped And may become a Get_mask. A signed-stamped
  // And-mask (lgyosys_tolg's shift lowering) is emitted as a WRAPPING
  // masked value whose downstream companion Get_mask is the to-positive
  // fixer -- the pair is a unit, and a signed Get_mask means something else
  // entirely (re-sign at the selected top bit). Retyping the And flipped
  // srasll's `s0 >> s1` cone from +0xFFFF to -1.
  if (!livehd::graph_util::is_unsign(node.create_driver_pin(0))) {
    return;
  }
  auto edges = node.inp_edges();  // snapshot
  if (edges.size() != 2) {
    return;
  }
  int const_idx = -1;
  for (int i = 0; i < 2; ++i) {
    if (is_const_pin(edges[i].driver)) {
      if (const_idx >= 0) {
        const_idx = -2;  // two constants: constant-fold territory, not ours
        break;
      }
      const_idx = i;
    }
  }
  if (const_idx < 0) {
    return;
  }
  auto      mask_pin = edges[const_idx].driver;
  const int n        = low_mask_width(hydrate_const(mask_pin));
  if (n <= 0) {
    return;
  }
  auto x_pin = edges[const_idx ^ 1].driver;
  // With literal width hints the And and Get_mask spellings agree for every
  // operand producer; there is no producer-specific sign-slot exception.
  if (x_pin.is_invalid() || is_const_pin(x_pin)) {
    return;
  }
  edges[0].del_edge();
  edges[1].del_edge();
  livehd::graph_util::set_type_op(node, Ntype_op::Get_mask);
  setup_sink_by_name(node, "a").connect_driver(x_pin);
  setup_sink_by_name(node, "mask").connect_driver(mask_pin);
  // Normalize the width stamp to the literal selection capacity. The rest of
  // this sweep (scalar_get_mask, the packed-slice walker, CSE's width screen)
  // reads bits_of on this pin long before pass.bitwidth gets to recompute it,
  // and an And stamped narrower than its own mask would read as a lossy slice.
  auto out = node.create_driver_pin(0);
  if (bits_of(out) < n) {
    livehd::graph_util::set_bits(out, n);
  }
}

// Hash-cons identical combinational nodes: two nodes with the same op reading
// the SAME driver pins compute the same unlimited-precision value, so every
// consumer of the duplicate can read the survivor instead. Sub-inlining and
// loop replication routinely clone whole expression cones (7.4% of minion's
// emitted sim statements were byte-identical duplicate defs), and cgen emits
// one Slop call per surviving node, so each merge is a direct sim-op win --
// and one node fewer for every other backend.
//
// Key = (op, sorted (pid, driver-pin index) list). Sorting makes the key
// order-insensitive, which is exact for multi-driver ports (And/Or/Xor/EQ/
// Mult `as`, Sum/LT/GT `as`/`bs` -- all commutative per port) and harmless
// for positional single-driver ports, where the pid half disambiguates.
//
// Guards, each load-bearing:
//  * pure combinational ops only (is_computed_comb_op), minus LUT: its function
//    table lives in a node attribute the key does not cover;
//  * identical bits/sign stamps on the output -- a twin re-stamped differently
//    by an earlier fold may be read at another width by its consumers;
//  * names: prefer keeping a named twin (user wires are addressable by sim
//    queries/VCD); when BOTH carry names, skip rather than pick a loser.
void Cprop::cse_pass(const std::vector<hhds::Node_class>& order) {
  using Key = std::pair<uint16_t, std::vector<std::pair<uint32_t, uint64_t>>>;

  bool                                       changed = false;
  absl::flat_hash_map<Key, hhds::Node_class> seen;
  // Consumers that may have gained a duplicate operand from a merge below.
  // Merging twins turns their COMMON consumers' operands into duplicates, so
  // record the idempotent ones BEFORE the collapse moves the edges.
  std::vector<hhds::Node_class>              idem;
  absl::flat_hash_set<hhds::Class_index>     idem_seen;
  const auto                                 record_idem_consumers = [&](const hhds::Node_class& dying) {
    for (const auto& out : dying.out_edges()) {
      auto consumer = out.sink.get_master_node();
      if (consumer.is_invalid()) {
        continue;
      }
      const auto cop = type_op_of(consumer);
      if ((cop == Ntype_op::And || cop == Ntype_op::Or) && idem_seen.insert(consumer.get_class_index()).second) {
        idem.push_back(consumer);
      }
    }
  };

  for (auto node : order) {
    if (node.is_invalid()) {
      continue;
    }
    const auto op = type_op_of(node);
    if (op == Ntype_op::Invalid || !is_computed_comb_op(op) || op == Ntype_op::LUT) {
      continue;
    }
    if (!node.has_out_edges()) {
      continue;
    }
    Key key;
    key.first   = static_cast<uint16_t>(op);
    bool usable = true;
    for (const auto& e : node.inp_edges()) {
      if (e.driver.is_invalid()) {
        usable = false;
        break;
      }
      key.second.emplace_back(static_cast<uint32_t>(e.sink.get_port_id()), static_cast<uint64_t>(e.driver.get_class_index().value));
    }
    if (!usable || key.second.empty()) {
      continue;
    }
    std::sort(key.second.begin(), key.second.end());

    auto [it, inserted] = seen.try_emplace(key, node);
    if (inserted) {
      continue;
    }
    auto kept = it->second;
    if (kept.is_invalid()) {  // a cascade delete took the survivor; re-seed
      it->second = node;
      continue;
    }
    auto kd = kept.create_driver_pin(0);
    auto nd = node.create_driver_pin(0);
    if (kd.is_invalid() || nd.is_invalid()) {
      continue;
    }
    if (bits_of(kd) != bits_of(nd) || livehd::graph_util::is_unsign(kd) != livehd::graph_util::is_unsign(nd)) {
      continue;
    }
    // On an already-colored graph (2opt/synthesis reruns) a merge across two
    // colors would move work between partitions behind the color pass's back.
    if (livehd::graph_util::has_color(node) != livehd::graph_util::has_color(kept)
        || (livehd::graph_util::has_color(node) && livehd::graph_util::color_of(node) != livehd::graph_util::color_of(kept))) {
      continue;
    }
    // Stamp-consistency screen: an UNSIGNED-stamped pin whose op can go
    // negative when an input does is a frontend stamping violation (the IR
    // contract says every signed driver is stamped -- see the
    // collapse_forward_for_pin comment). At fanout 1 the lie stays latent
    // because cgen INLINES the expression into its consumer's context;
    // merging twins raises fanout and MATERIALIZES the pin at its lying
    // unsigned width (slang's `s0 >>> s1` SRA landed as reg[4:0] and read
    // -1 as 31). Ops that are non-negative by cell CONTRACT are exempt.
    // Concat joins the exempt list: it is non-negative by CELL CONTRACT (each
    // lane is masked into its own window, so a signed lane's sign never
    // escapes), and a signed lane is the norm rather than a stamping lie.
    // Without the exemption every real pack would read "suspicious" and no
    // Concat would ever CSE.
    if (livehd::graph_util::is_unsign(nd) && op != Ntype_op::EQ && op != Ntype_op::LT && op != Ntype_op::GT && op != Ntype_op::Ror
        && op != Ntype_op::Get_mask && op != Ntype_op::Concat) {
      bool suspicious = false;
      for (const auto& e : node.inp_edges()) {
        if (is_const_pin(e.driver) ? hydrate_const(e.driver).is_negative() : !livehd::graph_util::is_unsign(e.driver)) {
          suspicious = true;
          break;
        }
      }
      if (suspicious) {
        continue;
      }
    }
    const bool kept_named = livehd::graph_util::has_name(kept) || !livehd::graph_util::pin_name_of(kd).empty();
    const bool node_named = livehd::graph_util::has_name(node) || !livehd::graph_util::pin_name_of(nd).empty();
    if (kept_named && node_named) {
      continue;
    }
    if (node_named) {  // keep the named twin: fold the anonymous survivor into it
      record_idem_consumers(kept);
      if (collapse_forward_for_pin(kept, nd)) {
        it->second = node;
        changed    = true;
      }
      continue;
    }
    record_idem_consumers(node);
    if (collapse_forward_for_pin(node, kd)) {
      changed = true;
    }
  }

  if (!changed) {
    return;
  }
  // Or(a, a') becomes Or(a, a) with two parallel edges. And/Or are
  // idempotent, so drop the extra edges (the VALUE was already equal before
  // the merge, so this is purely structural); a node left with one input
  // forwards at the next cprop run.
  // `idem` was collected above rather than by re-walking the whole graph:
  // only a consumer of a merged twin can have gained a duplicate operand.
  for (auto& node : idem) {
    if (node.is_invalid()) {
      continue;
    }
    absl::flat_hash_set<hhds::Class_index>   first_seen;
    absl::InlinedVector<hhds::Edge_class, 4> extras;
    for (const auto& e : node.inp_edges()) {
      if (e.driver.is_invalid() || static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
        continue;
      }
      if (!first_seen.insert(e.driver.get_class_index()).second) {
        extras.push_back(e);
      }
    }
    for (auto& e : extras) {
      e.del_edge();
    }
    if (!extras.empty()) {
      auto edges = ordered_inp_edges(node);
      if (edges.size() == 1) {
        collapse_forward_always_pin0(node, edges);
      }
    }
  }
}

void Cprop::scalar_node(hhds::Node_class& node) {
  if (node.is_invalid()) {
    return;
  }
  auto op = type_op_of(node);
  // IO/state/Sub/const/attr cells are not copy-propagatable. Hotmux sits
  // between Mux (36) and IO (39) and IS handled (const one-hot selector fold,
  // same-arm collapse); Concat sits ABOVE the boundary band by encoding
  // accident and is handled too (all-const lane fold) -- see
  // is_computed_comb_op.
  if (!is_computed_comb_op(op)) {
    return;
  }

  if (!node.has_out_edges()) {
    bwd_del_node(node);
    return;
  }

  auto inp_edges_ordered = ordered_inp_edges(node);

  if (op == Ntype_op::Sext) {
    if (inp_edges_ordered.size() >= 2) {
      scalar_sext(node, inp_edges_ordered);
    }
  } else if (op == Ntype_op::Mux) {
    bool del = scalar_mux(node, inp_edges_ordered);
    if (del) {
      return;
    }
  } else if (op == Ntype_op::EQ) {
    if (scalar_eq(node, inp_edges_ordered)) {
      return;
    }
  } else if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    if (scalar_shift(node, inp_edges_ordered)) {
      // Rewired in place (new source and/or amount): refresh the edge view
      // so the constant sweep below sees the composed shift, not the stale
      // one (an amount rewritten to 0 collapses right there).
      inp_edges_ordered = ordered_inp_edges(node);
    }
  } else if (op == Ntype_op::Or) {
    if (try_broadcast_or(node, inp_edges_ordered)) {
      return;
    }
  } else if (op == Ntype_op::Get_mask) {
    bool del = scalar_get_mask(node);
    if (del || node.is_invalid()) {
      return;
    }
    // The packed-slice fold can rewire `a`/`mask` in place and return false,
    // which would leave the vector captured above stale.
    inp_edges_ordered = ordered_inp_edges(node);
  } else if (op == Ntype_op::Set_mask) {
    if (scalar_set_mask(node)) {
      return;
    }
  }

  auto replaced_some = try_constant_prop(node, inp_edges_ordered);

  if (node.is_invalid()) {
    return;
  }

  if (replaced_some) {
    inp_edges_ordered = ordered_inp_edges(node);
  }

  try_collapse_forward(node, inp_edges_ordered);
}

void Cprop::canonicalize_latch_hold(const hhds::Node_class& latch) {
  if (latch.is_invalid() || type_op_of(latch) != Ntype_op::Latch) {
    return;
  }
  auto q      = latch.get_driver_pin(0);
  auto din    = livehd::graph_util::get_driver_of_sink_name(latch, "din");
  auto enable = livehd::graph_util::get_driver_of_sink_name(latch, "enable");

  // An always-active latch has no opaque/holding phase and therefore no
  // state: Q follows D combinationally. Pyrope now wires explicit enable=1
  // for this case; keep accepting a missing enable as the historical/default
  // spelling so old LGs and slang's fully-assigned nonblocking blocks also
  // canonicalize. Runtime reset is deliberately excluded because replacing
  // it requires preserving the reset-priority value mux.
  bool always_open = enable.is_invalid();
  if (!enable.is_invalid()) {
    auto en_value = const_truth(enable);
    if (en_value.has_value()) {
      bool active_high = true;
      auto posclk      = livehd::graph_util::get_driver_of_sink_name(latch, "posclk");
      if (!posclk.is_invalid()) {
        auto positive = const_truth(posclk);
        if (!positive.has_value()) {
          return;
        }
        active_high = *positive;
      }
      always_open = *en_value == active_high;
    }
  }
  auto reset = livehd::graph_util::get_driver_of_sink_name(latch, "reset_pin");
  if (always_open && reset.is_invalid() && !q.is_invalid() && !din.is_invalid() && !cone_reaches_q(din, q)) {
    for (const auto& out : q.out_edges()) {
      din.connect_sink(out.sink);
    }
    latch.del_node();
    return;
  }

  auto open = latch_data_open_condition(latch, q, din, enable);
  if (q.is_invalid() || din.is_invalid() || enable.is_invalid() || !open.has_value()) {
    return;
  }
  auto posclk = livehd::graph_util::get_driver_of_sink_name(latch, "posclk");
  if (!posclk.is_invalid()) {
    auto positive = const_truth(posclk);
    if (!positive.has_value()) {
      return;  // dynamic/unknown polarity is not safe to simplify
    }
    if (!*positive) {
      open->true_when_base = !open->true_when_base;
    }
  }

  // Each rewrite removes one mux, including hold muxes nested under value
  // shaping nodes. Re-read din every round because deleting the immediate
  // driver reconnects the latch sink.
  for (int depth = 0; depth < 64; ++depth) {
    auto din_now = livehd::graph_util::get_driver_of_sink_name(latch, "din");
    auto hit     = find_latch_hold_mux(din_now, q, *open);
    if (!hit.has_value()) {
      break;
    }
    for (const auto& out : hit->mux.out_edges()) {
      hit->data.connect_sink(out.sink);
    }
    hit->mux.del_node();
  }

  // Collapse the usual boolean materialization on enable itself. Keep an
  // inverted materialization intact; changing it requires moving inversion
  // into posclk and is outside this deliberately narrow rewrite.
  auto raw_enable = decode_bool_condition(enable);
  if (raw_enable.has_value() && raw_enable->true_when_base && !same_pin(raw_enable->base, enable)) {
    auto enable_sink = livehd::graph_util::find_sink_pin(latch, "enable");
    enable_sink.del_sink();
    raw_enable->base.connect_sink(enable_sink);
  }
}

void Cprop::do_trans(const std::shared_ptr<hhds::Graph>& g) {
  if (!g) {
    return;
  }

  auto gio  = g->get_io();
  auto name = gio ? std::string{gio->get_name()} : std::string{};

  TRACE_EVENT("pass", nullptr, [&name](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, name));
  });

#ifndef NDEBUG
  // Invariant (-c dbg): every value-producing cell must carry a resolved width
  // at cprop ENTRY (i.e. as produced by tolg / upass generation). Checking at
  // this boundary covers BOTH
  // front-ends' tolg output (upass/tolg and inou/yosys/lgyosys_tolg). The lnast
  // tolg additionally self-checks at its own output (covers O0, where no graph
  // pass runs) -- see uPass_tolg::run.
  livehd::graph_util::debug_assert_cells_sized(*g, "tolg/upass (seen at cprop entry)");
#endif

  current_graph = g.get();
  // One topological construction, then local sweeps over that materialized
  // vector. Producer folds land before their consumers are visited, so mask
  // normalization and scalar propagation fuse into a single pass. CSE is the
  // only additional graph traversal; one forward round is sufficient because
  // every key sees already-folded producer pins.
  auto order = stable_nodes(current_graph);
  // Latch hold removal is NOT foldable into the sweep below. It matches a
  // `din = enable ? data : Q` shape, and hhds forward order only guarantees
  // that a loop_break node precedes ITS OWN dependents: Pass 1 emits sources
  // and already-ready combinational nodes interleaved in storage order, so a
  // latch at a high storage index is reached only after arbitrary comb nodes --
  // its own hold mux among them -- have already been rewritten by scalar_node,
  // and the pattern no longer matches. Re-walking `order` costs no graph
  // traversal (it is already materialized) and restores the pre-sweep
  // guarantee.
  for (auto node : order) {
    canonicalize_latch_hold(node);
  }
  std::vector<hhds::Node_class> pack_candidates;
  for (auto node : order) {
    if (node.is_invalid()) {
      continue;
    }
    canonicalize_and_mask(node);
    scalar_node(node);
    if (node.is_invalid()) {
      continue;
    }
    if (is_concat_pack_candidate(node)) {
      pack_candidates.push_back(node);
    }
  }
  // Fold the hand-spelled concat idioms into the cell that says it, AFTER the
  // sweep and CONSUMER-side first. Both orderings are load bearing:
  // scalar_get_mask_packed resolves reads by matching the Or/SHL/Set_mask
  // spelling and that fold is cycle-breaking, so it must see the original
  // shapes first; and the outermost link of a write chain is the one that must
  // become the Concat.
  for (auto it = pack_candidates.rbegin(); it != pack_candidates.rend(); ++it) {
    canonicalize_concat_pack(current_graph, *it);
  }
  // Merge duplicate nodes after the scalar/canonical folds (duplicated inlined
  // cones merge wholesale). Re-materialize the order: the sweep above both
  // deletes and MINTS cells, and a stale vector would hide every freshly
  // created twin from the hash-cons.
  cse_pass(stable_nodes(current_graph));
  enforce_lossless_carriers(current_graph);
  // The front ends sometimes spell an unconditional latch write as a
  // tautological enable cone (for example, a full case/default). The sweep
  // above reduces that cone to a constant; revisit the latches so the now
  // explicit always-open cell becomes ordinary combinational logic too.
  for (auto node : order) {
    canonicalize_latch_hold(node);
  }
  current_graph = nullptr;

#ifndef NDEBUG
  // Debug self-check (Tier 1, -c dbg): every constant left in the graph must be
  // consistent with the bits/sign attributes on its pin. This is the front line
  // for catching front-end translation misses (a const stamped the wrong width/sign).
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(node) != Ntype_op::Nconst) {
      continue;
    }
    livehd::graph_util::debug_check_const_pin(node.create_driver_pin(0));
  }
#endif
}

void Cprop::bwd_del_node(hhds::Node_class& node) {
  // Aggressive del: also remove single-user inputs that become dead.
  // WARNING: only call when all needed downstream edges have been added.

  I(!Ntype::is_loop_last(type_op_of(node)));

  absl::flat_hash_set<hhds::Class_index> potential_set;
  std::deque<hhds::Node_class>           potential;

  for (const auto& e : node.inp_edges()) {
    if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
      continue;
    }
    auto master = e.driver.get_master_node();
    // CONST_NODE (and other singletons) cannot be deleted: const pins are
    // leaves of the form CONST_NODE.pid_N, so dropping the consumer just
    // leaves the pin unreferenced — harmless and dedup-friendly.
    if (livehd::graph_util::is_builtin_node(master)) {
      continue;
    }
    if (potential_set.contains(master.get_class_index())) {
      continue;
    }
    potential.emplace_back(master);
    potential_set.insert(master.get_class_index());
  }

  node.del_node();

  while (!potential.empty()) {
    auto n = potential.front();
    potential.pop_front();

    if (n.is_invalid()) {
      continue;
    }

    if (!Ntype::is_loop_last(type_op_of(n)) && !n.has_out_edges()) {
      for (auto e : n.inp_edges()) {
        if (is_graph_input_pin(e.driver) || is_graph_output_pin(e.driver)) {
          continue;
        }
        auto d_master = e.driver.get_master_node();
        if (livehd::graph_util::is_builtin_node(d_master)) {
          continue;
        }
        if (potential_set.contains(d_master.get_class_index())) {
          continue;
        }
        potential.emplace_back(d_master);
        potential_set.insert(d_master.get_class_index());
      }
      n.del_node();
    }
  }
}
