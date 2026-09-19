// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sim_color_plan.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <print>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "file_output.hpp"
#include "hash_util.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "port_reach.hpp"
#include "worker_pool.hpp"

// HOW THIS FILE READS ITS INPUTS
//
// Every in-edge walk here is a nested pin walk: inp_sorted_pins() over the
// node, then the PLURAL get_driver_pins() over each sink. hhds::Occurrence_node
// offers both, and the retired inp_edges() reader was itself defined as exactly
// this nesting, so the order and the multiset of (sink, driver) pairs the
// planner sees are unchanged.
//
// Two parts of the spelling are load-bearing, not style:
//   * SORTED, never the raw inp_pins(). hhds stores port 0 as the node itself,
//     so the raw list OMITS it -- and port 0 is where a banked cell's first
//     operand lives. The raw list is unordered too, while inp_sorted_pins()
//     yields the node-as-pin first and then ascending port order, which is what
//     the retired edge walk saw.
//   * PLURAL get_driver_pins(), never the singular get_driver_pin(). A compact
//     loop's carry-in is the one sink livehd sanctions with two drivers (the
//     parent's seed plus the self edge, see pass/legalize's
//     verify_single_driver_sinks); the singular reader hands back one of them
//     and its assert compiles out under -DNDEBUG.
//
// Flattening one edge loop into two also moves what `break`/`continue` mean, so
// each site below keeps the ORIGINAL control flow explicitly: a `continue` that
// skipped one edge is an inner-loop `continue`, a `break` that left the whole
// walk still leaves both loops, and a counter that ticked once per edge ticks
// once per driver.
//
// Out-edges have no pin-centric twin yet, so out_edges() walks stay as they are.
namespace livehd::sim {

namespace {

namespace gu = livehd::graph_util;
namespace lc = livehd::latch_contract;

using Site_kind           = Color_plan::Site_kind;
using Dependency_kind     = Color_plan::Dependency_kind;
using Boundary_kind       = Color_plan::Boundary_kind;
using Boundary_consumer   = Color_plan::Boundary_consumer;
using Boundary_slot       = Color_plan::Boundary_slot;
using Execution_slot      = Color_plan::Execution_slot;
using State_version       = Color_plan::State_version;
using Value_use           = Color_plan::Value_use;
using Version_role        = Color_plan::Version_role;
using Occurrence_step_key = std::tuple<hhds::Gid, hhds::Nid, bool, uint64_t>;

constexpr std::pair<int, int> kPacked_footprint_bail{-1, -1};

hhds::Occurrence_pin occurrence_driver_at(const hhds::Occurrence_node& node, hhds::Port_id pid) {
  hhds::Occurrence_pin result;
  for (const auto& sink : node.inp_sorted_pins()) {
    if (sink.get_port_id() != pid) {
      continue;  // the same per-edge skip: every edge of this sink shares its port
    }
    for (const auto& driver : sink.get_driver_pins()) {
      if (!result.is_invalid()) {
        return {};  // only fixed single-driver operands are traceable
      }
      result = driver;
    }
  }
  return result;
}

int occurrence_const_shl_amount(const hhds::Occurrence_node& node) {
  const auto amount = occurrence_driver_at(node, 1);
  if (amount.is_invalid() || !amount.is_const()) {
    return -1;
  }
  const auto& value = gu::const_of(amount);
  if (!value.is_just_i64() || value.has_unknowns() || value.to_just_i64() < 0 || value.to_just_i64() > (1 << 28)) {
    return -1;
  }
  return static_cast<int>(value.to_just_i64());
}

// Sound over-approximation of the positions an occurrence pin may set. This
// is deliberately the same proof direction as cprop's packed-slice fold: an
// imprecise operand overlaps the requested lane and prevents refinement.
//
// `visits` bounds the TOTAL pins visited by one top-level query. The depth cap
// alone does not: an And/Or/Xor arm recurses into every operand, so a wide
// packed tree costs fanin^depth pin visits, and `discover` runs this once per
// operand of every crossing Or. Exhausting the budget bails, which is the same
// fail-closed answer an unbounded operand already produces.
//
// `cache` holds the subtree results that no depth or budget cut touched. Such
// a result is the pin's UNBOUNDED footprint -- the tightest answer any
// truncation can give -- so reusing it at another depth or under another
// remaining budget is sound (it can only make a later query more precise). A
// result below a cut depends on where its walk started and is never stored;
// `cut` reports one to the caller. Without this, every top-level query re-walked
// the shared cones of minion's packed words: 1.7 s of a 9 s cgen.
using Packed_footprint_cache = absl::flat_hash_map<hhds::Occurrence_pin, std::pair<int, int>>;

std::pair<int, int> occurrence_packed_footprint(const hhds::Occurrence_pin& pin, int depth, int& visits,
                                                Packed_footprint_cache* cache, bool& cut);

std::pair<int, int> occurrence_packed_footprint_uncached(const hhds::Occurrence_pin& pin, int depth, int& visits,
                                                         Packed_footprint_cache* cache, bool& cut) {
  if (pin.is_const()) {
    const auto& value = gu::const_of(pin);
    if (value.is_negative()) {
      return kPacked_footprint_bail;
    }
    if (value.has_unknowns()) {
      return {0, value.get_signed_bits()};
    }
    const int first = value.get_first_bit_set();
    const int last  = value.get_last_bit_set();
    return first < 0 || last < 0 ? std::pair<int, int>{0, 0} : std::pair<int, int>{first, last + 1};
  }
  if (gu::is_graph_input_pin(pin)) {
    const int bits = gu::bits_of(pin);
    return gu::is_unsign(pin) && bits > 0 ? std::pair<int, int>{0, bits} : kPacked_footprint_bail;
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  if (op == Ntype_op::SHL) {
    const int  shift = occurrence_const_shl_amount(node);
    const auto value = occurrence_driver_at(node, 0);
    const auto inner = occurrence_packed_footprint(value, depth + 1, visits, cache, cut);
    if (shift < 0 || inner.first < 0) {
      return kPacked_footprint_bail;
    }
    return inner.second <= inner.first ? std::pair<int, int>{0, 0} : std::pair<int, int>{inner.first + shift, inner.second + shift};
  }
  if (op == Ntype_op::Get_mask) {
    const auto mask = occurrence_driver_at(node, 2);
    if (mask.is_const()) {
      const auto& constant = gu::const_of(mask);
      const auto [lo, hi]  = constant.get_mask_range();
      if (!constant.has_unknowns() && constant.is_positive() && lo >= 0 && hi > lo) {
        const auto inner = occurrence_packed_footprint(occurrence_driver_at(node, 0), depth + 1, visits, cache, cut);
        if (inner.first >= 0) {
          const int clipped_lo = std::max(inner.first, lo);
          const int clipped_hi = std::min(inner.second, hi);
          return clipped_hi <= clipped_lo ? std::pair<int, int>{0, 0} : std::pair<int, int>{clipped_lo - lo, clipped_hi - lo};
        }
        return {0, hi - lo};
      }
    }
  }
  if (op == Ntype_op::And) {
    // The result of a bitwise And can be nonzero only where every operand can
    // be nonzero. One positive constant mask is therefore enough to bound a
    // wide field wrapper even when another operand has an unknown footprint.
    // Intersect every bound we can prove; ignoring an unbound operand only
    // widens the result and remains conservative.
    std::pair<int, int> bound = kPacked_footprint_bail;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& driver : sink.get_driver_pins()) {
        const auto operand = occurrence_packed_footprint(driver, depth + 1, visits, cache, cut);
        if (operand.first < 0) {
          continue;
        }
        if (operand.second <= operand.first) {
          return {0, 0};
        }
        if (bound.first < 0) {
          bound = operand;
        } else {
          bound.first  = std::max(bound.first, operand.first);
          bound.second = std::min(bound.second, operand.second);
          if (bound.second <= bound.first) {
            return {0, 0};
          }
        }
      }
    }
    if (bound.first >= 0) {
      return bound;
    }
  }
  if (op == Ntype_op::Or || op == Ntype_op::Xor) {
    // A nested pack is often another Or tree. Its possible support is the
    // union of its operands' supports; unlike the unique-owner query below,
    // every operand must be bounded before that union is useful. When one is
    // not, the union is unknown but this pin's own unsigned width (the generic
    // bound below) still holds: returning a bail here instead made XS Rob's
    // deqPtr bundle, whose 141-bit inner pack sits at <<179, look like it could
    // own ANY bit, so a 3-bit read at [94,97) found two owners and the word
    // kept a false memory -> pack -> memory-address cycle.
    std::pair<int, int> bound{std::numeric_limits<int>::max(), 0};
    bool                any     = false;
    bool                bounded = true;
    for (const auto& sink : node.inp_sorted_pins()) {
      for (const auto& driver : sink.get_driver_pins()) {
        const auto operand = occurrence_packed_footprint(driver, depth + 1, visits, cache, cut);
        if (operand.first < 0) {
          bounded = false;
          break;
        }
        if (operand.second <= operand.first) {
          continue;
        }
        bound.first  = std::min(bound.first, operand.first);
        bound.second = std::max(bound.second, operand.second);
        any          = true;
      }
      if (!bounded) {
        break;
      }
    }
    if (bounded) {
      return any ? bound : std::pair<int, int>{0, 0};
    }
  }
  const int bits = gu::bits_of(pin);
  return gu::is_unsign(pin) && bits > 0 ? std::pair<int, int>{0, bits} : kPacked_footprint_bail;
}

std::pair<int, int> occurrence_packed_footprint(const hhds::Occurrence_pin& pin, int depth, int& visits,
                                                Packed_footprint_cache* cache, bool& cut) {
  if (pin.is_invalid()) {
    return kPacked_footprint_bail;
  }
  if (cache != nullptr) {
    if (const auto it = cache->find(pin); it != cache->end()) {
      return it->second;
    }
  }
  if (depth > 16 || --visits < 0) {
    cut = true;
    return kPacked_footprint_bail;
  }
  bool       below_cut = false;
  const auto result    = occurrence_packed_footprint_uncached(pin, depth, visits, cache, below_cut);
  if (below_cut) {
    cut = true;
  } else if (cache != nullptr) {
    cache->emplace(pin, result);
  }
  return result;
}

// One top-level query: a fresh budget of 4096 pin visits from depth 0.
std::pair<int, int> occurrence_packed_footprint(const hhds::Occurrence_pin& pin, Packed_footprint_cache* cache) {
  int  visits = 4096;
  bool cut    = false;
  return occurrence_packed_footprint(pin, 0, visits, cache, cut);
}

Occurrence_step_key occurrence_step_key(const hhds::Occurrence_step& step) {
  return {step.subnode.gid, step.subnode.value, step.ordinal.has_value(), step.ordinal.value_or(0)};
}

std::string_view site_kind_name(Site_kind kind) {
  switch (kind) {
    case Site_kind::data               : return "data";
    case Site_kind::state              : return "state";
    case Site_kind::instance           : return "instance";
    case Site_kind::conditional_control: return "conditional-control";
    case Site_kind::loop_control       : return "loop-control";
  }
  return "invalid";
}

std::string_view dependency_kind_name(Dependency_kind kind) {
  switch (kind) {
    case Dependency_kind::data        : return "data";
    case Dependency_kind::state_read  : return "state-read";
    case Dependency_kind::state_update: return "state-update";
    case Dependency_kind::control     : return "control";
    case Dependency_kind::loop_carry  : return "loop-carry";
  }
  return "invalid";
}

std::string_view state_version_name(State_version version) {
  switch (version) {
    case State_version::pre_rise : return "pre-rise";
    case State_version::post_rise: return "post-rise";
    case State_version::post_fall: return "post-fall";
  }
  return "invalid";
}

std::string_view execution_slot_name(Execution_slot slot) {
  switch (slot) {
    case Execution_slot::pre_rise_eval    : return "pre-rise-eval";
    case Execution_slot::rise_commit      : return "rise-commit";
    case Execution_slot::post_rise_eval   : return "post-rise-eval";
    case Execution_slot::fall_commit      : return "fall-commit";
    case Execution_slot::post_fall_publish: return "post-fall-publish";
  }
  return "invalid";
}

std::string_view version_role_name(Version_role role) {
  switch (role) {
    case Version_role::data        : return "data";
    case Version_role::state_read  : return "state-read";
    case Version_role::state_update: return "state-update";
  }
  return "invalid";
}

std::string_view boundary_kind_name(Boundary_kind kind) {
  switch (kind) {
    case Boundary_kind::color_value       : return "color-value";
    case Boundary_kind::top_input         : return "top-input";
    case Boundary_kind::top_output        : return "top-output";
    case Boundary_kind::observation_input : return "observation-input";
    case Boundary_kind::observation_output: return "observation-output";
    case Boundary_kind::state_current     : return "state-current";
    case Boundary_kind::state_pending     : return "state-pending";
  }
  return "invalid";
}

Execution_slot evaluation_slot(State_version version) {
  switch (version) {
    case State_version::pre_rise : return Execution_slot::pre_rise_eval;
    case State_version::post_rise: return Execution_slot::post_rise_eval;
    case State_version::post_fall: return Execution_slot::post_fall_publish;
  }
  return Execution_slot::pre_rise_eval;
}

bool is_true_constant(const hhds::Occurrence_pin& pin) { return pin.is_known_true(); }

std::optional<hhds::Port_id> valid_port(const hhds::Node_class& node) {
  auto io = node.get_subnode_io();
  if (!io) {
    return std::nullopt;
  }
  for (const auto& decl : io->get_input_pin_decls()) {
    if (decl.name == "__valid") {
      return decl.port_id;
    }
  }
  return std::nullopt;
}

std::optional<hhds::Port_id> valid_port(const hhds::Occurrence_node& node) { return valid_port(node.base_node()); }

bool is_conditional_boundary(const hhds::Occurrence_node& node) {
  const auto port = valid_port(node);
  if (!port) {
    return false;
  }
  hhds::Occurrence_pin guard;
  for (const auto& sink : node.inp_sorted_pins()) {
    if (sink.get_port_id() != *port) {
      continue;
    }
    // __valid is a single-driver control port. The plural reader is still what
    // asks the graph; taking the first driver is the same stop the edge walk
    // made, and the outer break is the one that left the WHOLE walk.
    for (const auto& driver : sink.get_driver_pins()) {
      guard = driver;
      break;
    }
    break;
  }
  if (guard.is_invalid() || is_true_constant(guard)) {
    return false;
  }
  // A definition's own top-level __valid forwarded into an unconditional
  // child is already guarded by the enclosing boundary. It is not a second
  // condition region.
  const auto root = lc::control_root(guard).net;
  return root.is_invalid() || !gu::is_graph_input_pin(root) || gu::pin_name_of(root) != "__valid";
}

bool is_conditional_boundary(const hhds::Instance_site& site) {
  const auto port = valid_port(site.base_node());
  if (!port) {
    return false;
  }
  hhds::Pin_class guard;
  for (const auto& sink : site.base_node().inp_sorted_pins()) {
    if (sink.get_port_id() != *port) {
      continue;
    }
    // Same shape as the Occurrence_node twin above: the PLURAL reader asks the
    // graph and the first driver is the one the edge walk stopped at. The
    // singular get_driver_pin() would assert away in a release build instead.
    for (const auto& driver : sink.get_driver_pins()) {
      guard = driver;
      break;
    }
    break;
  }
  if (guard.is_invalid() || (guard.is_known_true())) {
    return false;
  }
  const auto root = lc::control_root(guard).net;
  return root.is_invalid() || !gu::is_graph_input_pin(root) || gu::pin_name_of(root) != "__valid";
}

Site_kind classify(const hhds::Occurrence_node& node) {
  if (node.is_loop_subnode()) {
    return Site_kind::loop_control;
  }
  if (gu::type_op_of(node) == Ntype_op::Sub) {
    return is_conditional_boundary(node) ? Site_kind::conditional_control : Site_kind::instance;
  }
  if (gu::is_type_register(node) || gu::type_op_of(node) == Ntype_op::Memory) {
    return Site_kind::state;
  }
  return Site_kind::data;
}

// sim.tune class weight `cost` of one node's OWN emitted code: the 64-bit words
// of its widest output -- a memory: its data width, never its whole-array
// readall surface. Never 0: a zero-GE wiring node (Get_mask, Set_mask, Concat)
// is still emitted code, and an unstamped width floors at one word.
uint64_t sim_word_cost(const hhds::Node_class& node) {
  uint64_t   bits = 0;
  const auto op   = gu::type_op_of(node);
  if (op == Ntype_op::Memory) {
    const auto bits_pid = Ntype::get_sink_pid(Ntype_op::Memory, "bits");
    for (const auto& sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() != bits_pid) {
        continue;
      }
      for (const auto& driver : sink.get_driver_pins()) {
        if (!driver.is_const()) {
          continue;
        }
        const auto& value = gu::const_of(driver);
        if (value.is_just_i64() && !value.has_unknowns() && value.to_just_i64() > 0) {
          bits = std::max<uint64_t>(bits, static_cast<uint64_t>(value.to_just_i64()));
        }
      }
    }
  }
  if (bits == 0) {
    if (const auto width = gu::bits_of(node.create_driver_pin(0)); width > 0) {
      bits = static_cast<uint64_t>(width);
    }
    for (const auto& driver : node.out_sorted_pins()) {
      if (op == Ntype_op::Memory && driver.get_port_id() == Ntype::Memory_readall_pid) {
        continue;
      }
      if (const auto width = gu::bits_of(driver); width > 0) {
        bits = std::max<uint64_t>(bits, static_cast<uint64_t>(width));
      }
    }
  }
  return std::max<uint64_t>(1, (bits + 63) / 64);
}

// Second-lane parameters for the 128-bit structural digests below; the low
// lane is the shared FNV-1a.
constexpr uint64_t kHiLaneBasis = 7809847782465536322ULL;
constexpr uint64_t kHiLanePrime = 14029467366897019727ULL;

class Shape_hash_builder {
public:
  void append_u64(uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
      append_byte(static_cast<uint8_t>((value >> (byte * 8)) & 0xffU));
    }
  }

  void append_text(std::string_view text) {
    append_u64(text.size());
    for (const unsigned char byte : text) {
      append_byte(byte);
    }
  }

  [[nodiscard]] std::array<uint64_t, 2> finish() const { return {lo_, hi_}; }

private:
  void append_byte(uint8_t byte) {
    lo_ ^= byte;
    lo_ *= livehd::hash_util::kFnv1a64_prime;
    hi_ ^= static_cast<uint64_t>(byte) + 0x9e;
    hi_ *= kHiLanePrime;
    hi_ ^= hi_ >> 29;
  }

  uint64_t lo_ = livehd::hash_util::kFnv1a64_offset;
  uint64_t hi_ = kHiLaneBasis;
};

// Structural refinement feeds only fixed-width integers into the digest, and
// does so tens of millions of times for a large flattened hierarchy. Keep its
// mixer word-oriented instead of paying Shape_hash_builder's byte-at-a-time
// text-compatible cost. The final avalanche keeps the two lanes independent;
// as with the other structural hashes, equality is the only semantic use.
class Refinement_hash_builder {
public:
  void append_u64(uint64_t value) {
    lo_ ^= value + 0x9e3779b97f4a7c15ULL + rotate_left(hi_, 17);
    lo_  = rotate_left(lo_, 27) * 0x3c79ac492ba7b653ULL;
    hi_ ^= value + 0x1c69b3f74ac4ae35ULL + rotate_left(lo_, 31);
    hi_  = rotate_left(hi_, 33) * 0x1c69b3f74ac4ae35ULL;
  }

  [[nodiscard]] std::array<uint64_t, 2> finish() const { return {avalanche(lo_), avalanche(hi_ ^ lo_)}; }

private:
  static uint64_t rotate_left(uint64_t value, unsigned shift) { return std::rotl(value, static_cast<int>(shift)); }

  static uint64_t avalanche(uint64_t value) {
    value ^= value >> 27;
    value *= 0x3c79ac492ba7b653ULL;
    value ^= value >> 33;
    value *= 0x1c69b3f74ac4ae35ULL;
    value ^= value >> 27;
    return value;
  }

  uint64_t lo_ = livehd::hash_util::kFnv1a64_offset;
  uint64_t hi_ = kHiLaneBasis;
};

// Fold records without ordering them. Fields within a record remain positional;
// records form a multiset, including duplicate edges and their sink-port roles.
template <typename Hash, typename Range, typename Append>
void append_multiset(Hash& hash, const Range& records, Append append) {
  hhds::Commutative_combiner128 terms;
  for (const auto& record : records) {
    Hash term;
    append(term, record);
    const auto words = term.finish();
    terms.add(hhds::Sig128{words[0], words[1]});
  }
  const auto combined = terms.value();
  hash.append_u64(records.size());
  hash.append_u64(combined.a);
  hash.append_u64(combined.b);
}

std::string format_hash128(std::string_view prefix, const std::array<uint64_t, 2>& hash) {
  char      text[40];
  const int size = std::snprintf(text,
                                 sizeof text,
                                 "%.*s%016llx%016llx",
                                 static_cast<int>(prefix.size()),
                                 prefix.data(),
                                 static_cast<unsigned long long>(hash[0]),
                                 static_cast<unsigned long long>(hash[1]));
  return std::string{text, static_cast<size_t>(size)};
}

// A name-free structural seed used only for discovery-site identity. Canonical
// kernel identity is a later, stricter lockstep-verified contract. Hash typed,
// length-delimited fields directly: building and then discarding a formatted
// description for every occurrence dominated planner time on large hierarchies.
// The walk below MUST offer every driver of a sink pin. This is a CACHE
// KEY, and the one shape hhds still sanctions with two drivers on one sink pin
// (a compact loop's carry-in: the seed plus the self edge meaning "the previous
// ordinal", see pass/legalize's verify_single_driver_sinks) loses a term to any
// reader that takes only one. Two loop subnodes differing only in their seed
// would then digest identically, and a false cache hit here serves the wrong
// generated C++.
//
// It reads inp_sorted_pins() + the PLURAL get_driver_pins(), which satisfies
// that: SORTED keeps port 0 (the node-as-pin, where a banked cell's first
// operand lives) and the port ordering that the raw inp_pins() drops, and the
// PLURAL reader keeps the second driver that get_driver_pin() would discard.
// Both are correctness, not style.
std::string node_shape(const hhds::Node_class& node) {
  Shape_hash_builder hash;
  hash.append_u64(1);
  hash.append_u64(static_cast<uint64_t>(gu::type_op_of(node)));

  using Input_shape = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, std::string>;
  std::vector<Input_shape> inputs;
  for (auto edge_sink : node.inp_sorted_pins()) {
    for (auto edge_drv : edge_sink.get_driver_pins()) {
      uint64_t    source_kind = 2;
      uint64_t    source_op   = 0;
      std::string literal;
      if (edge_drv.is_const()) {
        source_kind = 0;
        literal     = gu::const_of(edge_drv).to_string();
      } else if (gu::is_graph_input_pin(edge_drv)) {
        source_kind = 1;
      } else {
        source_op = static_cast<uint64_t>(gu::type_op_of(edge_drv.get_master_node()));
      }
      inputs.emplace_back(edge_sink.get_port_id(),
                          source_kind,
                          source_op,
                          edge_drv.get_port_id(),
                          static_cast<uint64_t>(gu::bits_of(edge_drv)),
                          gu::is_unsign(edge_drv),
                          std::move(literal));
    }
  }
  append_multiset(hash, inputs, [](auto& term, const auto& input) {
    const auto& [sink, source_kind, source_op, source_port, bits, unsign, literal] = input;
    term.append_u64(sink);
    term.append_u64(source_kind);
    term.append_u64(source_op);
    term.append_u64(source_port);
    term.append_u64(bits);
    term.append_u64(unsign);
    term.append_text(literal);
  });

  std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> outputs;
  for (const auto& pin : node.out_pins()) {
    outputs.emplace_back(pin.get_port_id(), static_cast<uint64_t>(gu::bits_of(pin)), gu::is_unsign(pin));
  }
  append_multiset(hash, outputs, [](auto& term, const auto& record) {
    const auto& [port, bits, unsign] = record;
    term.append_u64(port);
    term.append_u64(bits);
    term.append_u64(unsign);
  });

  if (auto loop = node.subnode_loop()) {
    hash.append_u64(1);
    hash.append_u64(static_cast<uint64_t>(loop->first));
    hash.append_u64(static_cast<uint64_t>(loop->step));
    hash.append_u64(loop->count);
    const auto append_port = [&](const std::optional<hhds::Port_id>& port) {
      hash.append_u64(port.has_value());
      hash.append_u64(port.value_or(0));
    };
    append_port(loop->index_input);
    append_port(loop->activation_input);
    append_port(loop->next_active_output);
    std::vector<std::pair<uint64_t, uint64_t>> carries;
    for (const auto& carry : node.subnode_group().carries()) {
      carries.emplace_back(carry.input_port(), carry.output_port());
    }
    append_multiset(hash, carries, [](auto& term, const auto& record) {
      const auto& [input, output] = record;
      term.append_u64(input);
      term.append_u64(output);
    });
  } else {
    hash.append_u64(0);
  }

  if (auto io = node.get_subnode_io()) {
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> ports;
    for (const auto& decl : io->get_input_pin_decls()) {
      ports.emplace_back(0, decl.port_id, static_cast<uint64_t>(decl.bits), decl.loop_break);
    }
    for (const auto& decl : io->get_output_pin_decls()) {
      ports.emplace_back(1, decl.port_id, static_cast<uint64_t>(decl.bits), decl.loop_break);
    }
    append_multiset(hash, ports, [](auto& term, const auto& record) {
      const auto& [direction, port, bits, loop_break] = record;
      term.append_u64(direction);
      term.append_u64(port);
      term.append_u64(bits);
      term.append_u64(loop_break);
    });
  } else {
    hash.append_u64(0);
  }
  return format_hash128("n:", hash.finish());
}

struct Occurrence_shape_cache {
  absl::flat_hash_map<hhds::Definition_index, std::string> definition_nodes;
  absl::flat_hash_map<hhds::Occurrence_path, std::string>  paths;
};

const std::string& cached_node_shape(const hhds::Node_class& node, Occurrence_shape_cache& cache) {
  const auto key = node.get_definition_index();
  if (const auto found = cache.definition_nodes.find(key); found != cache.definition_nodes.end()) {
    return found->second;
  }
  return cache.definition_nodes.emplace(key, node_shape(node)).first->second;
}

// Kernel-local emission shape. Unlike discovery identity, an external value's
// occurrence/port is a binding and must not poison reuse: two identical cores
// behind different top inputs or ICG nets share code. Constants, sink roles,
// widths/signs, loop descriptors, and sub interfaces remain part of the shape.
// This is the exact collision-check representation, not a hash-only discovery
// seed. Keep its canonical ordering when changing the commutative seed hash.
// Same reason as node_shape above: a collision-check representation must not
// drop the second driver of a compact loop's carry-in sink, so this reads
// inp_sorted_pins() + the PLURAL get_driver_pins() and never the singular
// get_driver_pin() or the unordered, port-0-less inp_pins().
std::string kernel_node_shape(const hhds::Node_class& node) {
  std::string              result = std::format("op:{}", Ntype::get_name(gu::type_op_of(node)));
  std::vector<std::string> inputs;
  for (auto edge_sink : node.inp_sorted_pins()) {
    for (auto edge_drv : edge_sink.get_driver_pins()) {
      const std::string source = edge_drv.is_const() ? "const:" + gu::const_of(edge_drv).to_string()
                                                     : std::format("value:b{}:u{}", gu::bits_of(edge_drv), gu::is_unsign(edge_drv));
      inputs.push_back(std::format("{}<-{}", edge_sink.get_port_id(), source));
    }
  }
  std::ranges::sort(inputs);
  for (const auto& input : inputs) {
    result += "|i:" + input;
  }
  std::vector<std::string> outputs;
  for (const auto& pin : node.out_pins()) {
    outputs.push_back(std::format("p{}:b{}:u{}", pin.get_port_id(), gu::bits_of(pin), gu::is_unsign(pin)));
  }
  std::ranges::sort(outputs);
  for (const auto& output : outputs) {
    result += "|o:" + output;
  }
  if (auto loop = node.subnode_loop()) {
    const auto opt_port  = [](const std::optional<hhds::Port_id>& port) { return port ? std::to_string(*port) : std::string("-"); };
    result              += std::format("|loop:{},{},{}:{},{},{}",
                                       loop->first,
                                       loop->step,
                                       loop->count,
                                       opt_port(loop->index_input),
                                       opt_port(loop->activation_input),
                                       opt_port(loop->next_active_output));
    for (const auto& carry : node.subnode_group().carries()) {
      result += std::format("|carry:{}<-{}", carry.input_port(), carry.output_port());
    }
  }
  if (auto io = node.get_subnode_io()) {
    std::vector<std::string> ports;
    for (const auto& decl : io->get_input_pin_decls()) {
      ports.push_back(std::format("i:{}:{}:{}", decl.port_id, decl.bits, decl.loop_break));
    }
    for (const auto& decl : io->get_output_pin_decls()) {
      ports.push_back(std::format("o:{}:{}:{}", decl.port_id, decl.bits, decl.loop_break));
    }
    std::ranges::sort(ports);
    for (const auto& port : ports) {
      result += "|p:" + port;
    }
  }
  return result;
}

std::array<uint64_t, 2> stable_hash128(std::string_view text) {
  Shape_hash_builder hash;
  hash.append_text(text);
  return hash.finish();
}

std::string stable_id(std::string_view text) { return format_hash128("s:", stable_hash128(text)); }

std::string occurrence_shape(const hhds::Occurrence_node& node, const hhds::GraphLibrary* library, Occurrence_shape_cache& cache) {
  auto [path_it, fresh] = cache.paths.try_emplace(node.path());
  if (fresh) {
    std::string& path_shape = path_it->second;
    path_shape              = "root";
    if (library != nullptr) {
      for (const auto& step : node.path().steps()) {
        auto parent = library->get_graph(step.subnode.gid);
        if (!parent) {
          path_shape += "/missing";
          continue;
        }
        auto site   = parent->get_node(hhds::Class_index{step.subnode.value});
        path_shape += "/" + cached_node_shape(site, cache);
        if (step.ordinal) {
          path_shape += std::format("@{}", *step.ordinal);
        } else if (site.is_loop_subnode()) {
          path_shape += "@group";
        }
      }
    }
  }
  std::string shape  = path_it->second;
  shape             += "/" + cached_node_shape(node.base_node(), cache);
  return shape;
}

// The pin pair is the primary spelling: in-edge callers hand over the sink they
// are iterating and one of ITS drivers, never an assembled edge. The
// edge-taking overload stays for the out_edges() walk, which has no
// pin-centric twin yet.
bool is_loop_carry(const hhds::Occurrence_node& node, const hhds::Occurrence_pin& driver, const hhds::Occurrence_pin& sink) {
  if (!node.is_loop_subnode() || driver.get_master_node() != node || sink.get_master_node() != node) {
    return false;
  }
  for (const auto& carry : node.subnode_group().carries()) {
    if (carry.output_port() == driver.get_port_id() && carry.input_port() == sink.get_port_id()) {
      return true;
    }
  }
  return false;
}

bool is_loop_carry(const hhds::Occurrence_node& node, const hhds::Occurrence_edge& edge) {
  return is_loop_carry(node, edge.driver, edge.sink);
}

// Only the SINK decides this one, so it takes the pin the walk is standing on
// and never needs a driver at all.
bool is_loop_carry_input(const hhds::Occurrence_node& node, const hhds::Occurrence_pin& sink) {
  if (!node.is_loop_subnode()) {
    return false;
  }
  return std::ranges::any_of(node.subnode_group().carries(),
                             [&](const auto& carry) { return carry.input_port() == sink.get_port_id(); });
}

void refine_structural_ids(std::vector<Color_plan::Site>&                             sites,
                           const absl::flat_hash_map<hhds::Occurrence_index, size_t>& occurrence_index) {
  if (sites.empty()) {
    return;
  }
  std::vector<std::array<uint64_t, 2>> seeds;
  seeds.reserve(sites.size());
  for (const auto& site : sites) {
    seeds.push_back(stable_hash128(site.structural_id));
  }

  const auto classes_of = [](const std::vector<std::array<uint64_t, 2>>& descriptions) {
    std::vector<size_t> order(descriptions.size());
    std::vector<size_t> scratch(descriptions.size());
    std::iota(order.begin(), order.end(), 0);
    // descriptions are fixed-width hashes. An LSD radix sort retains the
    // exact unsigned lexicographic ordering used by std::array::operator<,
    // while avoiding an N log N comparison sort on every refinement round.
    for (int word = 1; word >= 0; --word) {
      for (unsigned shift = 0; shift != 64; shift += 8) {
        std::array<size_t, 256> counts{};
        for (const size_t index : order) {
          ++counts[(descriptions[index][word] >> shift) & 0xffU];
        }
        size_t position = 0;
        for (auto& count : counts) {
          const size_t bucket_size  = count;
          count                     = position;
          position                 += bucket_size;
        }
        for (const size_t index : order) {
          scratch[counts[(descriptions[index][word] >> shift) & 0xffU]++] = index;
        }
        order.swap(scratch);
      }
    }
    std::vector<uint32_t> classes(descriptions.size());
    uint32_t              next_class = 0;
    for (size_t position = 0; position < order.size(); ++position) {
      if (position != 0 && descriptions[order[position - 1]] != descriptions[order[position]]) {
        ++next_class;
      }
      classes[order[position]] = next_class;
    }
    return classes;
  };

  std::vector<uint32_t>                classes      = classes_of(seeds);
  std::vector<std::array<uint64_t, 2>> descriptions = seeds;
  const auto same_partition = [](const std::vector<uint32_t>& lhs, const std::vector<uint32_t>& rhs) {
    I(lhs.size() == rhs.size());
    if (lhs.empty()) {
      return true;
    }
    const auto            lhs_count = *std::ranges::max_element(lhs) + 1;
    const auto            rhs_count = *std::ranges::max_element(rhs) + 1;
    constexpr uint32_t    unmapped  = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> lhs_to_rhs(lhs_count, unmapped);
    std::vector<uint32_t> rhs_to_lhs(rhs_count, unmapped);
    for (size_t i = 0; i < lhs.size(); ++i) {
      auto& mapped_rhs = lhs_to_rhs[lhs[i]];
      auto& mapped_lhs = rhs_to_lhs[rhs[i]];
      if ((mapped_rhs != unmapped && mapped_rhs != rhs[i]) || (mapped_lhs != unmapped && mapped_lhs != lhs[i])) {
        return false;
      }
      mapped_rhs = rhs[i];
      mapped_lhs = lhs[i];
    }
    return true;
  };
  // Weisfeiler-Lehman-style partition refinement. Port roles and root IO port
  // ids are structural anchors; raw graph indices and user names never enter.
  // True automorphisms intentionally remain one symmetry class -- swapping two
  // indistinguishable anonymous nodes cannot change any reported dependence.
  struct Neighbor {
    std::array<uint64_t, 11> fields{};
    size_t                   site = Color_plan::invalid_index;
  };
  std::vector<std::vector<Neighbor>> adjacency(sites.size());
  for (size_t i = 0; i < sites.size(); ++i) {
    auto& neighbors = adjacency[i];
    // reserve() is a capacity hint: one sink usually carries one driver, and
    // the compact-loop carry-in that carries two just costs a regrow.
    neighbors.reserve(sites[i].node.inp_sorted_pins().size() + sites[i].node.out_edges().size());
    for (const auto& edge_sink : sites[i].node.inp_sorted_pins()) {
      for (const auto& edge_drv : edge_sink.get_driver_pins()) {
        if (is_loop_carry(sites[i].node, edge_drv, edge_sink)) {
          continue;
        }
        Neighbor   neighbor;
        auto&      fields = neighbor.fields;
        const auto it     = occurrence_index.find(edge_drv.get_master_node().get_occurrence_index());
        fields[0]         = 0;
        fields[1]         = edge_sink.get_port_id();
        if (it != occurrence_index.end()) {
          neighbor.site = it->second;
          fields[4]     = edge_drv.get_port_id();
        } else {
          const auto pin = edge_drv.base_pin();
          fields[2]      = 1;
          fields[4]      = pin.is_invalid() ? 0 : pin.get_port_id();
          fields[5]      = 3;
          if (pin.is_const()) {
            fields[5]        = 0;
            const auto value = stable_hash128(gu::const_of(pin).to_string());
            fields[9]        = value[0];
            fields[10]       = value[1];
          } else if (!pin.is_invalid() && gu::is_graph_input_pin(pin)) {
            fields[5] = 1;
          } else if (!pin.is_invalid()) {
            fields[5] = 2;
            fields[6] = static_cast<uint64_t>(gu::type_op_of(pin.get_master_node()));
          }
          fields[7] = pin.is_invalid() ? 0 : static_cast<uint64_t>(gu::bits_of(pin));
          fields[8] = !pin.is_invalid() && gu::is_unsign(pin);
        }
        neighbors.push_back(std::move(neighbor));
      }
    }
    for (const auto& edge : sites[i].node.out_edges()) {
      if (is_loop_carry(sites[i].node, edge)) {
        continue;
      }
      Neighbor   neighbor;
      auto&      fields = neighbor.fields;
      const auto it     = occurrence_index.find(edge.sink.get_master_node().get_occurrence_index());
      fields[0]         = 1;
      fields[1]         = edge.driver.get_port_id();
      if (it != occurrence_index.end()) {
        neighbor.site = it->second;
        fields[4]     = edge.sink.get_port_id();
      } else if (edge.sink.get_master_node().is_output_node()) {
        fields[2] = 2;
        fields[4] = edge.sink.get_port_id();
      } else {
        continue;
      }
      neighbors.push_back(std::move(neighbor));
    }
  }
  // One round hashes every site's neighbor multiset, reading only the previous
  // round's classes: the sites are independent, so a large plan splits them
  // across workers and gets the SAME descriptions. MEASURED on minion (Pyrope):
  // 202k sites with 12.4M neighbor records, 13 rounds, 6.3 s serial (battery).
  const auto refine_sites = [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      auto& neighbors = adjacency[i];
      for (auto& neighbor : neighbors) {
        if (neighbor.site != Color_plan::invalid_index) {
          neighbor.fields[3] = classes[neighbor.site];
        }
      }
      Refinement_hash_builder hash;
      hash.append_u64(seeds[i][0]);
      hash.append_u64(seeds[i][1]);
      append_multiset(hash, neighbors, [](auto& term, const auto& neighbor) {
        for (const uint64_t field : neighbor.fields) {
          term.append_u64(field);
        }
      });
      descriptions[i] = hash.finish();
    }
  };
  size_t neighbor_records = 0;
  for (const auto& neighbors : adjacency) {
    neighbor_records += neighbors.size();
  }
  constexpr size_t kParallelRecords = size_t{1} << 18;
  constexpr size_t kSitesPerClaim   = 1024;
  const size_t     workers
      = neighbor_records < kParallelRecords
            ? 1
            : std::min<size_t>({std::max<size_t>(1, std::thread::hardware_concurrency()), 16, (sites.size() + kSitesPerClaim - 1) / kSitesPerClaim});
  for (size_t round = 0; round < std::min<size_t>(sites.size() + 1, 32); ++round) {
    if (workers <= 1) {
      refine_sites(0, sites.size());
    } else {
      std::atomic<size_t> next_site{0};
      livehd::run_workers(workers, [&](size_t) {
        for (;;) {
          const size_t begin = next_site.fetch_add(kSitesPerClaim, std::memory_order_relaxed);
          if (begin >= sites.size()) {
            break;
          }
          refine_sites(begin, std::min(begin + kSitesPerClaim, sites.size()));
        }
      });
    }
    auto next = classes_of(descriptions);
    // Class numbers follow hash-sort order, so an unchanged partition can
    // acquire a permutation of its numeric labels from one round to the next.
    // Comparing the vectors directly then burns every one of the 32 rounds on
    // an already-stable graph. Convergence is equality of the equivalence
    // relation: each old class maps to exactly one new class and vice versa.
    if (same_partition(next, classes)) {
      break;
    }
    classes = std::move(next);
  }

  for (size_t i = 0; i < sites.size(); ++i) {
    sites[i].structural_id = format_hash128("s:", descriptions[i]);
  }
}

void count_grouped_structure(hhds::Graph* graph, absl::flat_hash_set<hhds::Gid>& active, uint64_t& count, bool& complete,
                             std::vector<std::string>& errors) {
  if (graph == nullptr) {
    return;
  }
  if (!active.insert(graph->get_gid()).second) {
    complete = false;
    errors.emplace_back("recursive instantiation is unsupported by the occurrence-wide simulator plan");
    return;
  }
  for (const auto node : graph->body().nodes()) {
    if (count != std::numeric_limits<uint64_t>::max()) {
      ++count;
    }
    if (gu::type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    if (node.is_loop_subnode()) {
      continue;  // one executable control site owns its analyzed native body
    }
    if (auto child = node.get_subnode_graph()) {
      count_grouped_structure(child.get(), active, count, complete, errors);
    }
  }
  active.erase(graph->get_gid());
}

std::string quote(std::string_view text) {
  std::string result = "\"";
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      result += '\\';
    }
    result += c;
  }
  result += '"';
  return result;
}

bool is_timing_input(const hhds::Occurrence_pin& sink) {
  const auto op      = gu::type_op_of(sink.get_master_node());
  const auto pid     = sink.get_port_id();
  const auto is_port = [&](std::string_view role) { return pid == Ntype::get_sink_pid(op, role); };
  if (op == Ntype_op::Latch) {
    // A latch enable is also DATA: a data-gated latch must rerun its update
    // color when that value changes. Pure clock-window latches may carry the
    // extra dependency, but their emitter suppresses the already-scheduled
    // phase predicate. Only dedicated timing metadata is excluded here.
    return is_port("clock_pin") || is_port("posclk");
  }
  if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Memory) {
    // clock_pin may be a secondary/data-driven clock. Its current level then
    // participates in the edge test against persistent previous-level state,
    // so it is a real value dependency. Reference-clock users carry one
    // conservative extra dependency and ignore the value in their kernel.
    return is_port("posclk");
  }
  return false;
}

// The state update's data version. `true` means the rising-edge commit and
// `false` the falling-edge commit. A clock-role latch commits at the edge that
// CLOSES its transparent window: !clk is transparent-low and closes on rise;
// clk is transparent-high and closes on fall. The latter therefore reads
// post-rise flop state, which is the event-semantics ruling for 3d-sim-color.
std::optional<bool> update_on_rise(const hhds::Occurrence_node& node, const lc::Design_clocks& clocks, bool& versioning_complete) {
  const auto op = gu::type_op_of(node);
  if (op == Ntype_op::Memory) {
    // Memory timing is PER PORT: raw sink ids are laid out in 16-pin groups,
    // so there may be no unsuffixed/port-zero clock even though later write
    // ports are registered. Treating such an array as combinational puts its
    // read value and write-input cone in one data version and manufactures a
    // read -> address/data -> Memory -> read cycle (XS Rob's robDeqGroup).
    // Per-port pins live at `port * Memory_port_stride + <base offset>`, and
    // `clock_pin` is base offset 2 (graph/cell.cpp). Test the raw id directly:
    // get_sink_name allocates a std::string per sink on a hot per-node path.
    // The question is per SINK, so this only needs the driver list to be
    // non-empty -- an unconnected clock pin contributed no edge either.
    const auto clock_off = Ntype::get_sink_pid(Ntype_op::Memory, "clock_pin");
    bool       has_clock = false;
    for (const auto& sink : node.inp_sorted_pins()) {
      if (sink.get_port_id() % Ntype::Memory_port_stride == clock_off && !sink.get_driver_pins().empty()) {
        has_clock = true;
        break;
      }
    }
    if (!has_clock) {
      return std::nullopt;  // combinational arrays are data colors, not state actions
    }
    const auto posclk = lc::sink_driver_hier(node, "posclk");
    if (posclk.is_known_false()) {
      // Today's emitter is rise-only for memories. Keep that behavior visible
      // as an incomplete edge-class decision instead of silently treating a
      // secondary/falling memory exactly like a flop.
      versioning_complete = false;
    }
    return true;
  }
  if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
    const auto posclk   = lc::sink_driver_hier(node, "posclk");
    const bool positive = !posclk.is_known_false();
    const auto clock    = lc::sink_driver_hier(node, "clock_pin");
    if (!clock.is_invalid()) {
      if (const auto cone = lc::clock_op_of(clock.base_pin(), clocks)) {
        if (cone->div != 1) {
          versioning_complete = false;
          return std::nullopt;
        }
        return positive != cone->clock_inverted;
      }
      const auto root = lc::control_root(clock);
      if (!root.net.is_invalid() && clocks.is_clock(root.net)) {
        // A posedge endpoint on an inverted structural clock closes on the
        // design-wide fall; a negedge endpoint on that net closes on the rise.
        return positive != root.inverted;
      }
    }
    return positive;
  }
  if (op != Ntype_op::Latch) {
    return std::nullopt;
  }

  const auto enable = lc::sink_driver_hier(node, "enable");
  if (enable.is_invalid() || (enable.is_known_true())) {
    // An always-open latch is a combinational buffer, not a staged state
    // update. The future emitter must lower that role explicitly.
    versioning_complete = false;
    return std::nullopt;
  }
  if (enable.is_known_false()) {
    return std::nullopt;  // permanently held state
  }
  const auto root = lc::control_root(enable);
  if (!root.net.is_invalid() && clocks.is_clock(root.net)) {
    return root.inverted;  // !clk closes on rise; clk closes on fall
  }
  return true;  // data-gated latch: the established end-of-period update slot
}

}  // namespace

// Live machine words a color may keep alive across its members lives on the
// PLAN (`live_word_budget_`, set here from `sim.tune.live_words`). THE single
// definition still: the coarsener below enforces that member and report()
// prints that member, so a change can never desynchronize the plan report from
// the plan -- and, unlike a global, a second discover() at another budget
// cannot retroactively rewrite what an earlier plan reports. The default was 20
// until 2026-09-13, when measuring showed a boundary slot (store + compare +
// dirty mark per value) costs more than the register pressure it avoids: 256
// words gave minion 1.75x, matched_filter 1.15x, RenameTable -4%.
Color_plan Color_plan::discover(hhds::Graph* root, bool include_observations, bool separate_runtime_calls, uint64_t live_words,
                                int64_t fence_ratio) {
  Color_plan plan;
  plan.live_word_budget_ = live_words > 0 ? live_words : kDefaultLiveWords;
  if (root == nullptr) {
    plan.summary_.complete = false;
    plan.errors_.emplace_back("null simulation root");
    return plan;
  }

  plan.outer_policy_     = std::make_shared<Policy>([](const hhds::Instance_site& site) {
    if (site.is_loop() || is_conditional_boundary(site)) {
      return hhds::Instance_action::opaque;
    }
    return hhds::Instance_action::descend;
  });
  plan.discovery_policy_ = std::make_shared<Policy>([](const hhds::Instance_site& site) {
    return site.is_loop() ? hhds::Instance_action::opaque : hhds::Instance_action::descend;
  });

  // TWO distinct view states are required: verdicts are memoized inside a
  // view and cannot be revised from opaque discovery to body descent.
  auto outer = root->grouped_hierarchy(*plan.outer_policy_);
  for (const auto& node : outer.nodes()) {
    plan.outer_nodes_.push_back(node);
  }
  plan.summary_.outer_sites = plan.outer_nodes_.size();

  auto                   discovery = root->grouped_hierarchy(*plan.discovery_policy_);
  auto                   io        = root->get_io();
  auto*                  library   = io ? io->get_library() : nullptr;
  Occurrence_shape_cache shape_cache;
  for (const auto& node : discovery.nodes()) {
    Site site;
    site.node                          = node;
    site.kind                          = classify(node);
    site.structural_id                 = stable_id(occurrence_shape(node, library, shape_cache));
    site.gate_equivalents              = gu::mappable_ge_weight(node.base_node());
    plan.summary_.compact_loops       += site.kind == Site_kind::loop_control;
    plan.summary_.conditional_regions += site.kind == Site_kind::conditional_control;
    plan.sites_.push_back(std::move(site));
  }
  plan.summary_.grouped_sites = plan.sites_.size();

  absl::flat_hash_set<hhds::Gid> active;
  count_grouped_structure(root, active, plan.summary_.physical_occurrence_sites, plan.summary_.complete, plan.errors_);
  if (plan.summary_.physical_occurrence_sites != plan.summary_.grouped_sites) {
    plan.summary_.complete = false;
    plan.errors_.push_back(std::format("independent grouped coverage count is {}, but discovery yielded {} sites",
                                       plan.summary_.physical_occurrence_sites,
                                       plan.summary_.grouped_sites));
  }

  absl::flat_hash_map<hhds::Occurrence_index, size_t> index;
  index.reserve(plan.sites_.size());
  for (size_t i = 0; i < plan.sites_.size(); ++i) {
    index.emplace(plan.sites_[i].node.get_occurrence_index(), i);
  }
  refine_structural_ids(plan.sites_, index);
  {
    std::map<std::string, size_t> occurrence_rank;
    for (auto& site : plan.sites_) {
      const size_t rank = occurrence_rank[site.structural_id]++;
      site.storage_id   = std::format("{}:o{}", site.structural_id, rank);
    }
  }
  absl::flat_hash_set<hhds::Definition_index>                                  compact_steps;
  absl::flat_hash_map<std::pair<hhds::Graph*, hhds::Definition_index>, size_t> loop_control_index;
  for (size_t site_index = 0; site_index < plan.sites_.size(); ++site_index) {
    const auto& site = plan.sites_[site_index];
    if (site.kind != Site_kind::loop_control) {
      continue;
    }
    if (!site.node.path().steps().empty()) {
      compact_steps.insert(site.node.path().steps().back().subnode);
    }
    loop_control_index.try_emplace(std::pair{site.node.get_graph(), site.node.base_node().get_definition_index()}, site_index);
  }
  const auto under_compact_loop = [&](const Site& site) {
    if (site.kind == Site_kind::loop_control) {
      return false;
    }
    for (const auto& step : site.node.path().steps()) {
      if (compact_steps.contains(step.subnode)) {
        return true;
      }
    }
    return false;
  };

  const auto find_body_driver_site = [&](hhds::Graph*                 body_graph,
                                         const hhds::Occurrence_path& body_path,
                                         const hhds::Node_class&      driver) -> std::optional<size_t> {
    const hhds::Occurrence_index occurrence{body_path, driver.get_definition_index()};
    if (const auto it = index.find(occurrence); it != index.end()) {
      return it->second;
    }
    // A grouped opaque Sub (notably a compact-loop control site) may carry the
    // group's synthetic occurrence identity rather than the base body edge's
    // identity.  Its definition identity within the same body is nevertheless
    // exact and unique.  Resolve that boundary without depending on a raw
    // occurrence index in reports or persisted plan state.
    if (gu::type_op_of(driver) != Ntype_op::Sub) {
      return std::nullopt;
    }
    if (const auto it = loop_control_index.find(std::pair{body_graph, driver.get_definition_index()});
        it != loop_control_index.end()) {
      return it->second;
    }
    return std::nullopt;
  };

  struct Body_occurrence {
    hhds::Occurrence_path path;
    hhds::Graph*          graph  = nullptr;
    size_t                anchor = Color_plan::invalid_index;
  };
  std::vector<Body_occurrence>                                                bodies;
  std::vector<std::vector<size_t>>                                            body_sites;
  absl::flat_hash_map<std::pair<hhds::Graph*, hhds::Occurrence_path>, size_t> body_lookup;
  // GraphIO is executable plan surface even when the body has no ordinary
  // data/state site.  A compact-loop-only root is represented by its opaque
  // loop-control site, and a completely folded root may have only a literal
  // output.  Seeding the root body keeps both shapes in the output-version
  // discovery below instead of admitting an empty color schedule that can
  // never publish the root outputs.
  {
    hhds::Occurrence_path root_path;
    body_lookup.emplace(std::pair{root, root_path}, 0);
    bodies.push_back(Body_occurrence{std::move(root_path), root, Color_plan::invalid_index});
    body_sites.emplace_back();
  }
  for (size_t i = 0; i < plan.sites_.size(); ++i) {
    const auto& site = plan.sites_[i];
    // Instance/control sites describe the boundary itself, not a node in the
    // occurrence body whose GraphIO they appear to reference.  In particular,
    // using a Sub site as a body anchor manufactures a second, unbound copy of
    // the parent's outputs under the child's path.  Executable data/state sites
    // carry the actual (definition,path) pair for a body occurrence.
    if (site.kind == Site_kind::instance || site.kind == Site_kind::conditional_control || site.kind == Site_kind::loop_control) {
      continue;
    }
    // A compact body is analyzed once by the grouped discovery above, but it
    // is executable only through the descriptor-bearing loop-control site.
    // Putting its nodes in the root version DAG would both recreate the carry
    // ring and manufacture one occurrence binding for an ordinal-indexed
    // storage array. The generated native loop owns those indexed bindings.
    if (under_compact_loop(site)) {
      continue;
    }
    const auto key               = std::pair{site.node.get_graph(), site.node.path()};
    const auto [found, inserted] = body_lookup.emplace(key, bodies.size());
    if (inserted) {
      bodies.push_back(Body_occurrence{site.node.path(), site.node.get_graph(), i});
      body_sites.emplace_back();
    }
    body_sites[found->second].push_back(i);
  }

  absl::flat_hash_set<size_t> live;
  live.reserve(plan.sites_.size());
  std::queue<size_t> pending;
  auto               mark_live = [&](size_t i) {
    if (live.insert(i).second) {
      pending.push(i);
    }
  };
  for (size_t i = 0; i < plan.sites_.size(); ++i) {
    const auto kind = plan.sites_[i].kind;
    if (under_compact_loop(plan.sites_[i])) {
      continue;
    }
    if (kind == Site_kind::state || kind == Site_kind::instance || kind == Site_kind::conditional_control
        || kind == Site_kind::loop_control) {
      mark_live(i);
    }
  }
  for (const auto& body : bodies) {
    const auto body_io = body.graph == nullptr ? nullptr : body.graph->get_io();
    if (!body_io) {
      continue;
    }
    const bool is_top_body = body.graph == root && body.path.steps().empty();
    if (!include_observations && !is_top_body) {
      continue;
    }
    std::string prefix;
    if (library != nullptr) {
      prefix = hhds::format_occurrence_path(*library, body.path);
      if (!prefix.empty()) {
        prefix += ".";
      }
    }
    for (const auto& decl : body_io->get_output_pin_decls()) {
      const auto  output    = body.graph->get_output_pin(decl.name);
      std::string driver_id = "unbound";
      if (body.graph == root && body.path.steps().empty()) {
        // Root GraphIO may be driven directly by a submodule output. Resolve
        // through that boundary to the executable leaf occurrence; the local
        // Pin_class edge would stop at an ordinary Sub, which is intentionally
        // not a color-plan site.
        for (auto edge_drv : discovery.lift(output).get_driver_pins()) {
          if (const auto site = index.find(edge_drv.get_master_node().get_occurrence_index()); site != index.end()) {
            mark_live(site->second);
            driver_id = plan.sites_[site->second].structural_id;
          }
        }
      } else {
        for (auto edge_drv : output.get_driver_pins()) {
          if (const auto site = find_body_driver_site(body.graph, body.path, edge_drv.get_master_node())) {
            mark_live(*site);
            driver_id = plan.sites_[*site].structural_id;
          }
        }
      }
      plan.observations_.push_back(Observation{prefix + decl.name, std::move(driver_id), false, decl.port_id});
    }
  }
  if (io != nullptr) {
    for (const auto& decl : io->get_input_pin_decls()) {
      plan.observations_.push_back(Observation{decl.name, std::format("top-input:p{}", decl.port_id), true, decl.port_id});
    }
  }
  while (!pending.empty()) {
    const size_t i = pending.front();
    pending.pop();
    if (std::getenv("LHD_SIM_PLAN_DEBUG") != nullptr && plan.sites_[i].kind == Site_kind::loop_control) {
      for (const auto& edge_sink : plan.sites_[i].node.inp_sorted_pins()) {
        for (const auto& edge_drv : edge_sink.get_driver_pins()) {
          const auto drv = edge_drv.get_master_node();
          std::print(stderr,
                     "[plan] loop site {} sink pid {} <- driver pid {} op {} self={} indexed={}\n",
                     i,
                     edge_sink.get_port_id(),
                     edge_drv.get_port_id(),
                     drv.is_invalid() ? -1 : static_cast<int>(gu::type_op_of(drv)),
                     !drv.is_invalid() && drv == plan.sites_[i].node,
                     !drv.is_invalid() && index.contains(drv.get_occurrence_index()));
        }
      }
    }
    // A carry input has TWO drivers -- which is why the walk reads the PLURAL
    // get_driver_pins() per sink: the compact node's own carry self-edge (the
    // eager chain inside the wrapper — not a parent-level dependency) and the
    // parent's SEED, which ordinal 0 consumes. Only the self-edge is skipped;
    // skipping every carry edge marked the seed cone dead, and a runtime seed
    // (a packed array built before the loop) then reached the wrapper as
    // `0 /*UNRESOLVED-CYCLE*/` (a constant seed hid it).
    for (const auto& edge_sink : plan.sites_[i].node.inp_sorted_pins()) {
      for (const auto& edge_drv : edge_sink.get_driver_pins()) {
        const auto driver = edge_drv.get_master_node();
        if (driver.is_invalid()) {
          continue;
        }
        if (plan.sites_[i].kind == Site_kind::loop_control && is_loop_carry_input(plan.sites_[i].node, edge_sink)
            && driver == plan.sites_[i].node) {
          continue;
        }
        if (const auto it = index.find(driver.get_occurrence_index()); it != index.end()) {
          mark_live(it->second);
        }
      }
    }
  }
  for (const size_t i : live) {
    plan.sites_[i].live = true;
  }
  plan.summary_.live_sites = live.size();
  if (std::getenv("LHD_SIM_PLAN_DEBUG") != nullptr) {
    for (size_t i = 0; i < plan.sites_.size(); ++i) {
      const auto& st = plan.sites_[i];
      std::print(stderr,
                 "[plan] site {} kind {} op {} live {} id {}\n",
                 i,
                 static_cast<int>(st.kind),
                 static_cast<int>(gu::type_op_of(st.node)),
                 st.live,
                 st.structural_id.substr(0, 40));
    }
  }

  for (size_t consumer = 0; consumer < plan.sites_.size(); ++consumer) {
    if (!plan.sites_[consumer].live) {
      continue;
    }
    // Carries are descriptor semantics, not ordinary data dependencies in the
    // grouped plan. Depending on which side of the descended boundary asks
    // for edges, HHDS may resolve the stored self-edge through the one analyzed
    // body. Record and cut the descriptor edge directly so this invariant does
    // not depend on that lazy-resolution direction.
    if (plan.sites_[consumer].kind == Site_kind::loop_control) {
      for (const auto& carry : plan.sites_[consumer].node.subnode_group().carries()) {
        plan.dependencies_.push_back(
            Dependency{consumer, consumer, carry.output_port(), carry.input_port(), Dependency_kind::loop_carry, true});
        ++plan.summary_.carry_edges_cut;
      }
    }
    if (gu::type_op_of(plan.sites_[consumer].node) == Ntype_op::Memory) {
      // Only ordering="none" carries an `undef` collision matrix and can draw
      // a runtime value. The matrix itself is a canonical Memory attribute, so
      // discovery can distinguish deterministic old/fwd/program arrays without
      // duplicating the emitter's full ordering classifier.
      bool undef_found = false;  // the old walk's `break` left BOTH loops
      for (const auto& edge_sink : plan.sites_[consumer].node.inp_sorted_pins()) {
        const auto name = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(edge_sink.get_port_id()));
        if (name != "undef") {
          continue;
        }
        for (const auto& edge_drv : edge_sink.get_driver_pins()) {
          if (edge_drv.is_const() && !edge_drv.is_known_false()) {
            plan.summary_.runtime_random = true;
            undef_found                  = true;
            break;
          }
        }
        if (undef_found) {
          break;
        }
      }
    }
    for (const auto& edge_sink : plan.sites_[consumer].node.inp_sorted_pins()) {
      for (const auto& edge_drv : edge_sink.get_driver_pins()) {
        // Unknown literal bits are not runtime randomness in simulation. Under
        // sim.unknown_zero cgen_sim's sim_const_text() concretizes them to zero;
        // by default sim_const_expr() draws them from hlop's seeded PRNG ONCE,
        // behind a `static const`, so the literal is still a constant across
        // periods. Only operations that draw on EVERY period (currently
        // ordering="none" memory collisions above) require random scheduling.
        const auto producer_it = index.find(edge_drv.get_master_node().get_occurrence_index());
        if (producer_it == index.end() || !plan.sites_[producer_it->second].live) {
          continue;
        }
        Dependency dep;
        dep.producer      = producer_it->second;
        dep.consumer      = consumer;
        dep.producer_port = edge_drv.get_port_id();
        dep.consumer_port = edge_sink.get_port_id();
        if (is_loop_carry(plan.sites_[consumer].node, edge_drv, edge_sink) && dep.producer == consumer) {
          continue;  // the self-edge: already represented once from Subnode_group::carries()
        } else if (plan.sites_[dep.producer].kind == Site_kind::state) {
          dep.kind = Dependency_kind::state_read;
        } else if (plan.sites_[dep.consumer].kind == Site_kind::state) {
          dep.kind = Dependency_kind::state_update;
        } else if (plan.sites_[dep.producer].kind != Site_kind::data || plan.sites_[dep.consumer].kind != Site_kind::data) {
          dep.kind = Dependency_kind::control;
        }
        plan.dependencies_.push_back(dep);
      }
    }
  }

  // Fine-grain legal colors over (occurrence node, state version). This is the
  // staged-state bring-up model: every version site is initially one color,
  // and the legality-constrained coarsener may merge them later. No base node
  // is forced into one color when separate pre/post edge readings are needed.
  // Conditional bodies may execute unconditionally: the materialized
  // Clock_cell carries (__valid || reset) into each state commit. Compact loop
  // bodies are represented by one opaque, versioned control site; their
  // indexed storage and sequential carry order stay inside the native loop
  // kernel rather than leaking into this occurrence-wide DAG.
  plan.summary_.versioning_complete = true;
  const lc::Design_clocks clocks(root, /*hier=*/true);
  using Version_key = std::tuple<size_t, State_version, Version_role, hhds::Port_id>;
  std::map<Version_key, size_t>    version_index;
  std::vector<std::vector<size_t>> versions_by_base(plan.sites_.size());
  std::queue<size_t>               version_pending;
  auto ensure_version = [&](size_t base, State_version version, Version_role role, Execution_slot slot, hhds::Port_id output_port) {
    const Version_key key{base, version, role, output_port};
    if (const auto it = version_index.find(key); it != version_index.end()) {
      return it->second;
    }
    Version_site site;
    site.base_site      = base;
    site.output_port    = output_port;
    site.version        = version;
    site.role           = role;
    site.slot           = slot;
    site.structural_id  = stable_id(plan.sites_[base].structural_id + ":" + std::string(state_version_name(version)) + ":"
                                    + std::string(version_role_name(role)) + ":p" + std::to_string(output_port));
    const size_t result = plan.version_sites_.size();
    plan.version_sites_.push_back(std::move(site));
    version_index.emplace(key, result);
    versions_by_base[base].push_back(result);
    if (role == Version_role::data) {
      version_pending.push(result);
    }
    return result;
  };

  absl::flat_hash_map<std::pair<size_t, size_t>, size_t> version_edges;
  auto add_version_edge = [&](size_t producer, size_t consumer, uint64_t boundary_bits) {
    // A self precedence edge says "run this site before itself". It is lethal to
    // the topological sort -- the site can never reach indegree 0, so it stalls,
    // and so does everything reachable from it -- but it is NOT vacuous, it is
    // UNSATISFIABLE: a genuine self-dependency (`w = w + 1`) that reached version
    // identity. Dropping it silently would schedule the site anyway and read a
    // stale value, so drop it only to keep the Kahn diagnostic readable and fail
    // the plan exactly as the stall did.
    //
    // MEASURED on XiangShan `Backend`: `self-edges-dropped=0`. No
    // design has produced one; the 307,612 blocked sites there come from a
    // 77-hop combinational ring, NOT from self edges (see the cycle extractor
    // below, and docs/opt_loop_incr.md). Do not read this guard as the fix for
    // that stall.
    if (producer == consumer) {
      if (plan.summary_.self_edges_dropped == 0) {  // one witness is enough; the count carries the rest
        plan.errors_.push_back(
            std::format("version site {} precedes ITSELF: an unsatisfiable ordering constraint, i.e. a "
                        "genuine self-dependency at version granularity",
                        plan.version_sites_[producer].structural_id));
      }
      ++plan.summary_.self_edges_dropped;
      plan.summary_.versioning_complete = false;
      return;
    }
    const auto [it, inserted]
        = version_edges.emplace(std::pair<size_t, size_t>{producer, consumer}, plan.version_dependencies_.size());
    if (inserted) {
      plan.version_dependencies_.push_back(Version_dependency{producer, consumer, boundary_bits});
    } else {
      plan.version_dependencies_[it->second].boundary_bits += boundary_bits;
    }
  };
  auto producer_version = [&](size_t base, State_version version, hhds::Port_id output_port) {
    // A flop/latch exposes one persistent Q value. A Memory exposes computed
    // read-port values whose address/forwarding inputs vary by version; its
    // persistent array is storage owned by the update action, not a scalar Q
    // slot that can be bound to a consumer operand.
    const auto op = gu::type_op_of(plan.sites_[base].node);
    const auto role
        = plan.sites_[base].kind == Site_kind::state && op != Ntype_op::Memory ? Version_role::state_read : Version_role::data;
    return ensure_version(base, version, role, evaluation_slot(version), role == Version_role::data ? output_port : 0);
  };

  // A Sub occurrence's identity path includes the call itself, while nodes in
  // its parent body use the path before that final step.  Index ordinary nodes
  // by Occurrence_index, and index nested calls by a hash of their full path so
  // an output alias can descend through another Sub without scanning the
  // occurrence-wide site table. Hash collisions are verified structurally.
  const auto path_hash = [](const hhds::Occurrence_path& path, std::optional<hhds::Definition_index> appended) {
    Refinement_hash_builder hash;
    hash.append_u64(static_cast<uint64_t>(path.root_gid()));
    for (const auto& step : path.steps()) {
      hash.append_u64(static_cast<uint64_t>(step.subnode.gid));
      hash.append_u64(static_cast<uint64_t>(step.subnode.value));
      hash.append_u64(step.ordinal.has_value());
      hash.append_u64(step.ordinal.value_or(0));
    }
    if (appended) {
      hash.append_u64(static_cast<uint64_t>(appended->gid));
      hash.append_u64(static_cast<uint64_t>(appended->value));
      hash.append_u64(0);
      hash.append_u64(0);
    }
    return hash.finish()[0];
  };
  absl::flat_hash_map<uint64_t, std::vector<size_t>> sub_by_path_hash;
  for (size_t site_index = 0; site_index < plan.sites_.size(); ++site_index) {
    const auto& site = plan.sites_[site_index];
    if (gu::type_op_of(site.node) == Ntype_op::Sub && !site.node.is_loop_subnode()) {
      sub_by_path_hash[path_hash(site.node.path(), std::nullopt)].push_back(site_index);
    }
  }
  const auto find_body_site
      = [&](hhds::Graph* body, const hhds::Occurrence_path& body_path, const hhds::Node_class& driver) -> std::optional<size_t> {
    if (gu::type_op_of(driver) != Ntype_op::Sub) {
      const hhds::Occurrence_index occurrence{body_path, driver.get_definition_index()};
      if (const auto found = index.find(occurrence); found != index.end()) {
        return found->second;
      }
      return std::nullopt;
    }
    const auto found = sub_by_path_hash.find(path_hash(body_path, driver.get_definition_index()));
    if (found == sub_by_path_hash.end()) {
      return std::nullopt;
    }
    for (const size_t candidate_index : found->second) {
      const auto& candidate = plan.sites_[candidate_index].node;
      const auto  steps     = candidate.path().steps();
      if (candidate.get_graph() != body || candidate.get_definition_index() != driver.get_definition_index()
          || steps.size() != body_path.steps().size() + 1 || steps.back().ordinal) {
        continue;
      }
      bool prefix = true;
      for (size_t i = 0; i < body_path.steps().size(); ++i) {
        prefix &= steps[i] == body_path.steps()[i];
      }
      if (prefix) {
        return candidate_index;
      }
    }
    return std::nullopt;
  };

  std::vector<std::optional<bool>> state_rising(plan.sites_.size());
  std::vector<size_t>              state_updates(plan.sites_.size(), std::numeric_limits<size_t>::max());
  std::set<std::tuple<size_t, size_t, hhds::Port_id, uint32_t, uint32_t, uint32_t, hhds::Port_id, uint32_t, State_version, bool>>
      exact_value_uses;
  struct Output_use {
    hhds::Port_id public_port   = 0;
    size_t        body_anchor   = Color_plan::invalid_index;
    bool          top           = false;
    size_t        producer      = Color_plan::invalid_index;
    hhds::Port_id producer_port = 0;
    State_version version       = State_version::pre_rise;
    uint32_t      width         = 1;
    bool          unsign        = false;
    std::string   literal;
  };
  std::vector<Output_use> output_uses;
  struct Input_use {
    std::string   name;
    size_t        body_anchor   = Color_plan::invalid_index;
    size_t        producer      = Color_plan::invalid_index;
    hhds::Port_id producer_port = 0;
    hhds::Port_id public_port   = 0;
    State_version version       = State_version::pre_rise;
    uint32_t      width         = 1;
    bool          unsign        = false;
    bool          top_input     = false;
    std::string   literal;
  };
  std::vector<Input_use>    input_uses;
  livehd::port_reach::Cache port_reach;
  // Top-level occurrence_packed_footprint results, per discover. The packed-Or
  // refinement below footprints EVERY operand of a packed word once per slice
  // read of that word, and each footprint re-walks up to 4096 pins of the
  // operand's cone through hierarchical occurrence queries: M reads x N
  // operands x a cone. On minion that was 35 s of a 36 s discover. A top-level
  // call always starts from a fresh budget and depth 0, so its result is a pure
  // function of the pin and caching it changes no answer.
  // The top-level memo keeps a CUT answer too (it is still a pure function of
  // the pin); `packed_footprint_exact` shares the uncut subtrees between
  // different top-level pins.
  absl::flat_hash_map<hhds::Occurrence_pin, std::pair<int, int>> packed_footprint_memo;
  Packed_footprint_cache                                         packed_footprint_exact;
  const auto packed_footprint = [&](const hhds::Occurrence_pin& pin) -> std::pair<int, int> {
    if (const auto it = packed_footprint_memo.find(pin); it != packed_footprint_memo.end()) {
      return it->second;
    }
    const auto footprint = occurrence_packed_footprint(pin, &packed_footprint_exact);
    packed_footprint_memo.emplace(pin, footprint);
    return footprint;
  };
  // The value use is named by the SINK pin the caller is walking plus ONE of
  // that pin's drivers, which is what the in-pin walks below hand over. A
  // two-driver carry-in therefore calls this once per driver, exactly as the
  // old per-edge callers did.
  const auto                add_value_use = [&](const hhds::Occurrence_pin& use_driver,
                                                const hhds::Occurrence_pin& use_sink,
                                                size_t                      consumer,
                                                uint32_t                    consumer_input,
                                                State_version               version) {
    auto producer_it = index.find(use_driver.get_master_node().get_occurrence_index());
    bool top_input   = producer_it == index.end() && gu::is_graph_input_pin(use_driver);
    if (!top_input && (producer_it == index.end() || !plan.sites_[producer_it->second].live)) {
      return;
    }
    size_t        producer_base       = top_input ? Color_plan::invalid_index : producer_it->second;
    hhds::Port_id producer_port       = use_driver.get_port_id();
    uint32_t      producer_shift      = 0;
    uint32_t      producer_width      = 0;
    uint32_t      producer_extract_lo = 0;
    uint32_t      producer_extract_hi = 0;
    bool          preextracted        = false;
    std::string   producer_literal;
    int           lo = -1;
    int           hi = -1;

    // A packed child output may be a word-level cycle while the exact slice
    // demanded by its parent is acyclic. Resolve constant Get_mask and the
    // common And(SRA(word,k),low-mask) idiom through the callee's proven
    // output-slice summary and bind the exact leaf value.
    const auto& consumer_base          = plan.sites_[plan.version_sites_[consumer].base_site];
    const auto  consumer_op            = gu::type_op_of(consumer_base.node);
    const bool  slice_debug            = std::getenv("LIVEHD_SIM_COLOR_DEBUG") != nullptr;
    uint32_t    output_boundary_width  = 0;
    bool        output_boundary_unsign = gu::is_unsign(use_driver);
    if (consumer_op == Ntype_op::Get_mask && use_sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
      bool mask_found = false;  // the old walk's `break` left BOTH loops
      for (const auto& input_sink : consumer_base.node.inp_sorted_pins()) {
        if (input_sink.get_port_id() != Ntype::get_sink_pid(Ntype_op::Get_mask, "mask")) {
          continue;
        }
        for (const auto& input_drv : input_sink.get_driver_pins()) {
          if (input_drv.is_const()) {
            std::tie(lo, hi) = gu::const_of(input_drv).get_mask_range();
            mask_found       = true;
            break;
          }
        }
        if (mask_found) {
          break;
        }
      }
    }
    // Occurrence traversal also erases a child's GraphIO output node. Recover
    // that public carrier before fusing the child producer into its parent
    // consumer. The child expression itself can be wider (for example, a
    // packed Set_mask chain), but those internal bits are not visible across
    // the declared module boundary.
    const auto driver_node  = use_driver.get_master_node();
    const auto driver_steps = driver_node.path().steps();
    if (driver_steps.size() > consumer_base.node.path().steps().size()) {
      const auto driver_graph = driver_node.get_graph();
      const auto driver_io    = driver_graph == nullptr ? nullptr : driver_graph->get_io();
      if (driver_io != nullptr) {
        for (const auto& decl : driver_io->get_output_pin_decls()) {
          const auto output  = driver_graph->get_output_pin(decl.name);
          bool       matches = false;
          for (auto output_edge_drv : output.get_driver_pins()) {
            matches
                |= output_edge_drv.get_master_node().get_class_index() == use_driver.base_pin().get_master_node().get_class_index()
                   && output_edge_drv.get_port_id() == use_driver.get_port_id();
          }
          if (matches) {
            output_boundary_width  = static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(output, *driver_io, decl.name)));
            // The GraphIO DECLARATION carries the port's sign; the output pin
            // itself never does (upass.tolg stamps sign on driver pins only,
            // and an output pin is a sink), so gu::is_unsign(output) is
            // unconditionally true and would zero-extend every signed port.
            output_boundary_unsign = decl.unsign;
            break;
          }
        }
      }
    }
    if (!top_input && library != nullptr) {
      bool                 position_in_whole = true;
      uint32_t             surface_shift     = 0;
      hhds::Occurrence_pin crossing          = use_driver;
      if (consumer_op == Ntype_op::Get_mask && use_sink.get_port_id() == 0) {
        // cprop canonicalizes the And(SRA(word,k), low-mask) bit read into
        // Get_mask(SRA(word,k), low-mask): hop the same consumer-side SRA the
        // And spelling below hops, so both spellings bind the exact
        // [k, k+width) slice instead of pinning the whole shifted word.
        //
        // Only a LOW-ANCHORED mask may hop. Dropping position_in_whole makes
        // the producer bind LSB-aligned (producer_shift = 0), and the consumer
        // Get_mask still applies its ORIGINAL mask to that value: for a mask
        // starting at bit m>0 the cell would then read bits [m, m+w) of the
        // LSB-aligned leaf, i.e. [k+2m, k+2m+w) of the word instead of
        // [k+m, k+m+w). The And spelling below guards the same way
        // (mask_lo == 0).
        if (lo == 0 && hi > lo && !crossing.is_const()) {
          const auto shift_node = crossing.get_master_node();
          if (shift_node.path() == consumer_base.node.path() && gu::type_op_of(shift_node) == Ntype_op::SRA) {
            hhds::Occurrence_pin value;
            hhds::Occurrence_pin amount;
            for (const auto& input_sink : shift_node.inp_sorted_pins()) {
              for (const auto& input_drv : input_sink.get_driver_pins()) {
                if (input_sink.get_port_id() == 0) {
                  value = input_drv;
                } else {
                  amount = input_drv;
                }
              }
            }
            if (!value.is_invalid() && amount.is_const()) {
              const auto& shift = gu::const_of(amount);
              // Bound the amount before narrowing to int: a to-positive mask
              // (-1) reports hi == INT_MAX/2, so an unbounded `hi += shift`
              // is signed overflow.
              if (shift.is_just_i64() && shift.to_just_i64() >= 0 && shift.to_just_i64() <= (1 << 28)) {
                lo                += static_cast<int>(shift.to_just_i64());
                hi                += static_cast<int>(shift.to_just_i64());
                crossing           = value;
                position_in_whole  = false;  // the SRA already defined the consumer's LSB-aligned surface
              }
            }
          }
        }
      } else if (consumer_op == Ntype_op::And && !use_driver.is_const()) {
        int mask_width = -1;
        for (const auto& input_sink : consumer_base.node.inp_sorted_pins()) {
          for (const auto& input_drv : input_sink.get_driver_pins()) {
            if (!input_drv.is_const()) {
              continue;
            }
            const auto [mask_lo, mask_hi] = gu::const_of(input_drv).get_mask_range();
            if (mask_lo == 0 && mask_hi > 0) {
              mask_width = mask_hi;
              break;
            }
          }
          if (mask_width > 0) {
            break;  // the old walk's `break` left BOTH loops
          }
        }
        for (int depth = 0; depth < 8 && !crossing.is_invalid() && !crossing.is_const(); ++depth) {
          const auto           wrapper = crossing.get_master_node();
          const auto           op      = gu::type_op_of(wrapper);
          hhds::Occurrence_pin value;
          bool                 transparent = op == Ntype_op::Sext;
          if (op == Ntype_op::Sext || op == Ntype_op::Get_mask) {
            hhds::Occurrence_pin mask;
            for (const auto& input_sink : wrapper.inp_sorted_pins()) {
              for (const auto& input_drv : input_sink.get_driver_pins()) {
                if (input_sink.get_port_id() == 0) {
                  value = input_drv;
                } else {
                  mask = input_drv;
                }
              }
            }
            transparent |= mask.is_invalid();
            if (mask.is_const()) {
              const auto& constant  = gu::const_of(mask);
              transparent          |= constant.is_just_i64() && constant.to_just_i64() == -1;
            }
          }
          if (!transparent || value.is_invalid()) {
            break;
          }
          crossing = value;
        }
        const auto shift_node = crossing.get_master_node();
        if (mask_width > 0 && shift_node.path() == consumer_base.node.path() && gu::type_op_of(shift_node) == Ntype_op::SRA) {
          hhds::Occurrence_pin value;
          hhds::Occurrence_pin amount;
          for (const auto& input_sink : shift_node.inp_sorted_pins()) {
            for (const auto& input_drv : input_sink.get_driver_pins()) {
              if (input_sink.get_port_id() == 0) {
                value = input_drv;
              } else {
                amount = input_drv;
              }
            }
          }
          if (!value.is_invalid() && amount.is_const()) {
            const auto& shift = gu::const_of(amount);
            if (shift.is_just_i64() && shift.to_just_i64() >= 0) {
              lo                = static_cast<int>(shift.to_just_i64());
              hi                = lo + mask_width;
              crossing          = value;
              position_in_whole = false;  // SRA already defined the consumer's LSB-aligned surface
            }
          }
        }
      }
      if (hi > lo) {
        // Width-only/to-unsigned Get_mask nodes are transparent while tracing
        // the actual child output boundary.
        for (int depth = 0; depth < 8 && !crossing.is_invalid(); ++depth) {
          const auto crossing_node = crossing.get_master_node();
          if (gu::type_op_of(crossing_node) != Ntype_op::Get_mask) {
            break;
          }
          hhds::Occurrence_pin value;
          hhds::Occurrence_pin mask;
          for (const auto& input_sink : crossing_node.inp_sorted_pins()) {
            for (const auto& input_drv : input_sink.get_driver_pins()) {
              if (input_sink.get_port_id() == 0) {
                value = input_drv;
              } else if (input_sink.get_port_id() == 2 && input_drv.is_const()) {
                mask = input_drv;
              }
            }
          }
          bool transparent = false;
          if (!value.is_invalid() && !mask.is_invalid()) {
            const auto& constant = gu::const_of(mask);
            transparent          = gu::is_whole_value_mask(constant);
            if (!transparent && gu::is_unsign(value)) {
              if (auto window = gu::mask_window_of(constant); window) {
                const auto value_bits = gu::bits_of(value);
                transparent           = window->first == 0 && value_bits > 0 && window->second >= value_bits;
              }
            }
          }
          if (!transparent || value.is_invalid()) {
            break;
          }
          crossing = value;
        }
      }
      if (hi > lo && !crossing.is_invalid()) {
        // Walk the PACKED-WORD spellings down to the single field the consumer
        // actually reads.
        //
        // Ntype_op::Concat is the canonical one: the cell carries the lane
        // table, so a read landing inside ONE lane binds that lane's value and
        // nothing else -- exact where the Or/SHL/Get_mask pattern match this
        // replaces had to guess a lane's extent from the NEXT lane's offset.
        // A read that STRADDLES lanes deliberately gets nothing: a Value_use
        // binds exactly one producer, so "depends on lanes 2 and 3" has nowhere
        // to land, and keeping the assembled word is the coarser-but-correct
        // answer (the same fail-closed rule as the straddling Set_mask write
        // below).
        //
        // Set_mask chains are the hand-spelled twin cprop leaves behind when a
        // window is non-constant or overlapping. An in-range read selects the
        // LSB-aligned value arm, an out-of-range read keeps walking the base.
        // surface_shift records where the selected field lived in the original
        // word so the unchanged callee Get_mask still sees the same bit
        // positions.
        bool rebased = false;
        for (int depth = 0; depth < 16 && !crossing.is_invalid() && !crossing.is_const(); ++depth) {
          const auto packed    = crossing.get_master_node();
          const auto packed_op = gu::type_op_of(packed);
          if (packed_op == Ntype_op::Or) {
            // Firtool also spells packed records as an Or of disjoint,
            // constant-shifted fields. At word grain an input field and an
            // unrelated output field can then close a false hierarchy cycle
            // (XS Rob: interrupt_safe versus deqPtrVec). Refine only when one
            // operand is the unique possible owner of the requested range;
            // an unbounded footprint counts as an overlap and fails closed.
            hhds::Occurrence_pin overlapper;
            size_t               overlaps  = 0;
            size_t               fanin     = 0;
            bool                 malformed = false;
            // `fanin` still ticks once per DRIVER, which is what the per-edge
            // count was: a sink with two drivers contributed two operands.
            for (const auto& input_sink : packed.inp_sorted_pins()) {
              for (const auto& input_drv : input_sink.get_driver_pins()) {
                // Every Or operand is on the one `as` bank; bank-check rather
                // than pid-check now that each owns its own pid (cell.hpp).
                if (++fanin > 4096 || Ntype::sink_bank(Ntype_op::Or, input_sink.get_port_id()) != 0) {
                  malformed = true;
                  break;
                }
                const auto footprint = packed_footprint(input_drv);
                if (footprint.first < 0 || !(hi <= footprint.first || lo >= footprint.second)) {
                  overlapper = input_drv;
                  ++overlaps;
                }
              }
              if (malformed) {
                break;  // the old walk's `break` left BOTH loops
              }
            }
            if (malformed || overlaps != 1) {
              break;
            }
            crossing = overlapper;
            rebased  = true;
            continue;
          }
          if (packed_op == Ntype_op::SHL) {
            const int  shift = occurrence_const_shl_amount(packed);
            const auto value = occurrence_driver_at(packed, 0);
            if (shift < 0 || value.is_invalid() || lo < shift) {
              break;  // unknown shift or a slice that straddles the zero fill
            }
            crossing       = value;
            lo            -= shift;
            hi            -= shift;
            surface_shift += static_cast<uint32_t>(shift);
            rebased        = true;
            continue;
          }
          if (packed_op == Ntype_op::Get_mask) {
            const auto value = occurrence_driver_at(packed, Ntype::get_sink_pid(Ntype_op::Get_mask, "a"));
            const auto mask  = occurrence_driver_at(packed, Ntype::get_sink_pid(Ntype_op::Get_mask, "mask"));
            if (value.is_invalid() || mask.is_invalid() || !mask.is_const()) {
              break;
            }
            const auto window = gu::mask_window_of(gu::const_of(mask));
            if (!window || hi > window->second - window->first) {
              break;
            }
            const auto [mask_lo, mask_hi]  = *window;
            // Compose nested constant slices before crossing a packed child
            // output. A wide child field may span several Or/SHL lanes even
            // though its parent consumes only one narrow subfield; tracing the
            // outer demand through this select makes that exact subfield the
            // range tested for unique ownership.
            crossing                       = value;
            lo                            += mask_lo;
            hi                            += mask_lo;
            rebased                        = true;
            continue;
          }
          if (packed_op == Ntype_op::Concat) {
            const auto lanes = gu::concat_lanes(packed.base_node());
            if (lanes.empty()) {
              break;  // undecodable cell: stay on the assembled word
            }
            size_t hit = lanes.size();
            for (size_t lane = 0; lane < lanes.size(); ++lane) {
              // int64: a lane table is only bounded by the cell's own width
              // constants, so offset + width is not an int32 the range compare
              // may assume.
              const int64_t window_hi = static_cast<int64_t>(lanes[lane].offset) + lanes[lane].width;
              if (lanes[lane].offset <= lo && hi <= window_hi) {
                hit = lane;
                break;
              }
            }
            if (hit == lanes.size()) {
              break;
            }
            // Lane i's value rides sink pid 2i (cell contract). Read the
            // OCCURRENCE driver rather than lanes[hit].value: the occurrence
            // driver has already crossed whatever GraphIO boundary sits between
            // the two bodies, which is what makes a lane fed from the caller
            // resolvable at all. One value may drive several lanes, so the pid
            // -- never pin identity -- is the sound key.
            hhds::Occurrence_pin value;
            for (const auto& input_sink : packed.inp_sorted_pins()) {
              if (input_sink.get_port_id() != static_cast<hhds::Port_id>(2 * hit)) {
                continue;
              }
              for (const auto& input_drv : input_sink.get_driver_pins()) {
                value = input_drv;
                break;  // the first driver, as the edge walk took
              }
              break;
            }
            if (value.is_invalid()) {
              break;
            }
            // A lane's declared width TRUNCATES its value, but only at and
            // above that width -- and `hi <= offset + width` already excluded
            // those bits, so the rebased range reads the raw value unchanged.
            crossing       = value;
            lo            -= lanes[hit].offset;
            hi            -= lanes[hit].offset;
            surface_shift += static_cast<uint32_t>(lanes[hit].offset);
            rebased        = true;
            continue;
          }
          // A Set_mask arm is only repositionable while the consumer still
          // holds the absolute bit position; with the SRA already absorbed
          // there is nowhere to put surface_shift back.
          if (packed_op != Ntype_op::Set_mask || !position_in_whole) {
            break;
          }
          hhds::Occurrence_pin base;
          hhds::Occurrence_pin mask;
          hhds::Occurrence_pin value;
          for (const auto& input_sink : packed.inp_sorted_pins()) {
            for (const auto& input_drv : input_sink.get_driver_pins()) {
              switch (input_sink.get_port_id()) {
                case 0 : base = input_drv; break;
                case 2 : mask = input_drv; break;
                case 4 : value = input_drv; break;
                default: break;
              }
            }
          }
          if (mask.is_invalid() || !mask.is_const()) {
            break;
          }
          const auto window = gu::mask_window_of(gu::const_of(mask));
          if (!window) {
            break;
          }
          const auto [write_lo, write_hi] = *window;
          if (lo >= write_lo && hi <= write_hi) {
            if (value.is_invalid()) {
              break;
            }
            crossing       = value;
            lo            -= write_lo;
            hi            -= write_lo;
            surface_shift += static_cast<uint32_t>(write_lo);
            rebased        = true;
            continue;
          }
          if (hi <= write_lo || lo >= write_hi) {
            if (base.is_invalid()) {
              break;
            }
            crossing = base;
            rebased  = true;
            continue;
          }
          break;  // a straddling read is not one disjoint packed field
        }
        if (rebased && hi > lo && crossing.is_const()) {
          // A constant lane has no executable producer. Materialize its exact
          // selected bits in the boundary ABI; retaining the whole packed
          // carrier invents a dependency on every other lane and can close a
          // false hierarchy cycle. This common tail covers Concat, Set_mask,
          // and shift/Or packed spellings alike.
          auto selected = gu::const_of(crossing);
          if (lo != 0 || hi < std::max(selected.get_signed_bits(), 1)) {
            selected = *selected.get_mask_op_opt(lo, hi);
          }
          producer_literal = selected.to_pyrope();
          producer_width   = static_cast<uint32_t>(hi - lo);
          preextracted     = true;
          crossing         = {};
        }
        // position_in_whole == false means the consumer's SRA was absorbed and
        // its operand must arrive LSB-aligned, so the walk is only bindable
        // when it landed exactly on the requested field's low bit -- the same
        // `slice.lo == lo` rule the child-output arm below applies, restated in
        // the coordinates this walk left behind. surface_shift then belongs to
        // the absorbed SRA, not to the operand.
        if (rebased && (position_in_whole || lo == 0) && !crossing.is_invalid() && !crossing.is_const()) {
          const auto terminal = index.find(crossing.get_master_node().get_occurrence_index());
          const auto bind     = [&] {
            producer_port  = crossing.get_port_id();
            producer_shift = position_in_whole ? surface_shift : 0;
            if (!position_in_whole) {
              // The narrowed field IS the whole operand now: its own stamp is
              // the storage width, not the assembled word's.
              producer_width = static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(crossing)));
            }
          };
          if (terminal != index.end() && plan.sites_[terminal->second].live) {
            top_input     = false;
            producer_base = terminal->second;
            bind();
          } else if (gu::is_graph_input_pin(crossing)) {
            top_input     = true;
            producer_base = Color_plan::invalid_index;
            bind();
          }
          if (slice_debug) {
            std::fprintf(stderr,
                         "[color-slice] traced packed field to depth=%zu pin=%u shift=%u top=%s\n",
                         crossing.get_master_node().path().steps().size(),
                         static_cast<unsigned>(producer_port),
                         producer_shift,
                         top_input ? "yes" : "no");
          }
        }
      }
      if (producer_literal.empty() && hi > lo && !crossing.is_invalid()) {
        auto                         crossing_node = crossing.get_master_node();
        auto                         producer_path = crossing_node.path();
        std::shared_ptr<hhds::Graph> child;
        std::optional<hhds::Port_id> output_port;
        bool                         have_child_path = false;

        if (gu::type_op_of(crossing_node) == Ntype_op::Sub) {
          // HHDS gives the Sub occurrence itself the CALLEE path (its base
          // node still belongs to the parent graph).  The nodes reached inside
          // the callee carry that same path, so it is already the exact key for
          // the leaf occurrence.  Treating it as the parent path and appending
          // another step made every direct Sub-output slice lookup miss.
          const auto sub  = crossing_node.base_node();
          child           = sub.get_subnode_graph();
          output_port     = crossing.get_port_id();
          producer_path   = crossing_node.path();
          have_child_path = child != nullptr;
        } else if (!producer_path.steps().empty()) {
          const auto sub  = library->get_node(producer_path.steps().back().subnode);
          child           = sub.get_subnode_graph();
          have_child_path = child != nullptr;
        }

        const auto sio = child ? child->get_io() : nullptr;
        if (sio != nullptr && have_child_path) {
          if (!output_port) {
            for (const auto& decl : sio->get_output_pin_decls()) {
              const auto output = child->get_output_pin(decl.name);
              for (auto output_edge_drv : output.get_driver_pins()) {
                // HHDS can expose equivalent multi-driver pins through
                // different handle encodings (notably pid 0). Match the
                // semantic driver identity, not the raw pin class index.
                if (output_edge_drv.get_master_node().get_class_index() == crossing.base_pin().get_master_node().get_class_index()
                    && output_edge_drv.get_port_id() == crossing.base_pin().get_port_id()) {
                  output_port = decl.port_id;
                  break;
                }
              }
              if (output_port) {
                break;
              }
            }
          }
          const auto& reach = port_reach.of(child);
          if (output_port) {
            if (const auto slices = reach.out_slices.find(*output_port); slices != reach.out_slices.end()) {
              for (const auto& slice : slices->second) {
                const auto slice_hi = slice.lo + slice.len;
                const bool contains = slice.lo <= static_cast<uint32_t>(lo) && static_cast<uint32_t>(hi) <= slice_hi;
                // A Get_mask consumer retains the absolute bit position, so
                // a containing leaf can be positioned as a whole.  The
                // And(SRA(...),mask) rewrite has already consumed the SRA;
                // without a separate extract ABI it is safe only when the
                // requested range starts at the leaf's LSB.
                if (!contains || (!position_in_whole && slice.lo != static_cast<uint32_t>(lo)) || slice.leaf.is_invalid()
                    || slice.leaf.is_const()) {
                  continue;
                }
                if (const auto leaf = find_body_site(child.get(), producer_path, slice.leaf.get_master_node());
                    leaf && plan.sites_[*leaf].live) {
                  producer_base   = *leaf;
                  producer_port   = slice.leaf.get_port_id();
                  producer_shift  = position_in_whole ? surface_shift + (!slice.shifted ? slice.lo : 0) : 0;
                  // The selected producer is the LSB-aligned leaf, so retain
                  // the requested range in that leaf's coordinates. The old
                  // ABI shifted the leaf back into the packed word and let the
                  // consumer extract it again; the lane ABI below extracts at
                  // the producer and transports only the useful bits.
                  lo             -= static_cast<int>(slice.lo);
                  hi             -= static_cast<int>(slice.lo);
                  if (!position_in_whole) {
                    producer_width = static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(slice.leaf)));
                  }
                  if (slice_debug) {
                    std::fprintf(stderr,
                                 "[color-slice] refined [%d,%d) through child output p%u to leaf p%u shift=%u\n",
                                 lo,
                                 hi,
                                 static_cast<unsigned>(*output_port),
                                 static_cast<unsigned>(producer_port),
                                 producer_shift);
                  }
                }
                break;
              }
            }
          }
        }
      }
    }

    // An ordinary Sub output is a hierarchy alias, not an executable value
    // operation.  Occurrence traversal usually erases that GraphIO boundary,
    // but a direct instance-to-instance connection (including a module output
    // fed back into another input of the same instance) can still surface the
    // Sub pin here.  Leaving it as producer_base creates an atomic "call"
    // version that depends on every instance input; on packed interfaces that
    // manufactures a whole-word cycle even when the callee's real output cone
    // is field-accurate and acyclic.  The direct color runtime cannot lower an
    // ordinary Sub version anyway: its members must be the callee's occurrence
    // nodes.  Resolve the alias to the exact GraphIO driver now.
    //
    // A LOOP subnode is the one Sub the direct runtime does lower (cgen_sim's
    // direct_color_supported accepts exactly `kind == loop_control`), and its
    // output is an iteration result, not a hierarchy alias: resolving it to a
    // body driver — or, through the graph-input arm below, to the loop's own
    // caller-side input — would erase the carry semantics.  Same exclusion the
    // sub_by_path_hash index above applies.
    for (int depth = 0;
         producer_literal.empty() && !top_input && producer_base != Color_plan::invalid_index && depth < 64
         && gu::type_op_of(plan.sites_[producer_base].node) == Ntype_op::Sub && !plan.sites_[producer_base].node.is_loop_subnode();
         ++depth) {
      const auto sub_occurrence = plan.sites_[producer_base].node;
      const auto sub            = sub_occurrence.base_node();
      const auto child          = sub.get_subnode_graph();
      const auto sio            = sub.get_subnode_io();
      if (child == nullptr || sio == nullptr) {
        break;
      }
      bool resolved = false;
      for (const auto& decl : sio->get_output_pin_decls()) {
        if (decl.port_id != producer_port) {
          continue;
        }
        if (output_boundary_width == 0) {
          output_boundary_width  = static_cast<uint32_t>(std::max<int32_t>(1, decl.bits));
          output_boundary_unsign = decl.unsign;
        }
        const auto output = child->get_output_pin(decl.name);
        for (auto output_edge_drv : output.get_driver_pins()) {
          if (gu::is_graph_input_pin(output_edge_drv)) {
            for (const auto& input_sink : sub_occurrence.inp_sorted_pins()) {
              if (input_sink.get_port_id() != output_edge_drv.get_port_id()) {
                continue;
              }
              for (const auto& input_drv : input_sink.get_driver_pins()) {
                const auto bound = index.find(input_drv.get_master_node().get_occurrence_index());
                top_input        = bound == index.end() && gu::is_graph_input_pin(input_drv);
                if (top_input || (bound != index.end() && plan.sites_[bound->second].live)) {
                  producer_base = top_input ? Color_plan::invalid_index : bound->second;
                  producer_port = input_drv.get_port_id();
                  resolved      = true;
                }
                break;  // only this port's FIRST driver, as the edge walk did
              }
              break;  // ...and then out of the whole walk, likewise
            }
          } else if (!output_edge_drv.is_const()) {
            const auto leaf = find_body_site(child.get(), sub_occurrence.path(), output_edge_drv.get_master_node());
            if (leaf && plan.sites_[*leaf].live) {
              producer_base = *leaf;
              producer_port = output_edge_drv.get_port_id();
              resolved      = true;
            }
          }
          if (resolved) {
            break;
          }
        }
        break;
      }
      if (!resolved) {
        break;
      }
    }

    // A positive contiguous Get_mask is an unsigned lane value. Once the
    // source has been resolved through any packed/child spelling above,
    // transport that lane LSB-aligned instead of rebuilding its whole packed
    // carrier at the boundary and extracting it again in the consumer. This
    // also applies to a top input: the public IO remains one packed object, but
    // each fixed internal view is a narrow direct-ABI lane.
    if (producer_literal.empty() && consumer_op == Ntype_op::Get_mask
        && use_sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Get_mask, "a") && lo >= 0 && hi > lo) {
      producer_extract_lo = static_cast<uint32_t>(lo);
      producer_extract_hi = static_cast<uint32_t>(hi);
      producer_shift      = 0;
      producer_width      = producer_extract_hi - producer_extract_lo;
      preextracted        = true;
    }

    const size_t producer = (top_input || !producer_literal.empty()) ? Color_plan::invalid_index
                                                                     : producer_version(producer_base, version, producer_port);
    const auto   key      = std::tuple{producer,
                                       consumer,
                                       producer_port,
                                       producer_shift,
                                       producer_extract_lo,
                                       producer_extract_hi,
                                       use_sink.get_port_id(),
                                       consumer_input,
                                       version,
                                       top_input};
    if (!exact_value_uses.insert(key).second) {
      return;
    }
    const uint32_t width
        = producer_width != 0 ? producer_width : static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(use_driver)));
    uint32_t consumer_width = preextracted ? width : (output_boundary_width != 0 ? output_boundary_width : width);
    // Occurrence traversal resolves a definition-local GraphIO input directly
    // to its caller-side producer. Preserve the declared port width at that
    // erased boundary: a signed N-bit producer is commonly represented by an
    // N+1-bit expression, and carrying that extra sign bit into a child's
    // packed cone can overlap the adjacent field. The module-local scheduler
    // performed this truncation when assigning child.__in; the direct ABI must
    // make the same cast explicit in its slot shape. A pre-extracted Get_mask
    // is the exception: its binding intentionally replaces the packed input
    // with the selected lane, so restoring the definition input width would
    // widen that lane before the identity Get_mask consumes it.
    if (!preextracted) {
      uint32_t definition_input = 0;
      for (auto base_edge_sink : consumer_base.node.base_node().inp_sorted_pins()) {
        for (auto base_edge_drv : base_edge_sink.get_driver_pins()) {
          if (definition_input++ != consumer_input) {
            continue;
          }
          if (gu::is_graph_input_pin(base_edge_drv)) {
            consumer_width = static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(base_edge_drv)));
          }
          break;
        }
      }
    }
    plan.value_uses_.push_back(
        Value_use{producer,
                  consumer,
                  version,
                  producer_port,
                  producer_shift,
                  producer_extract_lo,
                  producer_extract_hi,
                  use_sink.get_port_id(),
                  consumer_input,
                  width,
                  consumer_width,
                  preextracted ? true : (output_boundary_width != 0 ? output_boundary_unsign : gu::is_unsign(use_driver)),
                  top_input,
                  preextracted,
                  producer_literal});
    if (!top_input && producer_literal.empty()) {
      add_version_edge(producer, consumer, consumer_width);
    }
  };

  // A design whose state is only rising-edge (no latch, nothing on the fall)
  // needs no separate rise-commit evaluation for its flops: a flop's capture
  // reads exactly the pre-rise values, and its commit still runs at the rise
  // barrier after every slot-0/1 color. Evaluating the capture in pre-rise-eval
  // lets it fuse with its own input cone instead of receiving every next-state
  // value through a stored boundary slot (axi_demux: 92 of them). Memories keep
  // rise-commit: their staged-write/forwarding order is a separate protocol.
  bool rise_only = true;
  for (size_t base = 0; base < plan.sites_.size() && rise_only; ++base) {
    if (!plan.sites_[base].live || plan.sites_[base].kind != Site_kind::state) {
      continue;
    }
    if (gu::type_op_of(plan.sites_[base].node) == Ntype_op::Latch) {
      rise_only = false;
      break;
    }
    const auto rising = update_on_rise(plan.sites_[base].node, clocks, plan.summary_.versioning_complete);
    rise_only         = !rising || *rising;
  }

  // Every state endpoint gets one update action. Its input cone is evaluated
  // at the version immediately before that endpoint's closing/active edge.
  for (size_t base = 0; base < plan.sites_.size(); ++base) {
    if (!plan.sites_[base].live || plan.sites_[base].kind != Site_kind::state) {
      continue;
    }
    const auto rising = update_on_rise(plan.sites_[base].node, clocks, plan.summary_.versioning_complete);
    if (!rising) {
      continue;
    }
    state_rising[base]                 = *rising;
    const State_version  input_version = *rising ? State_version::pre_rise : State_version::post_rise;
    const auto           state_op      = gu::type_op_of(plan.sites_[base].node);
    const bool           fused_capture = rise_only && (state_op == Ntype_op::Flop || state_op == Ntype_op::Fflop);
    const Execution_slot commit_slot   = !*rising       ? Execution_slot::fall_commit
                                         : fused_capture ? Execution_slot::pre_rise_eval
                                                         : Execution_slot::rise_commit;
    const size_t         update        = ensure_version(base, input_version, Version_role::state_update, commit_slot, 0);
    state_updates[base]                = update;
    // consumer_input still counts one per DRIVER, which is what one per edge
    // was: it indexes the same operand list add_value_use replays.
    uint32_t consumer_input            = 0;
    for (const auto& edge_sink : plan.sites_[base].node.inp_sorted_pins()) {
      for (const auto& edge_drv : edge_sink.get_driver_pins()) {
        if (is_timing_input(edge_sink)) {
          ++consumer_input;
          continue;
        }
        add_value_use(edge_drv, edge_sink, update, consumer_input++, input_version);
      }
    }
  }

  // A latch that closes on the same edge as one of its readers is transparent
  // through that edge: the reader samples the latch's staged value, not its
  // previous persistent Q. Keep both updates in the same execution slot, but
  // order the latch sample before the reader sample. The direct ABI binds that
  // exact occurrence edge to the latch's pending member; this edge is the
  // scheduler half of the contract.
  // Cache the upstream latch frontier once for every live combinational site.
  // The old implementation restarted an occurrence-edge DFS for every input
  // of every state endpoint. On a large acyclic design that repeatedly walked
  // the same cones (and repeatedly resolved the same hierarchy edges). The
  // dependency graph above already contains precisely those resolved edges,
  // so propagate sparse latch sets through it once instead.
  std::vector<std::vector<size_t>> same_edge_latches(plan.sites_.size());
  std::vector<std::vector<size_t>> comb_successors(plan.sites_.size());
  std::vector<uint32_t>            comb_indegree(plan.sites_.size(), 0);
  std::vector<uint64_t>            latch_width(plan.sites_.size(), 1);
  for (size_t site = 0; site < plan.sites_.size(); ++site) {
    if (gu::type_op_of(plan.sites_[site].node) != Ntype_op::Latch || state_updates[site] == std::numeric_limits<size_t>::max()) {
      continue;
    }
    for (const auto& edge : plan.sites_[site].node.out_edges()) {
      latch_width[site] = std::max<uint64_t>(latch_width[site], std::max<int32_t>(1, gu::bits_of(edge.driver)));
    }
  }
  for (const auto& dep : plan.dependencies_) {
    if (dep.cut || dep.producer == dep.consumer || plan.sites_[dep.consumer].kind == Site_kind::state) {
      continue;
    }
    if (gu::type_op_of(plan.sites_[dep.producer].node) == Ntype_op::Latch
        && state_updates[dep.producer] != std::numeric_limits<size_t>::max()) {
      same_edge_latches[dep.consumer].push_back(dep.producer);
      continue;
    }
    if (plan.sites_[dep.producer].kind == Site_kind::state) {
      continue;
    }
    comb_successors[dep.producer].push_back(dep.consumer);
    ++comb_indegree[dep.consumer];
  }
  for (auto& latches : same_edge_latches) {
    std::ranges::sort(latches);
    latches.erase(std::unique(latches.begin(), latches.end()), latches.end());
  }
  const auto merge_latches = [](std::vector<size_t>& destination, const std::vector<size_t>& source) {
    if (source.empty()) {
      return false;
    }
    std::vector<size_t> merged;
    merged.reserve(destination.size() + source.size());
    std::ranges::set_union(destination, source, std::back_inserter(merged));
    if (merged.size() == destination.size()) {
      return false;
    }
    destination = std::move(merged);
    return true;
  };
  std::queue<size_t> comb_ready;
  for (size_t site = 0; site < plan.sites_.size(); ++site) {
    if (plan.sites_[site].kind != Site_kind::state && comb_indegree[site] == 0) {
      comb_ready.push(site);
    }
  }
  while (!comb_ready.empty()) {
    const size_t producer = comb_ready.front();
    comb_ready.pop();
    for (const size_t consumer : comb_successors[producer]) {
      merge_latches(same_edge_latches[consumer], same_edge_latches[producer]);
      if (--comb_indegree[consumer] == 0) {
        comb_ready.push(consumer);
      }
    }
  }
  // A legal color plan is acyclic after state cuts. Keep discovery complete
  // for an invalid combinational SCC as well: sparse monotone propagation
  // reaches a fixed point without restoring the old per-endpoint DFS.
  std::queue<size_t> cyclic_ready;
  std::vector<bool>  cyclic_queued(plan.sites_.size(), false);
  for (size_t site = 0; site < plan.sites_.size(); ++site) {
    if (comb_indegree[site] != 0 && !same_edge_latches[site].empty()) {
      cyclic_ready.push(site);
      cyclic_queued[site] = true;
    }
  }
  while (!cyclic_ready.empty()) {
    const size_t producer = cyclic_ready.front();
    cyclic_ready.pop();
    cyclic_queued[producer] = false;
    for (const size_t consumer : comb_successors[producer]) {
      if (comb_indegree[consumer] == 0 || !merge_latches(same_edge_latches[consumer], same_edge_latches[producer])
          || cyclic_queued[consumer]) {
        continue;
      }
      cyclic_ready.push(consumer);
      cyclic_queued[consumer] = true;
    }
  }
  for (size_t consumer = 0; consumer < plan.sites_.size(); ++consumer) {
    const size_t consumer_update = state_updates[consumer];
    if (consumer_update == std::numeric_limits<size_t>::max()) {
      continue;
    }
    for (const auto& edge_sink : plan.sites_[consumer].node.inp_sorted_pins()) {
      for (const auto& edge_drv : edge_sink.get_driver_pins()) {
        const auto producer_it = index.find(edge_drv.get_master_node().get_occurrence_index());
        if (producer_it == index.end()) {
          continue;
        }
        const bool timing_path = is_timing_input(edge_sink);
        const auto add_latch   = [&](size_t latch) {
          const size_t latch_update = state_updates[latch];
          if (latch != consumer && (timing_path || latch_update < consumer_update)
              && plan.version_sites_[latch_update].slot == plan.version_sites_[consumer_update].slot) {
            add_version_edge(latch_update, consumer_update, latch_width[latch]);
          }
        };
        const size_t producer = producer_it->second;
        if (gu::type_op_of(plan.sites_[producer].node) == Ntype_op::Latch
            && state_updates[producer] != std::numeric_limits<size_t>::max()) {
          add_latch(producer);
        } else if (plan.sites_[producer].kind != Site_kind::state) {
          for (const size_t latch : same_edge_latches[producer]) {
            add_latch(latch);
          }
        }
      }
    }
  }
  // Clock-gate preparation inlines an ICG latch into the occurrence that owns
  // the derived clock. A state endpoint may sit in a nested child reached
  // through that clock, while its resolved timing edge stops at an intervening
  // Sub boundary and therefore does not expose the latch to the recursive walk
  // above. Closing latches in an ancestor occurrence are still scheduler state
  // actions: make them ready before any nested edge-triggered endpoint samples
  // its clock/data guards. Independent siblings remain unordered.
  struct Latch_path_node {
    std::map<Occurrence_step_key, size_t> children;
    std::vector<size_t>                   latches;
  };
  std::vector<Latch_path_node> latch_path_trie(1);
  for (size_t latch = 0; latch < plan.sites_.size(); ++latch) {
    if (gu::type_op_of(plan.sites_[latch].node) != Ntype_op::Latch || state_updates[latch] == std::numeric_limits<size_t>::max()) {
      continue;
    }
    size_t trie_node = 0;
    for (const auto& step : plan.sites_[latch].node.path().steps()) {
      const auto key   = occurrence_step_key(step);
      const auto found = latch_path_trie[trie_node].children.find(key);
      if (found == latch_path_trie[trie_node].children.end()) {
        const size_t child = latch_path_trie.size();
        latch_path_trie[trie_node].children.emplace(key, child);
        latch_path_trie.emplace_back();
        trie_node = child;
      } else {
        trie_node = found->second;
      }
    }
    latch_path_trie[trie_node].latches.push_back(latch);
  }
  for (size_t consumer = 0; consumer < plan.sites_.size(); ++consumer) {
    if (gu::type_op_of(plan.sites_[consumer].node) == Ntype_op::Latch
        || state_updates[consumer] == std::numeric_limits<size_t>::max()) {
      continue;
    }
    const auto add_ancestor_latches = [&](size_t trie_node) {
      for (const size_t latch : latch_path_trie[trie_node].latches) {
        if (latch != consumer
            && plan.version_sites_[state_updates[latch]].slot == plan.version_sites_[state_updates[consumer]].slot) {
          add_version_edge(state_updates[latch], state_updates[consumer], 0);
        }
      }
    };
    size_t trie_node = 0;
    add_ancestor_latches(trie_node);
    for (const auto& step : plan.sites_[consumer].node.path().steps()) {
      const auto found = latch_path_trie[trie_node].children.find(occurrence_step_key(step));
      if (found == latch_path_trie[trie_node].children.end()) {
        break;
      }
      trie_node = found->second;
      add_ancestor_latches(trie_node);
    }
  }

  // Every occurrence output has two pinned observation versions: during-period
  // is the pre-rise cone; the public/testbench slot is the post-fall settled
  // cone.  The root uses its public Out surface and child occurrences use their
  // internal observation surfaces.
  const auto resolve_body_input
      = [&](const Body_occurrence& body, hhds::Port_id input_port) -> std::optional<hhds::Occurrence_pin> {
    // A Sub occurrence carries the same call path as the nodes in its child
    // body. Its occurrence drivers have already crossed the child's GraphIO, so
    // they are the authoritative binding even when that input is used only by
    // a pure input-to-output alias and no ordinary child site mentions it.
    for (const auto& site : plan.sites_) {
      if ((site.kind != Site_kind::instance && site.kind != Site_kind::conditional_control) || site.node.path() != body.path
          || site.node.get_subnode_gid() != body.graph->get_gid()) {
        continue;
      }
      for (const auto& edge_sink : site.node.inp_sorted_pins()) {
        if (edge_sink.get_port_id() != input_port) {
          continue;
        }
        for (const auto& edge_drv : edge_sink.get_driver_pins()) {
          return edge_drv;  // a `return` is unaffected by the extra loop
        }
      }
    }
    return std::nullopt;
  };
  for (const auto& body : bodies) {
    const auto body_io = body.graph == nullptr ? nullptr : body.graph->get_io();
    if (!body_io) {
      continue;
    }
    const bool is_top_body = body.graph == root && body.path.steps().empty();
    if (!include_observations && !is_top_body) {
      continue;
    }
    for (const auto& decl : body_io->get_output_pin_decls()) {
      const auto output            = body.graph->get_output_pin(decl.name);
      const auto add_output_driver = [&](const auto& driver) {
        if (driver.is_const()) {
          const auto& constant = gu::const_of(driver);
          const auto  literal  = constant.to_pyrope();
          if (constant.has_unknowns()) {
            if (std::getenv("LIVEHD_SIM_COLOR_DEBUG") != nullptr) {
              std::fprintf(stderr,
                           "[color-direct] unknown literal output port=%u bits=%d value=%s\n",
                           static_cast<unsigned>(decl.port_id),
                           decl.bits,
                           literal.c_str());
            }
            plan.summary_.versioning_complete = false;
            plan.errors_.emplace_back("a literal-only output contains runtime-unknown bits");
            return;
          }
          const auto add_literal_output = [&](State_version version) {
            output_uses.push_back(Output_use{decl.port_id,
                                             body.anchor,
                                             is_top_body,
                                             Color_plan::invalid_index,
                                             driver.get_port_id(),
                                             version,
                                             static_cast<uint32_t>(std::max<int32_t>(1, decl.bits)),
                                             decl.unsign,
                                             literal});
          };
          add_literal_output(State_version::pre_rise);
          add_literal_output(State_version::post_fall);
          return;
        }
        std::optional<size_t> site;
        if constexpr (std::is_same_v<std::decay_t<decltype(driver)>, hhds::Occurrence_pin>) {
          if (const auto found = index.find(driver.get_master_node().get_occurrence_index()); found != index.end()) {
            site = found->second;
          }
        } else {
          site = find_body_driver_site(body.graph, body.path, driver.get_master_node());
        }
        if (!site || !plan.sites_[*site].live) {
          if (gu::is_graph_input_pin(driver) && is_top_body) {
            // A pure GraphIO alias has no data site. Represent it as an output
            // slot owned directly by the top input; cgen.sim publishes both
            // observation versions while refreshing root inputs.
            const auto add_input_alias = [&](State_version version) {
              output_uses.push_back(Output_use{decl.port_id,
                                               body.anchor,
                                               is_top_body,
                                               Color_plan::invalid_index,
                                               driver.get_port_id(),
                                               version,
                                               static_cast<uint32_t>(std::max<int32_t>(1, decl.bits)),
                                               decl.unsign,
                                               {}});
            };
            add_input_alias(State_version::pre_rise);
            add_input_alias(State_version::post_fall);
          } else if (gu::is_graph_input_pin(driver)) {
            const auto resolved = resolve_body_input(body, driver.get_port_id());
            if (!resolved) {
              plan.summary_.versioning_complete = false;
              plan.errors_.emplace_back(std::format("site-free child GraphIO alias `{}.{}` has no occurrence input binding",
                                                    body.graph->get_name(),
                                                    decl.name));
              return;
            }
            if (resolved->is_const()) {
              const auto& constant = gu::const_of(*resolved);
              if (constant.has_unknowns()) {
                plan.summary_.versioning_complete = false;
                plan.errors_.emplace_back("a literal-only output contains runtime-unknown bits");
                return;
              }
              const auto literal = constant.to_pyrope();
              for (const auto version : {State_version::pre_rise, State_version::post_fall}) {
                output_uses.push_back(Output_use{decl.port_id,
                                                 body.anchor,
                                                 false,
                                                 Color_plan::invalid_index,
                                                 resolved->get_port_id(),
                                                 version,
                                                 static_cast<uint32_t>(std::max<int32_t>(1, decl.bits)),
                                                 decl.unsign,
                                                 literal});
              }
              return;
            }
            const auto producer_it = index.find(resolved->get_master_node().get_occurrence_index());
            const bool top_input = producer_it == index.end() && gu::is_graph_input_pin(*resolved) && resolved->get_graph() == root;
            if (!top_input && (producer_it == index.end() || !plan.sites_[producer_it->second].live)) {
              plan.summary_.versioning_complete = false;
              plan.errors_.emplace_back(std::format("site-free child GraphIO alias `{}.{}` resolves to no live producer",
                                                    body.graph->get_name(),
                                                    decl.name));
              return;
            }
            for (const auto version : {State_version::pre_rise, State_version::post_fall}) {
              const size_t producer
                  = top_input ? Color_plan::invalid_index : producer_version(producer_it->second, version, resolved->get_port_id());
              output_uses.push_back(Output_use{decl.port_id,
                                               body.anchor,
                                               false,
                                               producer,
                                               resolved->get_port_id(),
                                               version,
                                               static_cast<uint32_t>(std::max<int32_t>(1, decl.bits)),
                                               decl.unsign,
                                               {}});
            }
          }
          return;
        }
        const auto add_output_use = [&](State_version version) {
          const size_t producer = producer_version(*site, version, driver.get_port_id());
          output_uses.push_back(Output_use{decl.port_id,
                                           body.anchor,
                                           is_top_body,
                                           producer,
                                           driver.get_port_id(),
                                           version,
                                           static_cast<uint32_t>(std::max<int32_t>(1, decl.bits)),
                                           decl.unsign,
                                           {}});
        };
        add_output_use(State_version::pre_rise);
        add_output_use(State_version::post_fall);
      };
      if (is_top_body) {
        for (auto edge_drv : discovery.lift(output).get_driver_pins()) {
          add_output_driver(edge_drv);
        }
      } else {
        for (auto edge_drv : output.get_driver_pins()) {
          add_output_driver(edge_drv);
        }
      }
    }
  }

  // Preserve used child-input names without retaining module-call handoffs.
  // A definition edge sourced by one of this body's GraphIO inputs identifies
  // the public port; the matching occurrence edge already resolves through
  // ordinary hierarchy to its real producer.  The direct evaluator reads that
  // producer directly.  The observation binding below only materializes the
  // settled alias that query/probe currently publish.
  for (size_t body_i = 0; body_i < bodies.size(); ++body_i) {
    if (!include_observations) {
      break;
    }
    const auto& body = bodies[body_i];
    if (body.graph == root && body.path.steps().empty()) {
      continue;
    }
    const auto body_io = body.graph == nullptr ? nullptr : body.graph->get_io();
    if (!body_io) {
      continue;
    }
    std::string prefix;
    if (library != nullptr) {
      prefix = hhds::format_occurrence_path(*library, body.path);
      if (!prefix.empty()) {
        prefix += ".";
      }
    }
    absl::flat_hash_map<hhds::Port_id, hhds::Occurrence_pin> resolved_inputs;
    for (const size_t site_index : body_sites[body_i]) {
      const auto& site = plan.sites_[site_index];
      if (!site.live) {
        continue;
      }
      absl::flat_hash_map<hhds::Class_index, hhds::Occurrence_pin> occurrence_drivers;
      for (const auto& edge_sink : site.node.inp_sorted_pins()) {
        for (const auto& edge_drv : edge_sink.get_driver_pins()) {
          // try_emplace keeps the FIRST driver of a sink, exactly as it did
          // over the edge list, and the sorted walk keeps that first one first.
          occurrence_drivers.try_emplace(edge_sink.base_pin().get_class_index(), edge_drv);
        }
      }
      for (auto base_edge_sink : site.node.base_node().inp_sorted_pins()) {
        for (auto base_edge_drv : base_edge_sink.get_driver_pins()) {
          if (!gu::is_graph_input_pin(base_edge_drv)) {
            continue;
          }
          if (const auto found = occurrence_drivers.find(base_edge_sink.get_class_index()); found != occurrence_drivers.end()) {
            resolved_inputs.try_emplace(base_edge_drv.get_port_id(), found->second);
          }
        }
      }
    }
    for (const auto& decl : body_io->get_input_pin_decls()) {
      const auto                          body_input = body.graph->get_input_pin(decl.name);
      const auto                          binding    = resolved_inputs.find(decl.port_id);
      std::optional<hhds::Occurrence_pin> resolved;
      if (binding != resolved_inputs.end()) {
        resolved = binding->second;
      }
      if (!resolved) {
        plan.observations_.push_back(Observation{prefix + decl.name, "unbound", true, decl.port_id});
        continue;
      }
      const auto producer_it = index.find(resolved->get_master_node().get_occurrence_index());
      const bool top_input   = producer_it == index.end() && gu::is_graph_input_pin(*resolved) && resolved->get_graph() == root;
      const bool constant    = resolved->is_const();
      if (!top_input && !constant && (producer_it == index.end() || !plan.sites_[producer_it->second].live)) {
        plan.observations_.push_back(Observation{prefix + decl.name, "unbound", true, decl.port_id});
        continue;
      }
      const std::string literal = constant ? gu::const_of(*resolved).to_pyrope() : std::string{};
      for (const auto version : {State_version::pre_rise, State_version::post_fall}) {
        const size_t producer = (top_input || constant) ? Color_plan::invalid_index
                                                        : producer_version(producer_it->second, version, resolved->get_port_id());
        input_uses.push_back(Input_use{prefix + decl.name,
                                       body.anchor,
                                       producer,
                                       resolved->get_port_id(),
                                       decl.port_id,
                                       version,
                                       static_cast<uint32_t>(std::max<int32_t>(1, decl.bits)),
                                       gu::is_unsign(body_input),
                                       top_input,
                                       literal});
      }
      const auto source_id = top_input  ? std::format("top-input:p{}", resolved->get_port_id())
                             : constant ? stable_id("literal:" + literal)
                                        : plan.version_sites_[input_uses.back().producer].structural_id;
      plan.observations_.push_back(Observation{prefix + decl.name, source_id, true, decl.port_id});
    }
  }

  // Back-propagate each required version through its combinational cone. A
  // state Q is an anchor at that version; its update cone is scheduled only by
  // the explicit state-update site above.
  while (!version_pending.empty()) {
    const size_t consumer_version = version_pending.front();
    version_pending.pop();
    // add_value_use may append producer versions and reallocate this vector;
    // keep the queued consumer by value across those insertions.
    const auto  consumer_site = plan.version_sites_[consumer_version];
    const auto& base_site     = plan.sites_[consumer_site.base_site];
    if (gu::type_op_of(base_site.node) == Ntype_op::Memory) {
      struct Port_shape {
        bool rd      = false;
        bool present = false;
        int  rdidx   = -1;
        int  wridx   = -1;
        int  dout    = -1;
      };
      std::vector<Port_shape> ports;
      hhds::Occurrence_pin    fwd_matrix;
      hhds::Occurrence_pin    undef_matrix;
      bool                    registered = false;
      bool                    whole      = false;
      int                     type       = 2;
      for (const auto& edge_sink : base_site.node.inp_sorted_pins()) {
        const int    raw  = static_cast<int>(edge_sink.get_port_id());
        const auto   name = Ntype::get_sink_name(Ntype_op::Memory, raw);
        const size_t port = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
        for (const auto& edge_drv : edge_sink.get_driver_pins()) {
          if (name == "type" && edge_drv.is_const()) {
            type = static_cast<int>(gu::const_of(edge_drv).to_just_i64());
          } else if (name == "fwd") {
            fwd_matrix = edge_drv;
          } else if (name == "undef") {
            undef_matrix = edge_drv;
          } else if (name == "update") {
            whole = true;
          } else if (name == "update_enable" || name == "reset" || name == "initial" || name == "bits" || name == "size"
                     || name == "wensize" || name == "posclk") {
            // Cell-global pins share the raw 0..15 block with port zero. Keep
            // them out of the per-port table before suffix matching: notably,
            // `update_enable` is not read-port zero's `enable`.
          } else if (name.ends_with("clock_pin")) {
            registered = true;
          } else {
            if (ports.size() <= port) {
              ports.resize(port + 1);
            }
            if (name.ends_with("addr")) {
              ports[port].present = true;
            } else if (name.ends_with("rdport") && edge_drv.is_const()) {
              ports[port].rd = !gu::const_of(edge_drv).is_known_false();
            }
          }
        }
      }
      int writes = 0;
      for (const auto& port : ports) {
        writes += port.present && !port.rd;
      }
      int read  = 0;
      int write = 0;
      for (auto& port : ports) {
        if (!port.present) {
          continue;
        }
        if (port.rd) {
          port.rdidx = read;
          port.dout  = writes + read++;
        } else {
          port.wridx = write++;
        }
      }
      const auto row_prefix = [&](const hhds::Occurrence_pin& matrix, int row) {
        if (matrix.is_invalid() || !matrix.is_const()) {
          return 0;
        }
        const auto& value  = gu::const_of(matrix);
        int         prefix = 0;
        while (prefix < writes && value.bit_test(row * writes + prefix)) {
          ++prefix;
        }
        return prefix;
      };
      const Port_shape* target = nullptr;
      for (const auto& port : ports) {
        if (port.rd && static_cast<hhds::Port_id>(port.dout) == consumer_site.output_port) {
          target = &port;
          break;
        }
      }
      int prefix = 0;
      if (target != nullptr && consumer_site.version == State_version::pre_rise && type != 1) {
        prefix = registered ? std::max(row_prefix(fwd_matrix, target->rdidx), row_prefix(undef_matrix, target->rdidx)) : writes;
      }
      // consumer_input keeps ticking once per DRIVER, the same operand index
      // the per-edge walk produced.
      uint32_t consumer_input = 0;
      for (const auto& edge_sink : base_site.node.inp_sorted_pins()) {
        const int    raw  = static_cast<int>(edge_sink.get_port_id());
        const auto   name = Ntype::get_sink_name(Ntype_op::Memory, raw);
        const size_t port = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
        bool         used = false;
        if (whole && !registered && (consumer_site.output_port == Ntype::Memory_readall_pid || target != nullptr)) {
          used = name == "update" || name == "update_enable" || name == "reset" || name == "initial";
        }
        if (target != nullptr && type != 1 && port < ports.size()) {
          const auto& shape        = ports[port];
          const bool  port_enable  = name != "update_enable" && name.ends_with("enable");
          used                    |= (&shape == target && (name.ends_with("addr") || port_enable))
                                     || (!shape.rd && shape.wridx >= 0 && shape.wridx < prefix
                                         && (name.ends_with("addr") || port_enable || name.ends_with("din")));
        }
        for (const auto& edge_drv : edge_sink.get_driver_pins()) {
          if (used) {
            add_value_use(edge_drv, edge_sink, consumer_version, consumer_input, consumer_site.version);
          }
          ++consumer_input;
        }
      }
      continue;
    }
    uint32_t consumer_input = 0;
    for (const auto& edge_sink : base_site.node.inp_sorted_pins()) {
      for (const auto& edge_drv : edge_sink.get_driver_pins()) {
        // Only the carry SELF-edge stays inside the native kernel; the parent's
        // seed of that carry is an ordinary value use (see the liveness walk).
        // That seed is the SECOND driver of this one sink, which is why the
        // walk reads the plural get_driver_pins() and tests each driver.
        if (base_site.kind == Site_kind::loop_control && is_loop_carry_input(base_site.node, edge_sink)
            && edge_drv.get_master_node() == base_site.node) {
          ++consumer_input;
          continue;
        }
        add_value_use(edge_drv, edge_sink, consumer_version, consumer_input++, consumer_site.version);
      }
    }
  }

  // State-version transitions are scheduler dependencies created by the
  // emitter, not LGraph data edges. A rise update produces this state's
  // post-rise/post-fall reads; a fall update produces its post-fall read.
  for (size_t base = 0; base < plan.sites_.size(); ++base) {
    if (!state_rising[base] || state_updates[base] == std::numeric_limits<size_t>::max()) {
      continue;
    }
    const auto connect_read = [&](State_version version) {
      const auto role = gu::type_op_of(plan.sites_[base].node) == Ntype_op::Memory ? Version_role::data : Version_role::state_read;
      for (const size_t read : versions_by_base[base]) {
        const auto& candidate = plan.version_sites_[read];
        if (candidate.version == version && candidate.role == role) {
          add_version_edge(state_updates[base], read, 0);
        }
      }
    };
    if (*state_rising[base]) {
      connect_read(State_version::post_rise);
    }
    connect_read(State_version::post_fall);
  }

  // Memory read colors may share the transient forwarding protocol object,
  // but that storage hazard is not a value dependency. In particular, write
  // data for a low-numbered read port can depend on a higher-numbered read of
  // the same array. A synthetic port-order chain would turn that legal data
  // flow into a false combinational cycle. The multi-worker emitter protects
  // each complete memory-evaluation color with one runtime critical section;
  // the serial backend needs neither an edge nor a lock.

  // Validate and order the fine-color DAG. Slot order is a hard legality
  // constraint even when two colors have no explicit value edge; the later
  // Schedule construction uses one barrier/control boundary between slots
  // instead of materializing a quadratic all-to-all precedence relation.
  std::vector<uint32_t>                                 remaining_consumers(plan.version_sites_.size(), 0);
  std::vector<std::vector<std::pair<size_t, uint64_t>>> fanins(plan.version_sites_.size());
  for (const auto& edge : plan.version_dependencies_) {
    ++remaining_consumers[edge.producer];
    fanins[edge.consumer].emplace_back(edge.producer, edge.boundary_bits);
    if (static_cast<uint8_t>(plan.version_sites_[edge.producer].slot)
        > static_cast<uint8_t>(plan.version_sites_[edge.consumer].slot)) {
      plan.summary_.versioning_complete = false;
      plan.errors_.push_back(std::format("version dependency {} ({}) -> {} ({}) points backward across execution slots",
                                         plan.version_sites_[edge.producer].structural_id,
                                         execution_slot_name(plan.version_sites_[edge.producer].slot),
                                         plan.version_sites_[edge.consumer].structural_id,
                                         execution_slot_name(plan.version_sites_[edge.consumer].slot)));
    }
  }
  std::vector<std::pair<std::string_view, hhds::Occurrence_path>> ordered_paths;
  ordered_paths.reserve(shape_cache.paths.size());
  for (const auto& [path, shape] : shape_cache.paths) {
    ordered_paths.emplace_back(shape, path);
  }
  // The SHAPE alone is not a total order: two occurrences of the same
  // definition share it exactly (the kernel-reuse case). The source is an
  // absl::flat_hash_map, whose iteration order is randomized PER PROCESS, and
  // std::ranges::sort is not stable — so a shape tie left path_rank, and with it
  // execution_order / color ids / emitted kernel order, different on every run
  // of the same input. Break the tie on the path's own structure so the plan and
  // the emitted C++ are reproducible (the incremental digest depends on it).
  const auto path_before = [](const hhds::Occurrence_path& lhs, const hhds::Occurrence_path& rhs) {
    if (lhs.root_gid() != rhs.root_gid()) {
      return lhs.root_gid() < rhs.root_gid();
    }
    const auto a = lhs.steps();
    const auto b = rhs.steps();
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
      if (a[i].subnode.gid != b[i].subnode.gid) {
        return a[i].subnode.gid < b[i].subnode.gid;
      }
      if (a[i].subnode.value != b[i].subnode.value) {
        return a[i].subnode.value < b[i].subnode.value;
      }
      if (a[i].ordinal != b[i].ordinal) {
        return a[i].ordinal < b[i].ordinal;  // std::optional: nullopt (unrolled) sorts first
      }
    }
    return a.size() < b.size();
  };
  std::ranges::sort(ordered_paths, [&](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return path_before(lhs.second, rhs.second);
  });
  absl::flat_hash_map<hhds::Occurrence_path, size_t> path_rank;
  for (size_t rank = 0; rank < ordered_paths.size(); ++rank) {
    path_rank.emplace(ordered_paths[rank].second, rank);
  }
  std::vector<size_t> site_path_rank(plan.sites_.size());
  for (size_t site = 0; site < plan.sites_.size(); ++site) {
    site_path_rank[site] = path_rank.at(plan.sites_[site].node.path());
  }
  const auto later = [&](size_t lhs, size_t rhs) {
    const auto& a = plan.version_sites_[lhs];
    const auto& b = plan.version_sites_[rhs];
    if (a.slot != b.slot) {
      return a.slot < b.slot;  // Finish the latest phase first in the backward walk.
    }
    return std::tie(site_path_rank[a.base_site], a.structural_id, lhs)
           > std::tie(site_path_rank[b.base_site], b.structural_id, rhs);
  };
  std::priority_queue<size_t, std::vector<size_t>, decltype(later)> ready(later);
  for (size_t i = 0; i < remaining_consumers.size(); ++i) {
    if (remaining_consumers[i] == 0) {
      ready.push(i);
    }
  }
  // Reverse Kahn: start at outputs and pending state, and visit a fanin only
  // after all its consumers have been scheduled. Prefer newly ready fanins in
  // the same occurrence, with the widest dependency first. This keeps the
  // sink's cone together without traversing any edge more than once.
  uint64_t order          = 0;
  size_t   preferred      = Color_plan::invalid_index;
  uint64_t preferred_bits = 0;
  while (preferred != Color_plan::invalid_index || !ready.empty()) {
    size_t current = preferred;
    preferred      = Color_plan::invalid_index;
    preferred_bits = 0;
    if (current == Color_plan::invalid_index) {
      current = ready.top();
      ready.pop();
    }
    plan.version_sites_[current].execution_order = plan.version_sites_.size() - 1 - order++;
    std::vector<std::pair<size_t, uint64_t>> newly_ready;
    for (const auto& [successor, bits] : fanins[current]) {
      if (--remaining_consumers[successor] == 0) {
        newly_ready.emplace_back(successor, bits);
      }
    }
    for (const auto& [successor, bits] : newly_ready) {
      const auto& current_site   = plan.version_sites_[current];
      const auto& successor_site = plan.version_sites_[successor];
      const bool  same_surface   = current_site.slot == successor_site.slot
                                   && site_path_rank[current_site.base_site] == site_path_rank[successor_site.base_site];
      if (!same_surface) {
        ready.push(successor);
        continue;
      }
      if (preferred == Color_plan::invalid_index || bits > preferred_bits
          || (bits == preferred_bits && later(preferred, successor))) {
        if (preferred != Color_plan::invalid_index) {
          ready.push(preferred);
        }
        preferred      = successor;
        preferred_bits = bits;
      } else {
        ready.push(successor);
      }
    }
  }
  if (order != plan.version_sites_.size()) {
    plan.summary_.version_dag_acyclic    = false;
    plan.summary_.versioning_complete    = false;
    constexpr size_t max_cycle_witnesses = 32;
    std::string      blocked;
    size_t           blocked_count = 0;
    for (size_t i = 0; i < remaining_consumers.size(); ++i) {
      if (remaining_consumers[i] != 0) {
        if (blocked_count < max_cycle_witnesses) {
          blocked += (blocked.empty() ? "" : ",") + plan.version_sites_[i].structural_id;
        }
        ++blocked_count;
      }
    }
    // WHICH cycle, not merely that there is one. The witness list alone is a
    // wall of structural hashes: measured on XiangShan `Backend`, 307,612
    // blocked sites out of 2.95 M, and nothing in the report says what the loop
    // is MADE of — so nobody can tell a missing cut from a real combinational
    // loop in the design. Everything downstream (700k dominance errors, the
    // cyclic condensation, the refusal to lower the module at all) is one
    // domino chain from this single line, so this line has to carry the
    // evidence.
    //
    // Extract ONE concrete cycle from the residual subgraph — every site with
    // remaining_consumers>0 after Kahn stalls is on or upstream of a cycle, so an
    // iterative DFS following only residual edges is guaranteed to close a
    // loop — and report the slot/role/version sequence around it. That is the
    // shape a fix acts on: a cycle whose every hop is the same role is a
    // missing cut of that role, while a mixed one is a genuine dependency.
    std::string cycle_desc;
    {
      // CSR, not a bucket per site. `version_sites_` runs to 2.95 M on exactly
      // the designs this diagnostic exists for, while only the BLOCKED sites
      // (307 k, ~10%) carry a residual edge -- so a vector-per-site pays a
      // 24-byte vector header for all 2.95 M plus one malloc per non-empty
      // bucket plus growth reallocs, where two flat arrays pay (N+1)+E size_t
      // in two allocations. Counts, inclusive prefix, then a REVERSE fill with
      // a decrementing cursor, so each bucket ends up in version_dependencies_
      // order -- the same successor order a push_back build gave, hence the
      // same cycle reported.
      const size_t n_sites     = plan.version_sites_.size();
      const auto   is_residual = [&remaining_consumers](const Color_plan::Version_dependency& dep) {
        return remaining_consumers[dep.consumer] != 0 && remaining_consumers[dep.producer] != 0;
      };
      std::vector<size_t> residual_at(n_sites + 1, 0);  // residual_at[i]..residual_at[i+1] indexes residual_to
      size_t              n_residual = 0;
      for (const auto& dep : plan.version_dependencies_) {
        if (is_residual(dep)) {
          ++residual_at[dep.producer];
          ++n_residual;
        }
      }
      for (size_t i = 1; i < n_sites; ++i) {
        residual_at[i] += residual_at[i - 1];
      }
      residual_at[n_sites] = n_residual;
      std::vector<size_t> residual_to(n_residual);
      for (auto it = plan.version_dependencies_.rbegin(); it != plan.version_dependencies_.rend(); ++it) {
        if (is_residual(*it)) {
          residual_to[--residual_at[it->producer]] = it->consumer;
        }
      }

      // Iterative DFS for a BACK EDGE, which is the only sound way to name a
      // cycle here. A greedy walk is not: the residual subgraph contains sites
      // that are merely DOWNSTREAM of the cycle, so following any out-edge can
      // dead-end, and reporting the path at that point invents a cycle that is
      // not there. (It did: the first version of this reported "cycle-length=1"
      // for XiangShan `Backend`, which sent a fix at self-edges that turned out
      // not to exist — self-edges-dropped came back 0.)
      //
      // gray = on the current DFS path, black = fully explored. An edge into a
      // gray node closes a real cycle; an edge into a black node does not.
      enum : char { kWhite = 0, kGray = 1, kBlack = 2 };
      std::vector<char>   dfs_color(n_sites, kWhite);
      std::vector<size_t> path;
      std::vector<size_t> back_edge;  // the closing cycle, once found
      for (size_t seed = 0; seed < remaining_consumers.size() && back_edge.empty(); ++seed) {
        if (remaining_consumers[seed] == 0 || dfs_color[seed] != kWhite) {
          continue;
        }
        // (site, next-successor-index) frames, so the walk is heap-allocated
        // rather than a recursion over a 3 M-node graph.
        std::vector<std::pair<size_t, size_t>> stack{
            {seed, 0}
        };
        dfs_color[seed] = kGray;
        path.clear();
        path.push_back(seed);
        while (!stack.empty() && back_edge.empty()) {
          auto& [at, next] = stack.back();
          if (next >= residual_at[at + 1] - residual_at[at]) {
            dfs_color[at] = kBlack;
            stack.pop_back();
            path.pop_back();
            continue;
          }
          const size_t to = residual_to[residual_at[at] + next++];
          if (dfs_color[to] == kGray) {  // back edge: `to` .. `at` -> `to` is a cycle
            const auto begin = std::find(path.begin(), path.end(), to);
            back_edge.assign(begin, path.end());
            break;
          }
          if (dfs_color[to] == kWhite) {
            dfs_color[to] = kGray;
            path.push_back(to);
            stack.emplace_back(to, 0);
          }
        }
      }
      if (!back_edge.empty()) {
        // Name the ring in terms a person can act on: the OP, the node name and
        // the occurrence path of each hop, plus the port that carries each
        // edge. The structural ids alone are hashes — enough to prove a cycle
        // exists, useless for deciding whether it is real. A ring whose hops
        // are all the same op on the same module, connected through the same
        // port, is a granularity artifact; one that walks through unrelated
        // logic is a genuine combinational loop in the design.
        constexpr size_t                                            kMaxHops = 64;
        const size_t                                                shown    = std::min(back_edge.size(), kMaxHops);
        // The producer_port/consumer_port pair of the base dependency that
        // carries each printed hop, when there is one (a version edge may
        // summarize several uses; the first is representative for shape).
        // Collected in ONE pass over plan.dependencies_: that vector runs to
        // millions of entries on exactly the designs this diagnostic exists for,
        // so a full scan per hop is a scan too many.
        absl::flat_hash_map<std::pair<size_t, size_t>, std::string> via_of;
        for (size_t i = 0; i < back_edge.size(); ++i) {
          const auto& site = plan.sites_[plan.version_sites_[back_edge[i]].base_site];
          if (i < shown || gu::type_op_of(site.node) == Ntype_op::Sub) {
            via_of.try_emplace(
                {plan.version_sites_[back_edge[i]].base_site, plan.version_sites_[back_edge[(i + 1) % back_edge.size()]].base_site},
                std::string{});
            if (gu::type_op_of(site.node) == Ntype_op::Sub) {
              via_of.try_emplace({plan.version_sites_[back_edge[(i + back_edge.size() - 1) % back_edge.size()]].base_site,
                                  plan.version_sites_[back_edge[i]].base_site},
                                 std::string{});
            }
          }
        }
        for (const auto& dep : plan.dependencies_) {
          auto it = via_of.find(std::pair<size_t, size_t>{dep.producer, dep.consumer});
          if (it != via_of.end() && it->second.empty()) {
            it->second = std::format(" via p{}->p{}/{}", dep.producer_port, dep.consumer_port, dependency_kind_name(dep.kind));
          }
        }
        // Only the PRINTED hops read this one (the boundary lines below use
        // via_of), so seed it for those alone: back_edge runs to the same
        // millions the dependency vector does.
        absl::flat_hash_map<std::pair<size_t, size_t>, std::string> value_via;
        for (size_t i = 0; i < shown; ++i) {
          value_via.try_emplace({back_edge[i], back_edge[(i + 1) % back_edge.size()]}, std::string{});
        }
        for (const auto& use : plan.value_uses_) {
          auto it = value_via.find(std::pair<size_t, size_t>{use.producer_version, use.consumer_version});
          if (it != value_via.end() && it->second.empty()) {
            it->second = std::format(" value=p{} shift={} extract=[{},{}) -> p{}#{} width={}/{} pre={}",
                                     use.producer_port,
                                     use.producer_shift,
                                     use.producer_extract_lo,
                                     use.producer_extract_hi,
                                     use.consumer_port,
                                     use.consumer_input,
                                     use.width,
                                     use.consumer_width,
                                     use.preextracted);
          }
        }
        // An occurrence node may carry no owning Graph (the same nullable the
        // driver-boundary walk above guards); this diagnostic must never be the
        // thing that crashes the compile it is explaining.
        const auto graph_name_of = [](const hhds::Occurrence_node& n) -> std::string_view {
          const auto* owner = n.get_graph();
          return owner == nullptr ? std::string_view{"?"} : owner->get_name();
        };
        std::string hops;
        for (size_t i = 0; i < shown; ++i) {
          const size_t idx  = back_edge[i];
          const size_t next = back_edge[(i + 1) % back_edge.size()];
          const auto&  vs   = plan.version_sites_[idx];
          const auto&  site = plan.sites_[vs.base_site];
          hops += std::format("\n    {:>2}. {} op={} name={} graph={} path={} nid={} depth={} ge={} role={} version={} slot={}{}{}",
                              i,
                              vs.structural_id.substr(0, 10),
                              Ntype::get_name(gu::type_op_of(site.node)),
                              gu::has_name(site.node.base_node()) ? gu::node_name_of(site.node) : std::string_view{"-"},
                              graph_name_of(site.node),
                              library == nullptr ? std::string{} : hhds::format_occurrence_path(*library, site.node.path()),
                              static_cast<unsigned long long>(site.node.get_debug_nid() >> 2),
                              site.node.path().steps().size(),
                              site.gate_equivalents,
                              version_role_name(vs.role),
                              state_version_name(vs.version),
                              execution_slot_name(vs.slot),
                              via_of[std::pair<size_t, size_t>{vs.base_site, plan.version_sites_[next].base_site}],
                              value_via[std::pair<size_t, size_t>{idx, next}]);
        }
        // Op histogram over the WHOLE ring, not just the printed prefix: the
        // single most diagnostic number here is "how many distinct ops", because
        // a one-op ring is a tracking artifact and a many-op ring is real logic.
        absl::flat_hash_map<std::string_view, size_t> op_hist;
        for (size_t idx : back_edge) {
          op_hist[Ntype::get_name(gu::type_op_of(plan.sites_[plan.version_sites_[idx].base_site].node))]++;
        }
        std::vector<std::pair<std::string_view, size_t>> ops(op_hist.begin(), op_hist.end());
        // Tie-break on the NAME: the source is an absl::flat_hash_map, whose
        // iteration order is randomized per process, and std::ranges::sort is not
        // stable -- so counting alone would print a different `cycle-ops=` on
        // every run of the same input (the same hazard the path_rank sort above
        // documents).
        std::ranges::sort(ops, [](const auto& a2, const auto& b2) {
          return a2.second != b2.second ? a2.second > b2.second : a2.first < b2.first;
        });
        std::string hist;
        for (const auto& [name, n] : ops) {
          hist += std::format("{}{}x{}", hist.empty() ? "" : ",", n, name);
        }
        // Keep every hierarchy boundary in the witness even when it falls
        // beyond the truncated hop prefix.  A packed-word false cycle is
        // actionable at the Sub port where field-grain reachability was lost;
        // hiding that boundary behind "... (truncated)" leaves only anonymous
        // bitwise plumbing in the user-facing report.
        std::string boundaries;
        for (size_t i = 0; i < back_edge.size(); ++i) {
          const size_t idx  = back_edge[i];
          const size_t next = back_edge[(i + 1) % back_edge.size()];
          const size_t prev = back_edge[(i + back_edge.size() - 1) % back_edge.size()];
          const auto&  site = plan.sites_[plan.version_sites_[idx].base_site];
          if (gu::type_op_of(site.node) != Ntype_op::Sub) {
            continue;
          }
          const auto out_via
              = via_of.find(std::pair<size_t, size_t>{plan.version_sites_[idx].base_site, plan.version_sites_[next].base_site});
          const auto in_via
              = via_of.find(std::pair<size_t, size_t>{plan.version_sites_[prev].base_site, plan.version_sites_[idx].base_site});
          const auto  output_port = plan.version_sites_[idx].output_port;
          const auto  child       = site.node.base_node().get_subnode_graph();
          const auto& reach       = port_reach.of(child);
          const auto  slices      = reach.out_slices.find(static_cast<uint32_t>(output_port));
          const auto  inputs      = reach.out2ins.find(static_cast<uint32_t>(output_port));
          std::string output_name{"?"};
          if (const auto sio = site.node.base_node().get_subnode_io()) {
            for (const auto& decl : sio->get_output_pin_decls()) {
              if (decl.port_id == output_port) {
                output_name = decl.name;
                break;
              }
            }
          }
          boundaries += std::format("\n      {} graph={} instance={} depth={} output=p{}:{} slices={} support={} in={} out={}",
                                    plan.version_sites_[idx].structural_id.substr(0, 10),
                                    graph_name_of(site.node),
                                    gu::has_name(site.node.base_node()) ? gu::node_name_of(site.node) : std::string_view{"-"},
                                    site.node.path().steps().size(),
                                    output_port,
                                    output_name,
                                    slices == reach.out_slices.end() ? 0 : slices->second.size(),
                                    inputs == reach.out2ins.end() ? 0 : inputs->second.size(),
                                    in_via == via_of.end() ? std::string{} : in_via->second,
                                    out_via == via_of.end() ? std::string{} : out_via->second);
        }
        cycle_desc = std::format("; cycle-length={} cycle-ops={} cycle-boundaries={} cycle={}{}",
                                 back_edge.size(),
                                 hist,
                                 boundaries.empty() ? "none" : boundaries,
                                 hops,
                                 back_edge.size() > shown ? "\n    ... (truncated)" : "");
      } else {
        cycle_desc = "; cycle=NONE FOUND (blocked sites are upstream of one, or the residual graph is malformed)";
      }
    }
    plan.errors_.push_back(std::format(
        "fine-color dependency cycle remains after state and compact-loop carry cuts; blocked-count={} witnesses={}{}{}",
        blocked_count,
        blocked,
        blocked_count > max_cycle_witnesses ? ",..." : "",
        cycle_desc));
  }
  plan.summary_.version_sites = plan.version_sites_.size();
  plan.summary_.version_edges = plan.version_dependencies_.size();
  plan.summary_.fine_colors   = plan.version_sites_.size();

  // Assign each version to its outermost structural conditional region.  A
  // prefix trie keeps this linear in total occurrence-path length on designs
  // such as Minion, where scanning 13K conditional calls for each of 768K
  // versions would be prohibitive.  The owner is a scheduling contract, not a
  // name heuristic: it comes solely from grouped occurrence paths and the
  // canonical __valid boundary classification above.
  struct Control_trie_node {
    std::map<Occurrence_step_key, size_t> children;
    size_t                                owner = Color_plan::invalid_index;
  };
  std::vector<Control_trie_node> control_trie(1);
  for (size_t site_index = 0; site_index < plan.sites_.size(); ++site_index) {
    if (plan.sites_[site_index].kind != Site_kind::conditional_control) {
      continue;
    }
    size_t trie_node = 0;
    for (const auto& step : plan.sites_[site_index].node.path().steps()) {
      const auto key   = occurrence_step_key(step);
      const auto found = control_trie[trie_node].children.find(key);
      if (found == control_trie[trie_node].children.end()) {
        const size_t child = control_trie.size();
        control_trie[trie_node].children.emplace(key, child);
        control_trie.emplace_back();
        trie_node = child;
      } else {
        trie_node = found->second;
      }
    }
    if (control_trie[trie_node].owner == Color_plan::invalid_index) {
      control_trie[trie_node].owner = site_index;
    }
  }
  std::vector<size_t> site_control_owner(plan.sites_.size(), Color_plan::invalid_index);
  for (size_t site_index = 0; site_index < plan.sites_.size(); ++site_index) {
    size_t owner     = Color_plan::invalid_index;
    size_t trie_node = 0;
    for (const auto& step : plan.sites_[site_index].node.path().steps()) {
      const auto found = control_trie[trie_node].children.find(occurrence_step_key(step));
      if (found == control_trie[trie_node].children.end()) {
        break;
      }
      trie_node = found->second;
      if (owner == Color_plan::invalid_index && control_trie[trie_node].owner != Color_plan::invalid_index) {
        owner = control_trie[trie_node].owner;
      }
    }
    site_control_owner[site_index] = owner;
  }
  for (auto& version : plan.version_sites_) {
    version.control_owner = site_control_owner[version.base_site];
  }

  // sim.tune support tables (occurrences() / support()). HERE, before the
  // fence decision and the coarsener: everything they read (versions, value
  // uses, the version DAG and its order, path ranks) is final by now and
  // nothing below it depends on sim.tune.dirty / fence / live_words / backend,
  // so the tables -- and the root's <stem>.tune.cpp -- are vector-invariant.
  {
    std::vector<bool> compact_body(plan.sites_.size());
    for (size_t site = 0; site < plan.sites_.size(); ++site) {
      compact_body[site] = under_compact_loop(plan.sites_[site]);
    }
    plan.build_tune_tables(root, versions_by_base, site_path_rank, compact_body);
  }

  // Preserve useful ordinary module boundaries, rather than every wrapper.
  // A sizeable near-leaf body or a module with a narrow interface is a cheap
  // cut and a useful reuse unit. Tiny helpers can still fuse into their caller.
  struct Module_shape {
    size_t   sites           = 0;
    size_t   height          = 0;
    uint64_t interface_words = 0;
  };
  absl::flat_hash_map<hhds::Gid, Module_shape> module_shapes;
  absl::flat_hash_set<hhds::Gid>               active_modules;
  std::function<Module_shape(hhds::Graph*)>    module_shape = [&](hhds::Graph* graph) -> Module_shape {
    if (const auto it = module_shapes.find(graph->get_gid()); it != module_shapes.end()) {
      return it->second;
    }
    if (!active_modules.insert(graph->get_gid()).second) {
      return {};  // recursive hierarchy is already diagnosed by discovery
    }
    Module_shape shape;
    if (auto mod_io = graph->get_io()) {
      for (const auto& port : mod_io->get_input_pin_decls()) {
        shape.interface_words += (static_cast<uint64_t>(std::max<uint32_t>(1, port.bits)) + 63) / 64;
      }
      for (const auto& port : mod_io->get_output_pin_decls()) {
        shape.interface_words += (static_cast<uint64_t>(std::max<uint32_t>(1, port.bits)) + 63) / 64;
      }
    }
    for (auto node : graph->body().nodes()) {
      if (gu::is_builtin_node(node)) {
        continue;
      }
      ++shape.sites;
      if (gu::type_op_of(node) == Ntype_op::Sub && !node.is_loop_subnode()) {
        if (auto child = node.get_subnode_graph()) {
          const auto nested  = module_shape(child.get());
          shape.sites       += nested.sites;
          shape.height       = std::max(shape.height, nested.height + 1);
        }
      }
    }
    active_modules.erase(graph->get_gid());
    module_shapes.emplace(graph->get_gid(), shape);
    return shape;
  };
  // A fence is a trade-off. It lets dirty-bit gating skip a mostly idle
  // module (xs_alu's AluDataModule sees constant operands: 26 MHz fenced vs
  // 11 MHz fused; dino's register file and issue unit idle once the program
  // spins: 3.36 vs 2.64 MHz; XS RenameTable's tables under a read-only driver:
  // 61 vs 39 kHz), but every value crossing it is a stored, change-tested,
  // dirty-marked slot, and the interval contraction below cuts a new color at
  // every owner switch -- pure cost when the module toggles every cycle (an
  // LFSR-driven lhdtrack DUT: br_enc_gray2bin 4.4 -> 46 MHz unfenced). Skipping
  // an idle module saves work in proportion to its SITES; its seam costs in
  // proportion to its INTERFACE WORDS. A module used once (no shared kernel)
  // therefore keeps its fence only when sites / interface_words reaches
  // `fence_ratio` (sim.tune.fence; 0 = always). Reused or very large bodies
  // always keep it.
  constexpr size_t kSingleBodyFenceSites = 1024;
  const int64_t    single_body_ratio     = fence_ratio >= 0 ? fence_ratio : kDefaultFenceRatio;
  absl::flat_hash_map<const hhds::Graph*, size_t> body_occurrences;
  for (const auto& body : bodies) {
    if (!body.path.steps().empty() && body.graph != nullptr) {
      ++body_occurrences[body.graph];
    }
  }
  std::vector<Control_trie_node> module_trie(1);
  for (const auto& body : bodies) {
    if (body.path.steps().empty() || body.graph == nullptr) {
      continue;
    }
    const auto shape = module_shape(body.graph);
    if (fence_ratio == kNoFences || shape.sites < 32 || (shape.height > 1 && shape.interface_words > 20)) {
      continue;
    }
    // A single-occurrence body keeps its fence only when it is big for its seam.
    const bool single = body_occurrences[body.graph] < 2 && shape.sites < kSingleBodyFenceSites;
    const bool fenced
        = !single || static_cast<int64_t>(shape.sites) >= single_body_ratio * static_cast<int64_t>(std::max<uint64_t>(1, shape.interface_words));
    if (!fenced) {
      continue;
    }
    size_t trie_node = 0;
    for (const auto& step : body.path.steps()) {
      const auto key   = occurrence_step_key(step);
      const auto found = module_trie[trie_node].children.find(key);
      if (found == module_trie[trie_node].children.end()) {
        const size_t child = module_trie.size();
        module_trie[trie_node].children.emplace(key, child);
        module_trie.emplace_back();
        trie_node = child;
      } else {
        trie_node = found->second;
      }
    }
    module_trie[trie_node].owner = trie_node;
  }
  std::vector<size_t> site_module_owner(plan.sites_.size(), Color_plan::invalid_index);
  for (size_t site = 0; site < plan.sites_.size(); ++site) {
    size_t trie_node = 0;
    for (const auto& step : plan.sites_[site].node.path().steps()) {
      const auto found = module_trie[trie_node].children.find(occurrence_step_key(step));
      if (found == module_trie[trie_node].children.end()) {
        break;
      }
      trie_node = found->second;
      if (module_trie[trie_node].owner != Color_plan::invalid_index) {
        site_module_owner[site] = module_trie[trie_node].owner;
      }
    }
  }

  // Contract consecutive intervals of the reverse topological order. A path
  // between two interval members cannot leave and reenter that interval, so
  // contraction preserves an acyclic color graph without reachability searches.
  const size_t                     nversions = plan.version_sites_.size();
  std::vector<size_t>              parent(nversions);
  std::vector<std::vector<size_t>> members(nversions);
  std::vector<uint64_t>            color_ge(nversions, 0);
  for (size_t i = 0; i < nversions; ++i) {
    parent[i] = i;
    members[i].push_back(i);
    color_ge[i] = plan.sites_[plan.version_sites_[i].base_site].gate_equivalents;
  }
  const auto root_of = [&](size_t node) {
    while (parent[node] != node) {
      node = parent[node];
    }
    return node;
  };

  constexpr uint64_t kSoftColorGe  = 50'000;
  const auto         runtime_owner = [&](size_t version) {
    const auto& site         = plan.sites_[plan.version_sites_[version].base_site];
    const auto  op           = gu::type_op_of(site.node);
    // Keep the loop opaque and indivisible, but let its surrounding cones
    // share the color instead of forcing their values through stored slots.
    const bool  separate_sub = op == Ntype_op::Sub && site.kind != Site_kind::loop_control;
    return (separate_sub || (separate_runtime_calls && op == Ntype_op::Clock_cell)) ? plan.version_sites_[version].base_site
                                                                                    : Color_plan::invalid_index;
  };
  const auto crosses_control_boundary = [&](size_t lhs, size_t rhs) {
    const auto lhs_version = members[lhs].front();
    const auto rhs_version = members[rhs].front();
    return plan.version_sites_[lhs_version].control_owner != plan.version_sites_[rhs_version].control_owner
           || runtime_owner(lhs_version) != runtime_owner(rhs_version)
           || site_module_owner[plan.version_sites_[lhs_version].base_site]
                  != site_module_owner[plan.version_sites_[rhs_version].base_site];
  };
  // Track distinct physical values, not edge uses. Repeated uses of one input
  // cost one live value; a wide scalar costs ceil(width/64) machine words.
  // Outputs remain live through the color ABI, while internal values die at
  // their last scheduled use. Constants are rematerialized and cost no input.
  using Pressure_key = std::tuple<size_t, hhds::Port_id, uint32_t, uint32_t, uint32_t, uint32_t, bool, bool, uint8_t>;
  struct Pressure_value {
    size_t   producer;
    uint64_t words;
    uint64_t last_use;
  };
  absl::flat_hash_map<Pressure_key, size_t> pressure_index;
  std::vector<Pressure_value>               pressure_values;
  std::vector<std::vector<size_t>>          pressure_inputs(nversions), pressure_outputs(nversions);
  const auto                                pressure_value = [&](const Pressure_key& key, uint64_t last_use) {
    const auto [it, inserted] = pressure_index.try_emplace(key, pressure_values.size());
    if (inserted) {
      const auto producer = std::get<0>(key);
      pressure_values.push_back({producer, (static_cast<uint64_t>(std::get<5>(key)) + 63) / 64, last_use});
      if (producer != Color_plan::invalid_index && plan.version_sites_[producer].role != Version_role::state_read) {
        pressure_outputs[producer].push_back(it->second);
      }
    } else {
      pressure_values[it->second].last_use = std::max(pressure_values[it->second].last_use, last_use);
    }
    return it->second;
  };
  for (const auto& use : plan.value_uses_) {
    if (!use.literal.empty()) {
      continue;
    }
    const auto producer = use.top_input ? Color_plan::invalid_index : use.producer_version;
    const auto pressure_slot
        = producer == Color_plan::invalid_index || plan.version_sites_[producer].role == Version_role::state_read
              ? plan.version_sites_[use.consumer_version].slot
              : plan.version_sites_[producer].slot;
    const auto id = pressure_value({producer,
                                    use.producer_port,
                                    use.producer_shift,
                                    use.producer_extract_lo,
                                    use.producer_extract_hi,
                                    use.width,
                                    use.unsign,
                                    use.top_input,
                                    static_cast<uint8_t>(pressure_slot)},
                                   plan.version_sites_[use.consumer_version].execution_order);
    pressure_inputs[use.consumer_version].push_back(id);
  }
  for (const auto& output : output_uses) {
    if (output.producer == Color_plan::invalid_index || !output.literal.empty()) {
      continue;
    }
    const auto id = pressure_value({output.producer,
                                    output.producer_port,
                                    0,
                                    0,
                                    0,
                                    output.width,
                                    output.unsign,
                                    false,
                                    static_cast<uint8_t>(plan.version_sites_[output.producer].slot)},
                                   nversions);
    if (plan.version_sites_[output.producer].role == Version_role::state_read) {
      pressure_inputs[output.producer].push_back(id);
    }
  }
  // Pending register values are ABI outputs even without a Value_use edge.
  for (size_t version = 0; version < nversions; ++version) {
    const auto& site = plan.version_sites_[version];
    if (site.role != Version_role::state_update) {
      continue;
    }
    const auto node = plan.sites_[site.base_site].node.base_node();
    if (gu::type_op_of(node) == Ntype_op::Memory) {
      continue;  // memory helpers operate on storage, not a register-sized array value
    }
    const auto pin = node.get_driver_pin(site.output_port);
    if (!pin.is_invalid()) {
      pressure_value({version,
                      site.output_port,
                      0,
                      0,
                      0,
                      static_cast<uint32_t>(std::max(1, gu::bits_of(pin))),
                      gu::is_unsign(pin),
                      false,
                      static_cast<uint8_t>(site.slot)},
                     nversions);
    }
  }
  for (auto& inputs : pressure_inputs) {
    std::ranges::sort(inputs);
    inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());
  }
  std::vector<uint64_t> color_peak_words(nversions), color_input_words(nversions);
  if (plan.summary_.version_dag_acyclic) {
    std::vector<size_t> execution_order(nversions);
    std::iota(execution_order.begin(), execution_order.end(), 0);
    std::ranges::sort(execution_order, {}, [&](size_t version) { return plan.version_sites_[version].execution_order; });
    size_t                      current        = Color_plan::invalid_index;
    uint64_t                    start_order    = 0;
    uint64_t                    input_words    = 0;
    uint64_t                    frontier_words = 0;
    absl::flat_hash_set<size_t> frontier;
    for (const size_t version : execution_order) {
      const auto version_order = plan.version_sites_[version].execution_order;
      const auto reset_color   = [&] {
        current        = version;
        start_order    = version_order;
        input_words    = 0;
        frontier_words = 0;
        frontier.clear();
      };
      const bool can_join = current != Color_plan::invalid_index && color_peak_words[current] <= plan.live_word_budget_
                            && plan.version_sites_[members[current].front()].slot == plan.version_sites_[version].slot
                            && !crosses_control_boundary(current, version)
                            && color_ge[current] <= kSoftColorGe - std::min<uint64_t>(color_ge[version], kSoftColorGe);
      if (!can_join) {
        reset_color();
      }
      const auto external = [&](size_t id) {
        const auto producer = pressure_values[id].producer;
        return producer == Color_plan::invalid_index || plan.version_sites_[producer].role == Version_role::state_read
               || plan.version_sites_[producer].execution_order < start_order;
      };
      const auto additional_inputs = [&] {
        uint64_t words = 0;
        for (const auto id : pressure_inputs[version]) {
          if (external(id) && !frontier.contains(id)) {
            words += pressure_values[id].words;
          }
        }
        return words;
      };
      uint64_t output_words = 0;
      for (const auto id : pressure_outputs[version]) {
        output_words += pressure_values[id].words;
      }
      auto new_inputs = additional_inputs();
      if (current != version && new_inputs + frontier_words + output_words > plan.live_word_budget_) {
        reset_color();
        new_inputs = additional_inputs();
      }
      input_words                += new_inputs;
      color_input_words[current]  = input_words;
      color_peak_words[current]   = std::max(color_peak_words[current], new_inputs + frontier_words + output_words);
      for (const auto id : pressure_inputs[version]) {
        if (external(id)) {
          if (frontier.insert(id).second) {
            frontier_words += pressure_values[id].words;
          }
        }
        if (pressure_values[id].last_use == version_order && frontier.erase(id)) {
          frontier_words -= pressure_values[id].words;
        }
      }
      for (const auto id : pressure_outputs[version]) {
        if (pressure_values[id].last_use > version_order && frontier.insert(id).second) {
          frontier_words += pressure_values[id].words;
        }
      }
      // An indivisible wide operation or loop call may exceed the estimate.
      // Keep it as a singleton; a register budget must never unroll a loop.
      if (current != version) {
        parent[version] = current;
        members[current].push_back(version);
        members[version].clear();
        color_ge[current] += color_ge[version];
        color_ge[version]  = 0;
      }
    }
  }

  std::vector<size_t> roots;
  for (size_t i = 0; i < nversions; ++i) {
    if (parent[i] == i && !members[i].empty()) {
      roots.push_back(i);
    }
  }
  struct Pending_color {
    size_t root = 0;
    Color  color;
  };
  std::vector<Pending_color> pending_colors;
  pending_colors.reserve(roots.size());
  for (const size_t root_index : roots) {
    auto color_members = members[root_index];
    std::ranges::sort(color_members, [&](size_t a, size_t b) {
      // Member index breaks the tie: automorphic version sites share a
      // structural_id BY DESIGN, and this order sets version_position, which is
      // the tiebreak in the canonical rank sort.
      return std::tie(plan.version_sites_[a].structural_id, a) < std::tie(plan.version_sites_[b].structural_id, b);
    });
    std::string signature = std::string(execution_slot_name(plan.version_sites_[color_members.front()].slot));
    for (const size_t member : color_members) {
      signature += ":" + plan.version_sites_[member].structural_id;
    }
    Pending_color pending_color;
    pending_color.root                   = root_index;
    pending_color.color.structural_id    = stable_id(signature);
    pending_color.color.slot             = plan.version_sites_[color_members.front()].slot;
    pending_color.color.members          = std::move(color_members);
    pending_color.color.gate_equivalents = color_ge[root_index];
    pending_color.color.peak_live_words  = color_peak_words[root_index];
    pending_color.color.live_in_words    = color_input_words[root_index];
    pending_colors.push_back(std::move(pending_color));
  }
  std::ranges::sort(pending_colors, [](const Pending_color& a, const Pending_color& b) {
    // Root index breaks the tie: same legitimate-tie argument, and this sort
    // assigns the dense color indices.
    return std::tie(a.color.slot, a.color.structural_id, a.root) < std::tie(b.color.slot, b.color.structural_id, b.root);
  });
  std::vector<size_t> root_to_color(nversions, std::numeric_limits<size_t>::max());
  for (auto& pending_color : pending_colors) {
    root_to_color[pending_color.root] = plan.colors_.size();
    plan.colors_.push_back(std::move(pending_color.color));
  }
  absl::flat_hash_map<std::pair<size_t, size_t>, uint64_t> color_edge_bits;
  for (const auto& edge : plan.version_dependencies_) {
    const size_t producer = root_to_color[root_of(edge.producer)];
    const size_t consumer = root_to_color[root_of(edge.consumer)];
    if (producer != consumer) {
      color_edge_bits[{producer, consumer}] += edge.boundary_bits;
    }
  }
  for (const auto& [edge, bits] : color_edge_bits) {
    plan.color_dependencies_.push_back(Color_dependency{edge.first, edge.second, bits});
  }
  std::ranges::sort(plan.color_dependencies_, [](const Color_dependency& a, const Color_dependency& b) {
    return std::tie(a.producer, a.consumer) < std::tie(b.producer, b.consumer);
  });

  // Materialize the direct ABI only after coarsening, when cross-color values
  // are known. Version precedence and storage are deliberately separate:
  // state transitions add DAG edges but no value slot, while one producer value
  // can feed several consumer colors through one producer-owned slot.
  const auto color_of_version = [&](size_t version) {
    return version == Color_plan::invalid_index ? Color_plan::invalid_index : root_to_color[root_of(version)];
  };
  std::map<std::string, size_t> slot_index;
  const auto                    ensure_slot = [&](std::string   signature,
                                                  Boundary_kind kind,
                                                  State_version version,
                                                  size_t        owner_site,
                                                  size_t        producer_version_index,
                                                  size_t        producer_color,
                                                  hhds::Port_id producer_port,
                                                  hhds::Port_id public_port,
                                                  uint32_t      width,
                                                  bool          unsign) {
    if (const auto it = slot_index.find(signature); it != slot_index.end()) {
      auto& slot = plan.boundary_slots_[it->second];
      if (slot.kind != kind || slot.owner_site != owner_site || slot.producer_version != producer_version_index
          || slot.producer_color != producer_color || slot.producer_port != producer_port || slot.public_port != public_port
          || slot.width != width || slot.unsign != unsign) {
        plan.summary_.boundary_one_writer = false;
        plan.summary_.versioning_complete = false;
        plan.errors_.push_back(
            std::format("direct-ABI slot signature={} conflicts: "
                        "old(kind={},owner={},producer={},color={},producer-port={},public-port={},width={},unsign={}) "
                        "new(kind={},owner={},producer={},color={},producer-port={},public-port={},width={},unsign={})",
                        signature,
                        boundary_kind_name(slot.kind),
                        slot.owner_site,
                        slot.producer_version,
                        slot.producer_color,
                        slot.producer_port,
                        slot.public_port,
                        slot.width,
                        slot.unsign,
                        boundary_kind_name(kind),
                        owner_site,
                        producer_version_index,
                        producer_color,
                        producer_port,
                        public_port,
                        width,
                        unsign));
      }
      return it->second;
    }
    Boundary_slot slot;
    slot.structural_id    = stable_id(signature);
    slot.kind             = kind;
    slot.version          = version;
    slot.owner_site       = owner_site;
    slot.producer_version = producer_version_index;
    slot.producer_color   = producer_color;
    slot.producer_port    = producer_port;
    slot.public_port      = public_port;
    slot.width            = width;
    slot.unsign           = unsign;
    const size_t result   = plan.boundary_slots_.size();
    plan.boundary_slots_.push_back(std::move(slot));
    slot_index.emplace(std::move(signature), result);
    return result;
  };
  const auto add_consumer = [&](size_t        slot_index_value,
                                size_t        version,
                                hhds::Port_id port,
                                uint32_t      input,
                                uint32_t      consumer_width,
                                bool          preextracted) {
    auto&                   slot = plan.boundary_slots_[slot_index_value];
    const Boundary_consumer consumer{version, color_of_version(version), port, input, consumer_width, preextracted};
    const auto              duplicate = std::ranges::find_if(slot.consumers, [&](const Boundary_consumer& existing) {
      return std::tie(existing.version_site, existing.color, existing.port, existing.input, existing.width, existing.preextracted)
             == std::tie(consumer.version_site,
                         consumer.color,
                         consumer.port,
                         consumer.input,
                         consumer.width,
                         consumer.preextracted);
    });
    if (duplicate == slot.consumers.end()) {
      slot.consumers.push_back(consumer);
    }
  };

  // Current state is persistent ABI storage even when its only consumer is a
  // root output or another state's pending input. Declare it from the state
  // occurrence itself, rather than inferring its existence from one use.
  for (size_t base = 0; base < plan.sites_.size(); ++base) {
    if (!plan.sites_[base].live || plan.sites_[base].kind != Site_kind::state) {
      continue;
    }
    if (gu::type_op_of(plan.sites_[base].node) == Ntype_op::Memory) {
      continue;  // the array object owns persistent storage; read ports are versioned data values
    }
    // A state Q with no occurrence-level consumer (for example a state that is
    // only checkpoint-visible) may have no lifted out_pins(). The cell's base
    // interface still defines its persistent current/pending ABI, so fall back
    // to the canonical state Q port rather than inferring storage from fanout.
    std::vector<hhds::Pin_class> state_outputs;
    for (const auto& output : plan.sites_[base].node.base_node().out_pins()) {
      state_outputs.push_back(output);
    }
    if (state_outputs.empty()) {
      const auto q = plan.sites_[base].node.base_node().get_driver_pin(0);
      if (!q.is_invalid()) {
        state_outputs.push_back(q);
      }
    }
    for (const auto& output : state_outputs) {
      const auto   signature    = std::format("{}:current:p{}", plan.sites_[base].storage_id, output.get_port_id());
      const size_t update       = state_updates[base];
      const size_t current_slot = ensure_slot(signature,
                                              Boundary_kind::state_current,
                                              State_version::pre_rise,
                                              base,
                                              update,
                                              color_of_version(update),
                                              output.get_port_id(),
                                              output.get_port_id(),
                                              static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(output))),
                                              gu::is_unsign(output));
      for (const size_t version : versions_by_base[base]) {
        const auto& state_read = plan.version_sites_[version];
        if (state_read.base_site == base && state_read.role == Version_role::state_read) {
          add_consumer(current_slot,
                       version,
                       output.get_port_id(),
                       0,
                       static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(output))),
                       false);
        }
      }
      if (update != Color_plan::invalid_index) {
        const auto pending_signature = std::format("{}:pending:p{}", plan.sites_[base].storage_id, output.get_port_id());
        (void)ensure_slot(pending_signature,
                          Boundary_kind::state_pending,
                          plan.version_sites_[update].version,
                          base,
                          update,
                          color_of_version(update),
                          output.get_port_id(),
                          output.get_port_id(),
                          static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(output))),
                          gu::is_unsign(output));
      }
    }
  }

  for (const auto& use : plan.value_uses_) {
    const size_t consumer_color = color_of_version(use.consumer_version);
    size_t       source_slot    = Color_plan::invalid_index;
    size_t       producer_color = Color_plan::invalid_index;
    if (!use.literal.empty()) {
      const auto signature                      = std::format("literal:{}:b{}:u{}", use.literal, use.width, use.unsign);
      source_slot                               = ensure_slot(signature,
                                                              Boundary_kind::color_value,
                                                              State_version::pre_rise,
                                                              Color_plan::invalid_index,
                                                              Color_plan::invalid_index,
                                                              Color_plan::invalid_index,
                                                              0,
                                                              0,
                                                              use.width,
                                                              use.unsign);
      plan.boundary_slots_[source_slot].literal = use.literal;
    } else if (use.top_input) {
      const auto signature = std::format("top-input:p{}:b{}:u{}:x{}-{}",
                                         use.producer_port,
                                         use.width,
                                         use.unsign,
                                         use.producer_extract_lo,
                                         use.producer_extract_hi);
      source_slot          = ensure_slot(signature,
                                         Boundary_kind::top_input,
                                         State_version::pre_rise,
                                         Color_plan::invalid_index,
                                         Color_plan::invalid_index,
                                         Color_plan::invalid_index,
                                         use.producer_port,
                                         use.producer_port,
                                         use.width,
                                         use.unsign);
    } else {
      const auto& producer_site = plan.version_sites_[use.producer_version];
      const auto& producer_base = plan.sites_[producer_site.base_site];
      producer_color            = color_of_version(use.producer_version);
      if (producer_site.role == Version_role::state_read) {
        const auto physical_pin    = producer_base.node.base_node().get_driver_pin(use.producer_port);
        const auto physical_width  = static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(physical_pin)));
        const bool physical_unsign = gu::is_unsign(physical_pin);
        const bool natural_view
            = !use.preextracted && use.producer_shift == 0 && use.width == physical_width && use.unsign == physical_unsign;
        const auto   signature = natural_view ? std::format("{}:current:p{}", producer_base.storage_id, use.producer_port)
                                              : std::format("{}:current-view:p{}:shift{}:b{}:u{}:x{}-{}",
                                                            producer_base.storage_id,
                                                            use.producer_port,
                                                            use.producer_shift,
                                                            use.width,
                                                            use.unsign,
                                                            use.producer_extract_lo,
                                                            use.producer_extract_hi);
        const size_t update    = state_updates[producer_site.base_site];
        source_slot            = ensure_slot(signature,
                                             Boundary_kind::state_current,
                                             State_version::pre_rise,
                                             producer_site.base_site,
                                             update,
                                             color_of_version(update),
                                             use.producer_port,
                                             use.producer_port,
                                             use.width,
                                             use.unsign);
        plan.boundary_slots_[source_slot].producer_shift = use.producer_shift;
      } else if (producer_color != consumer_color) {
        const auto signature = std::format("{}:{}:{}:value:p{}:shift{}:b{}:u{}:x{}-{}",
                                           producer_base.storage_id,
                                           state_version_name(producer_site.version),
                                           version_role_name(producer_site.role),
                                           use.producer_port,
                                           use.producer_shift,
                                           use.width,
                                           use.unsign,
                                           use.producer_extract_lo,
                                           use.producer_extract_hi);
        source_slot          = ensure_slot(signature,
                                           Boundary_kind::color_value,
                                           producer_site.version,
                                           producer_site.base_site,
                                           use.producer_version,
                                           producer_color,
                                           use.producer_port,
                                           use.producer_port,
                                           use.width,
                                           use.unsign);
        auto& slot           = plan.boundary_slots_[source_slot];
        slot.producer_shift  = use.producer_shift;
      }
    }
    if (source_slot != Color_plan::invalid_index) {
      auto& slot = plan.boundary_slots_[source_slot];
      if (use.preextracted) {
        if ((slot.producer_extract_hi != 0 || slot.producer_extract_lo != 0)
            && (slot.producer_extract_lo != use.producer_extract_lo || slot.producer_extract_hi != use.producer_extract_hi)) {
          plan.summary_.boundary_one_writer = false;
          plan.summary_.versioning_complete = false;
          plan.errors_.push_back(std::format("direct-ABI slot {} has incompatible extraction ranges [{},{}) and [{},{})",
                                             slot.structural_id,
                                             slot.producer_extract_lo,
                                             slot.producer_extract_hi,
                                             use.producer_extract_lo,
                                             use.producer_extract_hi));
        }
        slot.producer_extract_lo = use.producer_extract_lo;
        slot.producer_extract_hi = use.producer_extract_hi;
        slot.producer_shift      = 0;
      }
      add_consumer(source_slot, use.consumer_version, use.consumer_port, use.consumer_input, use.consumer_width, use.preextracted);
    }
  }
  for (const auto& output : output_uses) {
    const auto   kind      = output.top ? Boundary_kind::top_output : Boundary_kind::observation_output;
    const auto   signature = output.top ? std::format("top-output:p{}:{}", output.public_port, state_version_name(output.version))
                                        : std::format("{}:observation-output:p{}:{}",
                                                      plan.sites_[output.body_anchor].storage_id,
                                                      output.public_port,
                                                      state_version_name(output.version));
    const size_t slot      = ensure_slot(signature,
                                         kind,
                                         output.version,
                                         output.top ? Color_plan::invalid_index : output.body_anchor,
                                         output.producer,
                                         color_of_version(output.producer),
                                         output.producer_port,
                                         output.public_port,
                                         output.width,
                                         output.unsign);
    if (!plan.boundary_slots_[slot].literal.empty() && plan.boundary_slots_[slot].literal != output.literal) {
      plan.summary_.boundary_one_writer = false;
      plan.summary_.versioning_complete = false;
      plan.errors_.emplace_back("an output slot has incompatible constant drivers");
    }
    plan.boundary_slots_[slot].literal = output.literal;
  }
  for (const auto& input : input_uses) {
    const auto   signature = std::format("{}:observation-input:p{}:{}",
                                         plan.sites_[input.body_anchor].storage_id,
                                         input.public_port,
                                         state_version_name(input.version));
    const size_t slot      = ensure_slot(signature,
                                         Boundary_kind::observation_input,
                                         input.version,
                                         input.body_anchor,
                                         input.producer,
                                         color_of_version(input.producer),
                                         input.producer_port,
                                         input.public_port,
                                         input.width,
                                         input.unsign);
    if (!plan.boundary_slots_[slot].literal.empty() && plan.boundary_slots_[slot].literal != input.literal) {
      plan.summary_.boundary_one_writer = false;
      plan.summary_.versioning_complete = false;
      plan.errors_.emplace_back("an observation-input slot has incompatible constant drivers");
    }
    plan.boundary_slots_[slot].literal = input.literal;
  }
  std::ranges::sort(plan.boundary_slots_, [](const Boundary_slot& a, const Boundary_slot& b) {
    // Exact tiebreak after the digest: two slots may legitimately share a
    // structural_id, and this order is observable downstream.
    return std::tie(a.structural_id, a.kind, a.owner_site, a.producer_version, a.producer_port, a.public_port, a.width,
                    a.unsign)
           < std::tie(b.structural_id, b.kind, b.owner_site, b.producer_version, b.producer_port, b.public_port, b.width,
                      b.unsign);
  });
  for (auto& slot : plan.boundary_slots_) {
    std::ranges::sort(slot.consumers, [](const Boundary_consumer& a, const Boundary_consumer& b) {
      return std::tie(a.color, a.version_site, a.port, a.input, a.width)
             < std::tie(b.color, b.version_site, b.port, b.input, b.width);
    });
    plan.summary_.boundary_bits += slot.width;
  }
  plan.summary_.value_uses     = plan.value_uses_.size();
  plan.summary_.boundary_slots = plan.boundary_slots_.size();

  std::vector<uint32_t>            color_indegree(plan.colors_.size(), 0);
  std::vector<std::vector<size_t>> color_next(plan.colors_.size());
  for (const auto& edge : plan.color_dependencies_) {
    ++color_indegree[edge.consumer];
    color_next[edge.producer].push_back(edge.consumer);
  }
  const auto color_later = [&](size_t lhs, size_t rhs) {
    return std::tie(plan.colors_[lhs].slot, plan.colors_[lhs].structural_id, lhs)
           > std::tie(plan.colors_[rhs].slot, plan.colors_[rhs].structural_id, rhs);
  };
  std::priority_queue<size_t, std::vector<size_t>, decltype(color_later)> color_ready(color_later);
  for (size_t i = 0; i < color_indegree.size(); ++i) {
    if (color_indegree[i] == 0) {
      color_ready.push(i);
    }
  }
  uint64_t color_order = 0;
  while (!color_ready.empty()) {
    const size_t current = color_ready.top();
    color_ready.pop();
    plan.colors_[current].execution_order = color_order++;
    for (const size_t successor : color_next[current]) {
      if (--color_indegree[successor] == 0) {
        color_ready.push(successor);
      }
    }
  }
  plan.summary_.color_dag_acyclic = color_order == plan.colors_.size();
  if (!plan.summary_.color_dag_acyclic) {
    plan.summary_.versioning_complete = false;
    plan.errors_.emplace_back("coarsened color condensation graph is cyclic");
  }
  for (const auto& slot : plan.boundary_slots_) {
    if (slot.producer_color == Color_plan::invalid_index || slot.kind == Boundary_kind::state_current) {
      continue;
    }
    for (const auto& consumer : slot.consumers) {
      if (consumer.color == Color_plan::invalid_index || consumer.color == slot.producer_color) {
        continue;
      }
      if (plan.colors_[slot.producer_color].execution_order >= plan.colors_[consumer.color].execution_order) {
        plan.summary_.boundary_dominance  = false;
        plan.summary_.versioning_complete = false;
        plan.errors_.push_back(std::format("direct-ABI slot {} writer {} order={} does not dominate consumer {} order={}",
                                           slot.structural_id,
                                           plan.colors_[slot.producer_color].structural_id,
                                           plan.colors_[slot.producer_color].execution_order,
                                           plan.colors_[consumer.color].structural_id,
                                           plan.colors_[consumer.color].execution_order));
      }
    }
  }

  // Canonical kernel classes. The 128-bit hash is only an anchor; reuse occurs
  // solely when the complete name/path-free serialization matches. Refinement
  // must also give every member a unique canonical rank. A true automorphism is
  // safe in principle, but this first verifier deliberately salts that color by
  // occurrence identity instead of guessing a correspondence: missed reuse is
  // allowed, incorrect reuse is not.
  struct Kernel_edge {
    size_t        producer       = 0;
    size_t        consumer       = 0;
    hhds::Port_id producer_port  = 0;
    hhds::Port_id consumer_port  = 0;
    uint32_t      consumer_input = 0;
    bool          transition     = false;
  };
  std::vector<size_t>                   version_color(nversions, Color_plan::invalid_index);
  std::vector<size_t>                   version_position(nversions, Color_plan::invalid_index);
  std::vector<std::vector<std::string>> abi_roles_by_version(nversions);
  std::vector<std::vector<Kernel_edge>> kernel_edges_by_color(plan.colors_.size());
  for (size_t color_index = 0; color_index < plan.colors_.size(); ++color_index) {
    const auto& color = plan.colors_[color_index];
    for (size_t position = 0; position < color.members.size(); ++position) {
      version_color[color.members[position]]    = color_index;
      version_position[color.members[position]] = position;
    }
  }
  for (const auto& slot : plan.boundary_slots_) {
    if (slot.producer_version != Color_plan::invalid_index) {
      abi_roles_by_version[slot.producer_version].push_back(std::format("write:{}:p{}:shift{}:b{}:u{}",
                                                                        boundary_kind_name(slot.kind),
                                                                        slot.producer_port,
                                                                        slot.producer_shift,
                                                                        slot.width,
                                                                        slot.unsign));
    }
    for (const auto& consumer : slot.consumers) {
      if (consumer.version_site != Color_plan::invalid_index) {
        abi_roles_by_version[consumer.version_site].push_back(std::format("read:{}:p{}:i{}:b{}:u{}",
                                                                          boundary_kind_name(slot.kind),
                                                                          consumer.port,
                                                                          consumer.input,
                                                                          slot.width,
                                                                          slot.unsign));
      }
    }
  }
  absl::flat_hash_set<std::pair<size_t, size_t>> intra_color_value_pairs;
  for (const auto& use : plan.value_uses_) {
    if (use.producer_version == Color_plan::invalid_index || use.consumer_version == Color_plan::invalid_index) {
      continue;
    }
    const size_t producer_color = version_color[use.producer_version];
    if (producer_color == Color_plan::invalid_index || producer_color != version_color[use.consumer_version]) {
      continue;
    }
    kernel_edges_by_color[producer_color].push_back(Kernel_edge{version_position[use.producer_version],
                                                                version_position[use.consumer_version],
                                                                use.producer_port,
                                                                use.consumer_port,
                                                                use.consumer_input,
                                                                false});
    intra_color_value_pairs.emplace(use.producer_version, use.consumer_version);
  }
  for (const auto& edge : plan.version_dependencies_) {
    const size_t producer_color = version_color[edge.producer];
    if (producer_color == Color_plan::invalid_index || producer_color != version_color[edge.consumer]
        || intra_color_value_pairs.contains({edge.producer, edge.consumer})) {
      continue;
    }
    kernel_edges_by_color[producer_color].push_back(
        Kernel_edge{version_position[edge.producer], version_position[edge.consumer], 0, 0, 0, true});
  }

  std::map<std::string, size_t>    serialization_to_kernel;
  absl::flat_hash_set<std::string> used_kernel_signatures;
  plan.canonical_members_.resize(plan.colors_.size());
  for (size_t color_index = 0; color_index < plan.colors_.size(); ++color_index) {
    const auto& color = plan.colors_[color_index];
    const bool  reusable_candidate
        = color.members.size() <= 512 && std::ranges::all_of(color.members, [&](size_t member) {
            const auto& version = plan.version_sites_[member];
            const auto& site    = plan.sites_[version.base_site];
            const auto  op      = gu::type_op_of(site.node);
            return version.role == Version_role::data && op != Ntype_op::Memory && op != Ntype_op::Clock_cell && op != Ntype_op::Sub
                   && site.node.path() == plan.sites_[plan.version_sites_[color.members.front()].base_site].node.path();
          });
    if (!reusable_candidate) {
      plan.canonical_members_[color_index] = color.members;
      std::ranges::sort(plan.canonical_members_[color_index], [&](size_t lhs, size_t rhs) {
        return std::tie(plan.version_sites_[lhs].execution_order, plan.version_sites_[lhs].structural_id)
               < std::tie(plan.version_sites_[rhs].execution_order, plan.version_sites_[rhs].structural_id);
      });
      std::string signature = stable_id("unique:" + color.structural_id);
      while (!used_kernel_signatures.insert(signature).second) {
        signature = stable_id("collision:" + signature + ":" + color.structural_id);
      }
      plan.kernel_classes_.push_back(Color_plan::Kernel_class{std::move(signature), color_index, {color_index}});
      continue;
    }
    std::vector<std::string> seeds(color.members.size());
    for (size_t i = 0; i < color.members.size(); ++i) {
      const auto& version  = plan.version_sites_[color.members[i]];
      const auto& base     = plan.sites_[version.base_site];
      seeds[i]             = std::format("{}|kind:{}|version:{}|role:{}|output-port:{}|slot:{}",
                                         kernel_node_shape(base.node.base_node()),
                                         site_kind_name(base.kind),
                                         state_version_name(version.version),
                                         version_role_name(version.role),
                                         version.output_port,
                                         execution_slot_name(version.slot));
      seeds[i]            += version.control_owner == Color_plan::invalid_index ? "|control:unconditional" : "|control:conditional";
      auto abi_roles       = abi_roles_by_version[color.members[i]];
      std::ranges::sort(abi_roles);
      for (const auto& role : abi_roles) {
        seeds[i] += "|abi:" + role;
      }
    }

    const auto& kernel_edges = kernel_edges_by_color[color_index];

    std::vector<std::string> descriptions = seeds;
    for (size_t round = 0; round < color.members.size() + 1; ++round) {
      std::vector<std::string> ordered = descriptions;
      std::ranges::sort(ordered);
      ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
      std::vector<size_t> classes;
      classes.reserve(descriptions.size());
      for (const auto& description : descriptions) {
        classes.push_back(static_cast<size_t>(std::ranges::lower_bound(ordered, description) - ordered.begin()));
      }
      std::vector<std::string> next = seeds;
      for (const auto& edge : kernel_edges) {
        next[edge.producer] += std::format("|out:{}:p{}>c{}:p{}:i{}",
                                           edge.transition ? "transition" : "value",
                                           edge.producer_port,
                                           classes[edge.consumer],
                                           edge.consumer_port,
                                           edge.consumer_input);
        next[edge.consumer] += std::format("|in:{}:p{}:i{}<c{}:p{}",
                                           edge.transition ? "transition" : "value",
                                           edge.consumer_port,
                                           edge.consumer_input,
                                           classes[edge.producer],
                                           edge.producer_port);
      }
      if (next == descriptions) {
        break;
      }
      descriptions = std::move(next);
    }
    bool unique_ranks = true;
    {
      auto ordered = descriptions;
      std::ranges::sort(ordered);
      unique_ranks = std::adjacent_find(ordered.begin(), ordered.end()) == ordered.end();
    }
    std::string serialization = std::string(execution_slot_name(color.slot));
    if (!unique_ranks && color.members.size() > 1) {
      // UNIQUE BY CONSTRUCTION, not by digest. This string keys
      // `serialization_to_kernel`, and key equality is what merges two colors
      // onto ONE emitted C++ body -- so a digest collision here emitted the
      // wrong kernel. Everything else in `serialization` is exact text; this
      // substring was the whole 64-bit exposure. The color index cannot collide,
      // is cheaper, and an ambiguous color never reuses anyway.
      serialization += "|ambiguous-no-reuse:c" + std::to_string(color_index);
    }
    std::vector<size_t> canonical_order(color.members.size());
    std::iota(canonical_order.begin(), canonical_order.end(), 0);
    std::ranges::sort(canonical_order,
                      [&](size_t a, size_t b) { return std::tie(descriptions[a], a) < std::tie(descriptions[b], b); });
    std::vector<size_t> canonical_rank(color.members.size());
    for (size_t rank = 0; rank < canonical_order.size(); ++rank) {
      canonical_rank[canonical_order[rank]] = rank;
      plan.canonical_members_[color_index].push_back(color.members[canonical_order[rank]]);
      serialization += std::format("|n{}:{}", rank, descriptions[canonical_order[rank]]);
    }
    std::vector<std::string> serialized_edges;
    for (const auto& edge : kernel_edges) {
      serialized_edges.push_back(std::format("{}:{}:p{}>{}:p{}:i{}",
                                             edge.transition ? "transition" : "value",
                                             canonical_rank[edge.producer],
                                             edge.producer_port,
                                             canonical_rank[edge.consumer],
                                             edge.consumer_port,
                                             edge.consumer_input));
    }
    std::ranges::sort(serialized_edges);
    for (const auto& edge : serialized_edges) {
      serialization += "|e:" + edge;
    }

    if (const auto it = serialization_to_kernel.find(serialization); it != serialization_to_kernel.end()) {
      plan.kernel_classes_[it->second].colors.push_back(color_index);
      continue;
    }
    std::string signature = stable_id(serialization);
    if (!used_kernel_signatures.insert(signature).second) {
      // A genuine 128-bit hash collision receives a distinct deterministic ID;
      // it is never admitted into the existing exact-serialization class.
      signature = stable_id("collision:" + signature + ":" + serialization);
      while (!used_kernel_signatures.insert(signature).second) {
        signature = stable_id("collision:" + signature + ":" + serialization);
      }
    }
    const size_t kernel_index = plan.kernel_classes_.size();
    plan.kernel_classes_.push_back(Color_plan::Kernel_class{std::move(signature), color_index, {color_index}});
    serialization_to_kernel.emplace(std::move(serialization), kernel_index);
  }
  std::ranges::sort(plan.kernel_classes_,
                    [](const Color_plan::Kernel_class& a, const Color_plan::Kernel_class& b) { return a.signature < b.signature; });
  plan.summary_.kernel_classes = plan.kernel_classes_.size();
  plan.summary_.kernel_reuses  = plan.colors_.size() - plan.kernel_classes_.size();
  plan.summary_.colors         = plan.colors_.size();
  plan.summary_.color_merges   = plan.summary_.fine_colors - plan.summary_.colors;

  return plan;
}

// sim.tune support tables (sim_profile.md §6.1 / §8.1); the header says what
// they mean, this is how they are built.
//
// OCCURRENCES. An executable site (live, outside every compact loop's native
// body, with at least one version) belongs to the body that EXECUTES it. A
// data/state site's own path names that body. A Sub site -- a compact loop or
// an opaque call -- carries the CALLEE path, which ends in its own step, so it
// belongs one step up, to its caller. Occurrences are keyed by their step
// sequence: an Occurrence_path cannot be rebuilt from a prefix of steps, and
// the formatted path is not injective (an anonymous wrapper is transparent).
//
// SUPPORT. The single-period fan-in frontier of every version, over the exact
// value-flow record (value_uses_) plus the state-transition precedence edges:
//   * a source site's own reads (flop/latch state-read, memory read port,
//     loop/opaque call output) set the source's bucket -- the frontier stops at
//     stored state;
//   * a value use ORs its producer's bits, or sets a root input's bucket;
//   * a precedence edge out of a STATE UPDATE ORs the update's bits: a
//     post-rise/post-fall read of a state is that period's committed next value,
//     and a same-edge latch hands its reader the staged value, so both depend on
//     the update cone, not only on the stored bits.
// Every one of those edges points forward in execution order, so one ascending
// pass is exact. A site ORs its versions (its state update included: the
// next-state cone is work the site does every period); classes hash-cons EVERY
// executable site by (bits, occurrence) and carry four weights (ge, sites,
// cost, cost_flat; see Support_class). A zero-GE wiring site (Get_mask,
// Set_mask, Concat) still joins: it is emitted code, so its cost is >= 1.
//
// DETERMINISM. Occurrences sort by (path, definition, steps); sources by
// (site_path_rank, structural_id, index), root inputs last by port id; classes
// by (occurrence, bits); the class cap keeps the heaviest by cost, canonical
// order breaking ties. No order comes from an absl container or from the
// discovery order of the sites.
void Color_plan::build_tune_tables(hhds::Graph* root, const std::vector<std::vector<size_t>>& versions_by_base,
                                   const std::vector<size_t>& site_path_rank, const std::vector<bool>& compact_body) {
  occurrences_.clear();
  support_               = Support{};
  const size_t nsites    = sites_.size();
  const size_t nversions = version_sites_.size();
  const auto   io        = root->get_io();
  auto*        library   = io ? io->get_library() : nullptr;

  constexpr uint64_t kMax64     = std::numeric_limits<uint64_t>::max();
  constexpr uint32_t kNoIndex32 = std::numeric_limits<uint32_t>::max();
  const auto         sat_add    = [](uint64_t a, uint64_t b) { return a > kMax64 - b ? kMax64 : a + b; };
  const auto         sat_mul    = [](uint64_t a, uint64_t b) { return a != 0 && b > kMax64 / a ? kMax64 : a * b; };
  const auto         executable = [&](size_t s) { return sites_[s].live && !compact_body[s] && !versions_by_base[s].empty(); };

  // ---- weights: a compact loop does its native body's work once per lane.
  absl::flat_hash_map<const hhds::Graph*, uint64_t> body_ge_memo;
  absl::flat_hash_set<const hhds::Graph*>           body_ge_active;
  std::function<uint64_t(hhds::Graph*)>             body_ge = [&](hhds::Graph* graph) -> uint64_t {
    if (const auto it = body_ge_memo.find(graph); it != body_ge_memo.end()) {
      return it->second;
    }
    if (!body_ge_active.insert(graph).second) {
      return 0;  // recursive instantiation: discovery already failed the plan
    }
    uint64_t ge = 0;
    for (const auto node : graph->body().nodes()) {
      uint64_t weight = gu::mappable_ge_weight(node);
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        if (const auto child = node.get_subnode_graph()) {
          const auto loop = node.subnode_loop();
          weight          = std::max<uint64_t>(1, sat_mul(body_ge(child.get()), loop ? std::max<uint64_t>(1, loop->count) : 1));
        }
      }
      ge = sat_add(ge, weight);
    }
    body_ge_active.erase(graph);
    body_ge_memo.emplace(graph, ge);
    return ge;
  };
  std::vector<uint64_t> weight(nsites, 0);
  for (size_t s = 0; s < nsites; ++s) {
    const auto& site = sites_[s];
    if (!executable(s)) {
      continue;
    }
    if (site.kind != Site_kind::loop_control) {
      weight[s] = site.gate_equivalents;
      continue;
    }
    const auto child = site.node.get_subnode_graph();
    const auto loop  = site.node.subnode_loop();
    const auto lanes = loop ? std::max<uint64_t>(1, loop->count) : 1;
    weight[s]        = std::max<uint64_t>(1, sat_mul(child ? body_ge(child.get()) : 0, lanes));
  }

  // ---- occurrences
  using Steps = std::vector<Occurrence_step_key>;
  std::map<Steps, uint32_t> occurrence_by_steps;
  std::vector<const Steps*> occurrence_steps;
  const auto                add_occurrence = [&](Steps steps, const hhds::Graph* graph) {
    const auto [it, inserted] = occurrence_by_steps.try_emplace(std::move(steps), static_cast<uint32_t>(occurrences_.size()));
    if (inserted) {
      Occurrence occurrence;
      occurrence.def = graph == nullptr ? std::string{} : std::string(graph->get_name());
      occurrences_.push_back(std::move(occurrence));
      occurrence_steps.push_back(&it->first);
    }
    return it->second;
  };
  add_occurrence({}, root);  // the root is always an occurrence, even with no own site
  const auto steps_key = [](std::span<const hhds::Occurrence_step> steps, size_t count) {
    Steps key;
    key.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      key.push_back(occurrence_step_key(steps[i]));
    }
    return key;
  };
  absl::flat_hash_map<hhds::Occurrence_path, uint32_t> occurrence_of_body_path;
  std::vector<uint32_t>                                site_occurrence(nsites, kNoIndex32);
  for (size_t s = 0; s < nsites; ++s) {
    if (!executable(s)) {
      continue;
    }
    const auto& site  = sites_[s];
    const auto  steps = site.node.path().steps();
    uint32_t    occurrence;
    if (!steps.empty() && steps.back().subnode == site.node.get_definition_index()) {
      occurrence = add_occurrence(steps_key(steps, steps.size() - 1), site.node.get_graph());
    } else if (const auto it = occurrence_of_body_path.find(site.node.path()); it != occurrence_of_body_path.end()) {
      occurrence = it->second;
    } else {
      occurrence = add_occurrence(steps_key(steps, steps.size()), site.node.get_graph());
      occurrence_of_body_path.emplace(site.node.path(), occurrence);
    }
    site_occurrence[s]  = occurrence;
    auto& owner         = occurrences_[occurrence];
    owner.ge            = sat_add(owner.ge, weight[s]);
    owner.sites        += 1;
    owner.state_sites  += site.kind == Site_kind::state;
  }

  // ---- simulation word costs (Support_class::cost / cost_flat). A compact
  // loop runs its native body once per lane (nested loops multiply); an opaque
  // call runs its body -- unless that body was descended (some occurrence at or
  // below the callee path owns executable sites), whose work its own sites
  // already carry: charging it again would count it twice.
  absl::flat_hash_map<const hhds::Graph*, uint64_t> body_cost_memo;
  absl::flat_hash_set<const hhds::Graph*>           body_cost_active;
  std::function<uint64_t(hhds::Graph*)>             body_cost = [&](hhds::Graph* graph) -> uint64_t {
    if (const auto it = body_cost_memo.find(graph); it != body_cost_memo.end()) {
      return it->second;
    }
    if (!body_cost_active.insert(graph).second) {
      return 0;  // recursive instantiation: discovery already failed the plan
    }
    uint64_t cost = 0;
    for (const auto node : graph->body().nodes()) {
      if (node.is_invalid() || gu::is_builtin_node(node)) {
        continue;
      }
      uint64_t node_cost = 0;
      if (const auto child = gu::type_op_of(node) == Ntype_op::Sub ? node.get_subnode_graph() : nullptr) {
        const auto loop = node.subnode_loop();
        node_cost       = std::max<uint64_t>(1, sat_mul(body_cost(child.get()), loop ? std::max<uint64_t>(1, loop->count) : 1));
      } else {
        node_cost = sim_word_cost(node);
      }
      cost = sat_add(cost, node_cost);
    }
    body_cost_active.erase(graph);
    body_cost_memo.emplace(graph, cost);
    return cost;
  };
  std::set<Steps> descended;  // every prefix of an occurrence's steps
  for (const Steps* steps : occurrence_steps) {
    for (size_t length = 1; length <= steps->size(); ++length) {
      descended.emplace(steps->begin(), steps->begin() + static_cast<std::ptrdiff_t>(length));
    }
  }
  std::vector<uint64_t> cost(nsites, 0);
  std::vector<uint64_t> cost_flat(nsites, 0);
  for (size_t s = 0; s < nsites; ++s) {
    if (!executable(s)) {
      continue;
    }
    const auto& site = sites_[s];
    cost_flat[s]     = sim_word_cost(site.node.base_node());
    cost[s]          = cost_flat[s];
    if (site.kind == Site_kind::loop_control) {
      if (const auto child = site.node.get_subnode_graph()) {
        const auto loop = site.node.subnode_loop();
        cost[s]         = std::max<uint64_t>(1, sat_mul(body_cost(child.get()), loop ? std::max<uint64_t>(1, loop->count) : 1));
      }
    } else if (site.kind == Site_kind::instance || site.kind == Site_kind::conditional_control) {
      const auto child = site.node.get_subnode_graph();
      if (child && !descended.contains(steps_key(site.node.path().steps(), site.node.path().steps().size()))) {
        cost[s] = std::max<uint64_t>(1, body_cost(child.get()));
      }
    }
    auto& owner     = occurrences_[site_occurrence[s]];
    owner.cost      = sat_add(owner.cost, cost[s]);
    owner.cost_flat = sat_add(owner.cost_flat, cost_flat[s]);
  }
  // Display paths. An occurrence one step above a Sub site is that caller's
  // own body, whose instance -- another Sub site -- carries exactly its steps,
  // so every non-root occurrence finds a site path to format.
  if (library != nullptr) {
    std::vector<bool>                          named(occurrences_.size(), false);
    absl::flat_hash_set<hhds::Occurrence_path> seen;
    named[0] = true;  // the root: ""
    for (const auto& site : sites_) {
      if (!seen.insert(site.node.path()).second) {
        continue;
      }
      const auto steps = site.node.path().steps();
      const auto it    = occurrence_by_steps.find(steps_key(steps, steps.size()));
      if (it != occurrence_by_steps.end() && !named[it->second]) {
        occurrences_[it->second].path = hhds::format_occurrence_path(*library, site.node.path());
        named[it->second]             = true;
      }
    }
  }
  // Canonical order, fixed BEFORE anything below records an index.
  {
    std::vector<uint32_t> order(occurrences_.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, [&](uint32_t lhs, uint32_t rhs) {
      return std::tie(occurrences_[lhs].path, occurrences_[lhs].def, *occurrence_steps[lhs])
             < std::tie(occurrences_[rhs].path, occurrences_[rhs].def, *occurrence_steps[rhs]);
    });
    std::vector<uint32_t>   renumber(occurrences_.size());
    std::vector<Occurrence> sorted;
    sorted.reserve(occurrences_.size());
    for (size_t rank = 0; rank < order.size(); ++rank) {
      renumber[order[rank]] = static_cast<uint32_t>(rank);
      sorted.push_back(std::move(occurrences_[order[rank]]));
    }
    occurrences_ = std::move(sorted);
    for (auto& occurrence : site_occurrence) {
      if (occurrence != kNoIndex32) {
        occurrence = renumber[occurrence];
      }
    }
  }
  const auto no_occurrence = static_cast<uint32_t>(occurrences_.size());  // a top input / a multi-occurrence class

  // ---- sources
  std::vector<size_t> source_sites;
  for (size_t s = 0; s < nsites; ++s) {
    if (executable(s) && sites_[s].kind != Site_kind::data) {
      source_sites.push_back(s);
    }
  }
  std::ranges::sort(source_sites, [&](size_t lhs, size_t rhs) {
    return std::tie(site_path_rank[lhs], sites_[lhs].structural_id, lhs)
           < std::tie(site_path_rank[rhs], sites_[rhs].structural_id, rhs);
  });
  std::vector<uint32_t> input_ports;
  if (io != nullptr) {
    for (const auto& decl : io->get_input_pin_decls()) {
      input_ports.push_back(static_cast<uint32_t>(decl.port_id));
    }
  }
  std::ranges::sort(input_ports);
  input_ports.erase(std::unique(input_ports.begin(), input_ports.end()), input_ports.end());
  const size_t nsources = source_sites.size() + input_ports.size();

  // Everything below needs a total execution order (the plan's reverse-Kahn
  // rank, a permutation when the version DAG is acyclic).
  bool ordered = summary_.version_dag_acyclic && nversions < kNoIndex32 && value_uses_.size() < kNoIndex32 && nsources < kNoIndex32;
  std::vector<uint32_t> order;
  if (ordered) {
    order.assign(nversions, kNoIndex32);
    for (size_t v = 0; v < nversions && ordered; ++v) {
      const auto rank = version_sites_[v].execution_order;
      ordered         = rank < nversions && order[rank] == kNoIndex32;
      if (ordered) {
        order[rank] = static_cast<uint32_t>(v);
      }
    }
  }
  if (!ordered) {
    return;  // support_.available stays false; occurrences are still valid
  }

  // ---- mask width and buckets. Contiguous bucketing keeps one occurrence's
  // sources on neighbouring bits; an aliased bucket only over-reports activity
  // (conservative: fewer trials, never a wrong verdict).
  uint32_t words = static_cast<uint32_t>(std::clamp<size_t>((nsources + 63) / 64, 1, kMaxSupportWords));
  while (words > 1 && static_cast<uint64_t>(nversions) * words * sizeof(uint64_t) > kMaxSupportBitmapBytes) {
    words /= 2;
  }
  const uint64_t buckets = uint64_t{64} * words;
  const bool     exact   = nsources <= buckets;
  const auto     bucket
      = [&](size_t rank) { return static_cast<uint32_t>(exact ? rank : static_cast<uint64_t>(rank) * buckets / nsources); };
  support_.words = words;
  support_.exact = exact;
  support_.sources.reserve(nsources);
  std::vector<uint32_t> site_bucket(nsites, kNoIndex32);
  for (const size_t s : source_sites) {
    Support_source source;
    source.kind       = sites_[s].kind == Site_kind::state ? Support_source::Kind::state : Support_source::Kind::opaque;
    source.site       = s;
    source.bucket     = bucket(support_.sources.size());
    source.occurrence = site_occurrence[s];
    site_bucket[s]    = source.bucket;
    support_.sources.push_back(source);
    ++occurrences_[source.occurrence].sources;
  }
  absl::flat_hash_map<uint32_t, uint32_t> port_bucket;
  for (const uint32_t port : input_ports) {
    Support_source source;
    source.kind       = Support_source::Kind::top_input;
    source.port       = port;
    source.bucket     = bucket(support_.sources.size());
    source.occurrence = no_occurrence;
    port_bucket.emplace(port, source.bucket);
    support_.sources.push_back(source);
  }

  // ---- per-version frontier, one ascending pass (see the comment above)
  std::vector<uint32_t> use_begin(nversions + 1, 0);
  for (const auto& use : value_uses_) {
    ++use_begin[use.consumer_version + 1];
  }
  for (size_t v = 0; v < nversions; ++v) {
    use_begin[v + 1] += use_begin[v];
  }
  std::vector<uint32_t> use_list(value_uses_.size());
  {
    std::vector<uint32_t> cursor(use_begin.begin(), use_begin.end() - 1);
    for (size_t u = 0; u < value_uses_.size(); ++u) {
      use_list[cursor[value_uses_[u].consumer_version]++] = static_cast<uint32_t>(u);
    }
  }
  std::vector<std::vector<uint32_t>> update_fanin(nversions);
  for (const auto& edge : version_dependencies_) {
    if (version_sites_[edge.producer].role == Version_role::state_update) {
      update_fanin[edge.consumer].push_back(static_cast<uint32_t>(edge.producer));
    }
  }
  std::vector<uint64_t> bits(nversions * words, 0);
  const auto            or_row = [&](uint64_t* row, size_t from) {
    const uint64_t* source = bits.data() + from * words;
    for (uint32_t w = 0; w < words; ++w) {
      row[w] |= source[w];
    }
  };
  const auto set_bit = [](uint64_t* row, uint32_t bit) { row[bit / 64] |= uint64_t{1} << (bit % 64); };
  for (const uint32_t v : order) {
    uint64_t*   row     = bits.data() + static_cast<size_t>(v) * words;
    const auto& version = version_sites_[v];
    if (version.role != Version_role::state_update && site_bucket[version.base_site] != kNoIndex32) {
      set_bit(row, site_bucket[version.base_site]);
    }
    for (uint32_t i = use_begin[v]; i < use_begin[v + 1]; ++i) {
      const auto& use = value_uses_[use_list[i]];
      if (!use.literal.empty()) {
        continue;  // a fixed lane: no runtime source
      }
      if (use.top_input) {
        if (const auto it = port_bucket.find(static_cast<uint32_t>(use.producer_port)); it != port_bucket.end()) {
          set_bit(row, it->second);
        }
      } else if (use.producer_version < nversions) {
        or_row(row, use.producer_version);
      }
    }
    for (const uint32_t producer : update_fanin[v]) {
      or_row(row, producer);
    }
  }

  // ---- classes: hash-cons (site bits, occurrence)
  std::vector<uint64_t>                                class_bits;
  std::vector<Support_class>                           classes;
  absl::flat_hash_map<uint64_t, std::vector<uint32_t>> class_by_hash;
  std::vector<uint64_t>                                scratch(words);
  for (size_t s = 0; s < nsites; ++s) {
    if (!executable(s)) {
      continue;
    }
    std::ranges::fill(scratch, 0);
    for (const size_t v : versions_by_base[s]) {
      or_row(scratch.data(), v);
    }
    const uint32_t occurrence = site_occurrence[s];
    uint64_t       hash       = livehd::hash_util::kFnv1a64_offset ^ occurrence;
    for (const uint64_t word : scratch) {
      hash  = (hash ^ word) * 0x9e3779b97f4a7c15ULL;
      hash ^= hash >> 29;
    }
    auto&    same_hash = class_by_hash[hash];
    uint32_t found     = kNoIndex32;
    for (const uint32_t candidate : same_hash) {
      if (classes[candidate].occurrence == occurrence
          && std::equal(scratch.begin(), scratch.end(), class_bits.begin() + static_cast<std::ptrdiff_t>(candidate) * words)) {
        found = candidate;
        break;
      }
    }
    if (found == kNoIndex32) {
      found = static_cast<uint32_t>(classes.size());
      same_hash.push_back(found);
      classes.push_back(Support_class{.occurrence = occurrence});
      class_bits.insert(class_bits.end(), scratch.begin(), scratch.end());
    }
    auto& cls      = classes[found];
    cls.ge         = sat_add(cls.ge, weight[s]);
    cls.sites     += 1;
    cls.cost       = sat_add(cls.cost, cost[s]);
    cls.cost_flat  = sat_add(cls.cost_flat, cost_flat[s]);
  }
  // The per-version bitmaps are the only big allocation: release them now.
  std::vector<uint64_t>().swap(bits);

  const auto add_weights = [&](Support_class& into, const Support_class& from) {
    into.ge         = sat_add(into.ge, from.ge);
    into.sites     += from.sites;
    into.cost       = sat_add(into.cost, from.cost);
    into.cost_flat  = sat_add(into.cost_flat, from.cost_flat);
    if (into.occurrence != from.occurrence) {
      into.occurrence = no_occurrence;  // spans several occurrences
    }
  };

  // ---- class cap, step 1 (EXACT): over the cap, re-hash-cons by the bit row
  // alone. Two classes with the same row are idle in exactly the same sampled
  // pairs, so summing their weights changes no idle_* column; only the
  // occurrence split goes (the merged class keeps the shared occurrence, else
  // the no_occurrence sentinel -- nothing reads class_occ). On minion the
  // (bits, occurrence) split held ~2x as many classes as distinct rows, and the
  // cap used to fold the surplus away (review round 2, driver-weights:FX2-R2-1).
  if (classes.size() > kMaxSupportClasses) {
    std::vector<Support_class>                           by_row;
    std::vector<uint64_t>                                by_row_bits;
    absl::flat_hash_map<uint64_t, std::vector<uint32_t>> row_by_hash;
    for (size_t c = 0; c < classes.size(); ++c) {
      const auto row  = std::span<const uint64_t>(class_bits.data() + c * words, words);
      uint64_t   hash = livehd::hash_util::kFnv1a64_offset;
      for (const uint64_t word : row) {
        hash  = (hash ^ word) * 0x9e3779b97f4a7c15ULL;
        hash ^= hash >> 29;
      }
      auto&    same_hash = row_by_hash[hash];
      uint32_t found     = kNoIndex32;
      for (const uint32_t candidate : same_hash) {
        if (std::equal(row.begin(), row.end(), by_row_bits.begin() + static_cast<std::ptrdiff_t>(candidate) * words)) {
          found = candidate;
          break;
        }
      }
      if (found == kNoIndex32) {
        same_hash.push_back(static_cast<uint32_t>(by_row.size()));
        by_row.push_back(classes[c]);
        by_row_bits.insert(by_row_bits.end(), row.begin(), row.end());
        continue;
      }
      add_weights(by_row[found], classes[c]);
    }
    classes.swap(by_row);
    class_bits.swap(by_row_bits);
  }

  // Canonical class order: (occurrence, bits).
  std::vector<uint32_t> class_order(classes.size());
  std::iota(class_order.begin(), class_order.end(), 0);
  const auto bits_of_class
      = [&](uint32_t c) { return std::span<const uint64_t>(class_bits.data() + static_cast<size_t>(c) * words, words); };
  std::ranges::sort(class_order, [&](uint32_t lhs, uint32_t rhs) {
    if (classes[lhs].occurrence != classes[rhs].occurrence) {
      return classes[lhs].occurrence < classes[rhs].occurrence;
    }
    return std::ranges::lexicographical_compare(bits_of_class(lhs), bits_of_class(rhs));
  });

  // ---- class cap, step 2 (CONSERVATIVE): still over the cap, the heaviest
  // kMaxSupportClasses - kSupportFoldGroups classes stay -- ranked by
  // cost_flat, the weight the tuner's ladder reads (lhd cm1::kSupportWeight),
  // then ge, then cost, then canonical order -- and the rest, in canonical
  // order, fold into kSupportFoldGroups CONTIGUOUS groups whose row is the OR
  // of their members' rows. A folded group is idle only when all its members
  // are, so the idle columns can only under-report; neighbouring (occurrence,
  // bits) classes keep each union tight, where the old single all-ones class
  // was idle only when nothing changed at all and capped I_s well below the
  // ladder's thresholds (minion verilog: 36% of cost_flat, never idle).
  std::vector<bool> kept(classes.size(), true);
  if (classes.size() > kMaxSupportClasses) {
    std::vector<uint32_t> by_weight(class_order);  // canonical order breaks weight ties
    std::ranges::stable_sort(by_weight, [&](uint32_t lhs, uint32_t rhs) {
      const auto& l = classes[lhs];
      const auto& r = classes[rhs];
      return std::tie(l.cost_flat, l.ge, l.cost) > std::tie(r.cost_flat, r.ge, r.cost);
    });
    for (size_t i = kMaxSupportClasses - kSupportFoldGroups; i < by_weight.size(); ++i) {
      kept[by_weight[i]] = false;
    }
  }
  std::vector<uint32_t> overflow;
  for (const uint32_t c : class_order) {
    if (!kept[c]) {
      overflow.push_back(c);
      continue;
    }
    support_.classes.push_back(classes[c]);
    const auto row = bits_of_class(c);
    support_.class_bits.insert(support_.class_bits.end(), row.begin(), row.end());
  }
  if (!overflow.empty()) {
    const size_t groups = std::min(kSupportFoldGroups, overflow.size());
    for (size_t group = 0; group < groups; ++group) {
      const size_t          begin = group * overflow.size() / groups;
      const size_t          end   = (group + 1) * overflow.size() / groups;
      Support_class         folded{.occurrence = classes[overflow[begin]].occurrence};
      std::vector<uint64_t> row(words, 0);
      for (size_t i = begin; i < end; ++i) {
        add_weights(folded, classes[overflow[i]]);
        const auto member_row = bits_of_class(overflow[i]);
        for (uint32_t w = 0; w < words; ++w) {
          row[w] |= member_row[w];
        }
      }
      support_.classes.push_back(folded);
      support_.class_bits.insert(support_.class_bits.end(), row.begin(), row.end());
      support_.fold_classes   += 1;
      support_.fold_members   += static_cast<uint32_t>(end - begin);
      support_.fold_ge         = sat_add(support_.fold_ge, folded.ge);
      support_.fold_sites      = sat_add(support_.fold_sites, folded.sites);
      support_.fold_cost       = sat_add(support_.fold_cost, folded.cost);
      support_.fold_cost_flat  = sat_add(support_.fold_cost_flat, folded.cost_flat);
    }
  }
  for (const auto& cls : support_.classes) {
    support_.total_ge        = sat_add(support_.total_ge, cls.ge);
    support_.total_sites     = sat_add(support_.total_sites, cls.sites);
    support_.total_cost      = sat_add(support_.total_cost, cls.cost);
    support_.total_cost_flat = sat_add(support_.total_cost_flat, cls.cost_flat);
  }
  support_.available = true;
}

const std::vector<size_t>& Color_plan::colors_in_execution_order() const {
  if (colors_in_execution_order_.size() != colors_.size()) {
    colors_in_execution_order_.assign(colors_.size(), 0);
    for (size_t color = 0; color < colors_.size(); ++color) {
      const auto rank = colors_[color].execution_order;
      // A dense rank: place directly. A malformed (out-of-range or repeated)
      // rank cannot happen for a plan assign_color_order built, but fall back
      // to a sort rather than write out of bounds if one ever does.
      if (rank >= colors_.size()) {
        colors_in_execution_order_.clear();
        break;
      }
      colors_in_execution_order_[rank] = color;
    }
    if (colors_in_execution_order_.size() != colors_.size()) {
      colors_in_execution_order_.resize(colors_.size());
      std::iota(colors_in_execution_order_.begin(), colors_in_execution_order_.end(), 0);
      std::ranges::sort(colors_in_execution_order_, {}, [&](size_t color) { return colors_[color].execution_order; });
    }
  }
  return colors_in_execution_order_;
}

bool Color_plan::validate_retained_handles() const {
  // One in-edge per (sink pin, driver) pair: summing the PLURAL driver lists
  // over the sorted sink pins is the same count the in-edge range reported,
  // and it still counts both drivers of a compact loop's carry-in.
  const auto inp_edge_count = [](const auto& node) {
    uint64_t count = 0;
    for (const auto& sink : node.inp_sorted_pins()) {
      count += sink.get_driver_pins().size();
    }
    return count;
  };
  uint64_t edge_count = 0;
  for (const auto& node : outer_nodes_) {
    edge_count += inp_edge_count(node);
    edge_count += node.out_edges().size();
  }
  for (const auto& site : sites_) {
    edge_count += inp_edge_count(site.node);
    edge_count += site.node.out_edges().size();
  }
  return edge_count >= summary_.carry_edges_cut;
}

std::string Color_plan::report() const {
  std::string result;
  result += "sim-color-plan v2\n";
  result += "stage discovery+versioning+coarsening+direct-abi\n";
  result += std::format("complete {}\n", summary_.complete ? "true" : "false");
  result += std::format("versioning-complete {}\n", summary_.versioning_complete ? "true" : "false");
  result += std::format("version-dag-acyclic {}\n", summary_.version_dag_acyclic ? "true" : "false");
  result += std::format("color-dag-acyclic {}\n", summary_.color_dag_acyclic ? "true" : "false");
  result += std::format("boundary-one-writer {}\n", summary_.boundary_one_writer ? "true" : "false");
  result += std::format("boundary-dominance {}\n", summary_.boundary_dominance ? "true" : "false");
  result += std::format("runtime-random {}\n", summary_.runtime_random ? "true" : "false");
  // sim.tune support (name-free, like everything above the observation map).
  if (support_.available) {
    result += std::format(
        "support words={} sources={} exact={} classes={} total-ge={} total-sites={} total-cost={} total-cost-flat={} "
        "fold-classes={} fold-members={} fold-ge={} fold-sites={} fold-cost={} fold-cost-flat={}\n",
        support_.words,
        support_.sources.size(),
        support_.exact ? "true" : "false",
        support_.classes.size(),
        support_.total_ge,
        support_.total_sites,
        support_.total_cost,
        support_.total_cost_flat,
        support_.fold_classes,
        support_.fold_members,
        support_.fold_ge,
        support_.fold_sites,
        support_.fold_cost,
        support_.fold_cost_flat);
  } else {
    result += "support unavailable reason=version-dag-cyclic\n";
  }
  result += std::format(
      "counts grouped-sites={} outer-sites={} live-sites={} independent-sites={} compact-loops={} "
      "conditional-regions={} carry-edges-cut={} self-edges-dropped={} version-sites={} version-edges={} fine-colors={} colors={} "
      "color-merges={} value-uses={} boundary-slots={} boundary-bits={} kernel-classes={} kernel-reuses={}\n",
      summary_.grouped_sites,
      summary_.outer_sites,
      summary_.live_sites,
      summary_.physical_occurrence_sites,
      summary_.compact_loops,
      summary_.conditional_regions,
      summary_.carry_edges_cut,
      summary_.self_edges_dropped,
      summary_.version_sites,
      summary_.version_edges,
      summary_.fine_colors,
      summary_.colors,
      summary_.color_merges,
      summary_.value_uses,
      summary_.boundary_slots,
      summary_.boundary_bits,
      summary_.kernel_classes,
      summary_.kernel_reuses);
  uint64_t max_live_words       = 0;
  size_t   oversized_singletons = 0;
  for (const auto& color : colors_) {
    max_live_words        = std::max(max_live_words, color.peak_live_words);
    oversized_singletons += color.members.size() == 1 && color.peak_live_words > live_word_budget_;
  }
  result += std::format("register-budget words={} max-estimated-live-words={} oversized-singletons={}\n",
                        live_word_budget_,
                        max_live_words,
                        oversized_singletons);
  result += "identity structural-128; raw-index=false; user-name=false\n";
  result += "membership occurrence-node,state-version\n";
  result += "clock-level slot=pre-rise-eval reference=low\n";
  result += "clock-level slot=rise-commit reference=high\n";
  result += "clock-level slot=post-rise-eval reference=high\n";
  result += "clock-level slot=fall-commit reference=low\n";
  result += "clock-level slot=post-fall-publish reference=low\n";

  std::vector<std::string> errors = errors_;
  std::ranges::sort(errors);
  for (const auto& error : errors) {
    result += "error " + quote(error) + "\n";
  }

  const bool omit_exhaustive_detail = version_sites_.size() > 100'000;
  if (omit_exhaustive_detail) {
    result += "detail omitted reason=large-plan threshold=100000\n";
  } else {
    std::vector<std::string> sites;
    sites.reserve(sites_.size());
    for (const auto& site : sites_) {
      sites.push_back(std::format("site {} kind={} op={} depth={} live={} ge={}\n",
                                  site.structural_id,
                                  site_kind_name(site.kind),
                                  Ntype::get_name(gu::type_op_of(site.node)),
                                  site.node.path().steps().size(),
                                  site.live ? "true" : "false",
                                  site.gate_equivalents));
    }
    std::ranges::sort(sites);
    for (const auto& site : sites) {
      result += site;
    }

    std::vector<std::string> dependencies;
    dependencies.reserve(dependencies_.size());
    for (const auto& dep : dependencies_) {
      dependencies.push_back(std::format("edge {}:p{} -> {}:p{} kind={} cut={}\n",
                                         sites_[dep.producer].structural_id,
                                         dep.producer_port,
                                         sites_[dep.consumer].structural_id,
                                         dep.consumer_port,
                                         dependency_kind_name(dep.kind),
                                         dep.cut ? "true" : "false"));
    }
    std::ranges::sort(dependencies);
    for (const auto& dependency : dependencies) {
      result += dependency;
    }

    std::vector<std::string> version_sites;
    version_sites.reserve(version_sites_.size());
    std::vector<std::string_view> version_color(version_sites_.size());
    for (const auto& color : colors_) {
      for (const size_t member : color.members) {
        version_color[member] = color.structural_id;
      }
    }
    for (size_t site_index = 0; site_index < version_sites_.size(); ++site_index) {
      const auto& site = version_sites_[site_index];
      version_sites.push_back(
          std::format("version-site {} base={} control-owner={} output-port={} version={} role={} slot={} order={} color={}\n",
                      site.structural_id,
                      sites_[site.base_site].structural_id,
                      site.control_owner == Color_plan::invalid_index ? std::string_view{"unconditional"}
                                                                      : std::string_view{sites_[site.control_owner].structural_id},
                      site.output_port,
                      state_version_name(site.version),
                      version_role_name(site.role),
                      execution_slot_name(site.slot),
                      site.execution_order,
                      version_color[site_index]));
    }
    std::ranges::sort(version_sites);
    for (const auto& site : version_sites) {
      result += site;
    }

    std::vector<std::string> version_dependencies;
    version_dependencies.reserve(version_dependencies_.size());
    for (const auto& dependency : version_dependencies_) {
      version_dependencies.push_back(std::format("version-edge {} -> {} bits={}\n",
                                                 version_sites_[dependency.producer].structural_id,
                                                 version_sites_[dependency.consumer].structural_id,
                                                 dependency.boundary_bits));
    }
    std::ranges::sort(version_dependencies);
    for (const auto& dependency : version_dependencies) {
      result += dependency;
    }

    std::vector<std::string> colors;
    colors.reserve(colors_.size());
    for (const auto& color : colors_) {
      std::string line = std::format("color {} slot={} order={} ge={} members=",
                                     color.structural_id,
                                     execution_slot_name(color.slot),
                                     color.execution_order,
                                     color.gate_equivalents);
      for (size_t i = 0; i < color.members.size(); ++i) {
        line += (i == 0 ? "" : ",") + version_sites_[color.members[i]].structural_id;
      }
      colors.push_back(std::move(line) + "\n");
    }
    std::ranges::sort(colors);
    for (const auto& color : colors) {
      result += color;
    }

    for (const auto& kernel : kernel_classes_) {
      result += std::format("kernel-class {} representative={} colors=",
                            kernel.signature,
                            colors_[kernel.representative].structural_id);
      for (size_t i = 0; i < kernel.colors.size(); ++i) {
        result += (i == 0 ? "" : ",") + colors_[kernel.colors[i]].structural_id;
      }
      result += "\n";
    }

    std::vector<std::string> color_dependencies;
    color_dependencies.reserve(color_dependencies_.size());
    for (const auto& dependency : color_dependencies_) {
      color_dependencies.push_back(std::format("color-edge {} -> {} bits={}\n",
                                               colors_[dependency.producer].structural_id,
                                               colors_[dependency.consumer].structural_id,
                                               dependency.boundary_bits));
    }
    std::ranges::sort(color_dependencies);
    for (const auto& dependency : color_dependencies) {
      result += dependency;
    }

    for (const auto& slot : boundary_slots_) {
      const auto owner
          = slot.owner_site == invalid_index ? std::string_view("-") : std::string_view(sites_[slot.owner_site].structural_id);
      const auto producer_version  = slot.producer_version == invalid_index
                                         ? std::string_view("-")
                                         : std::string_view(version_sites_[slot.producer_version].structural_id);
      const auto producer_color    = slot.producer_color == invalid_index
                                         ? std::string_view("-")
                                         : std::string_view(colors_[slot.producer_color].structural_id);
      result                      += std::format(
          "boundary-slot {} kind={} version={} owner={} writer-version={} writer-color={} "
          "producer-port={} producer-shift={} producer-extract=[{},{}) public-port={} bits={} unsign={} consumers=",
          slot.structural_id,
          boundary_kind_name(slot.kind),
          state_version_name(slot.version),
          owner,
          producer_version,
          producer_color,
          slot.producer_port,
          slot.producer_shift,
          slot.producer_extract_lo,
          slot.producer_extract_hi,
          slot.public_port,
          slot.width,
          slot.unsign);
      for (size_t i = 0; i < slot.consumers.size(); ++i) {
        const auto& consumer  = slot.consumers[i];
        result               += std::format("{}{}:{}:p{}:i{}:b{}:preextracted={}",
                                            i == 0 ? "" : ",",
                                            colors_[consumer.color].structural_id,
                                            version_sites_[consumer.version_site].structural_id,
                                            consumer.port,
                                            consumer.input,
                                            consumer.width,
                                            consumer.preextracted ? "true" : "false");
      }
      result += "\n";
    }
  }

  result += "old-scheduler-carveout mixed-phase-child-stale\n";
  result += "old-scheduler-carveout refresh-negedge-uncovered\n";
  result += "old-scheduler-carveout nonreference-latch-clock-window\n";
  result += "old-scheduler-carveout transparent-high-event-semantics\n";

  result += "observation-map begin\n";
  std::vector<std::string> observations;
  observations.reserve(observations_.size());
  for (const auto& observation : observations_) {
    observations.push_back(std::format("observe {} port={} name={} site={}\n",
                                       observation.input ? "input" : "output",
                                       observation.port,
                                       quote(observation.name),
                                       observation.structural_id));
  }
  std::ranges::sort(observations);
  for (const auto& observation : observations) {
    result += observation;
  }
  result += "observation-map end\n";

  // Like the observation map, a names-allowed section: instance paths and
  // definition names identify the sim.tune occurrences for a person reading
  // the profile; nothing above depends on them.
  result += "occurrence-map begin\n";
  for (const auto& occurrence : occurrences_) {
    result += std::format("occurrence path={} def={} ge={} cost={} cost-flat={} sites={} state-sites={} sources={}\n",
                          quote(occurrence.path),
                          quote(occurrence.def),
                          occurrence.ge,
                          occurrence.cost,
                          occurrence.cost_flat,
                          occurrence.sites,
                          occurrence.state_sites,
                          occurrence.sources);
  }
  result += "occurrence-map end\n";
  return result;
}

void Color_plan::write_report(std::string_view path) const {
  File_output output(path);
  output.append(report());
}

}  // namespace livehd::sim
