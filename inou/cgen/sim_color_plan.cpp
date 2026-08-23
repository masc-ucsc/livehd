// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "sim_color_plan.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "file_output.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "port_reach.hpp"

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

bool is_true_constant(const hhds::Occurrence_pin& pin) {
  return !pin.is_invalid() && gu::is_const_pin(pin) && gu::hydrate_const(pin).is_known_true();
}

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
  for (const auto& edge : node.inp_edges()) {
    if (edge.sink.get_port_id() == *port) {
      guard = edge.driver;
      break;
    }
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
  for (const auto& edge : site.base_node().inp_edges()) {
    if (edge.sink.get_port_id() == *port) {
      guard = edge.driver;
      break;
    }
  }
  if (guard.is_invalid() || (gu::is_const_pin(guard) && gu::hydrate_const(guard).is_known_true())) {
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
    lo_ *= 1099511628211ULL;
    hi_ ^= static_cast<uint64_t>(byte) + 0x9e;
    hi_ *= 14029467366897019727ULL;
    hi_ ^= hi_ >> 29;
  }

  uint64_t lo_ = 1469598103934665603ULL;
  uint64_t hi_ = 7809847782465536322ULL;
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
  static uint64_t rotate_left(uint64_t value, unsigned shift) { return (value << shift) | (value >> (64 - shift)); }

  static uint64_t avalanche(uint64_t value) {
    value ^= value >> 27;
    value *= 0x3c79ac492ba7b653ULL;
    value ^= value >> 33;
    value *= 0x1c69b3f74ac4ae35ULL;
    value ^= value >> 27;
    return value;
  }

  uint64_t lo_ = 1469598103934665603ULL;
  uint64_t hi_ = 7809847782465536322ULL;
};

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
std::string node_shape(const hhds::Node_class& node) {
  Shape_hash_builder hash;
  hash.append_u64(1);
  hash.append_u64(static_cast<uint64_t>(gu::type_op_of(node)));

  using Input_shape = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, std::string>;
  std::vector<Input_shape> inputs;
  for (const auto& edge : node.inp_edges()) {
    uint64_t    source_kind = 2;
    uint64_t    source_op   = 0;
    std::string literal;
    if (gu::is_const_pin(edge.driver)) {
      source_kind = 0;
      literal     = gu::hydrate_const(edge.driver).to_string();
    } else if (gu::is_graph_input_pin(edge.driver)) {
      source_kind = 1;
    } else {
      source_op = static_cast<uint64_t>(gu::type_op_of(edge.driver.get_master_node()));
    }
    inputs.emplace_back(edge.sink.get_port_id(),
                        source_kind,
                        source_op,
                        edge.driver.get_port_id(),
                        static_cast<uint64_t>(gu::bits_of(edge.driver)),
                        gu::is_unsign(edge.driver),
                        std::move(literal));
  }
  std::ranges::sort(inputs);
  hash.append_u64(inputs.size());
  for (const auto& [sink, source_kind, source_op, source_port, bits, unsign, literal] : inputs) {
    hash.append_u64(sink);
    hash.append_u64(source_kind);
    hash.append_u64(source_op);
    hash.append_u64(source_port);
    hash.append_u64(bits);
    hash.append_u64(unsign);
    hash.append_text(literal);
  }

  std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> outputs;
  for (const auto& pin : node.out_pins()) {
    outputs.emplace_back(pin.get_port_id(), static_cast<uint64_t>(gu::bits_of(pin)), gu::is_unsign(pin));
  }
  std::ranges::sort(outputs);
  hash.append_u64(outputs.size());
  for (const auto& [port, bits, unsign] : outputs) {
    hash.append_u64(port);
    hash.append_u64(bits);
    hash.append_u64(unsign);
  }

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
    std::ranges::sort(carries);
    hash.append_u64(carries.size());
    for (const auto& [input, output] : carries) {
      hash.append_u64(input);
      hash.append_u64(output);
    }
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
    std::ranges::sort(ports);
    hash.append_u64(ports.size());
    for (const auto& [direction, port, bits, loop_break] : ports) {
      hash.append_u64(direction);
      hash.append_u64(port);
      hash.append_u64(bits);
      hash.append_u64(loop_break);
    }
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
std::string kernel_node_shape(const hhds::Node_class& node) {
  std::string              result = std::format("op:{}", Ntype::get_name(gu::type_op_of(node)));
  std::vector<std::string> inputs;
  for (const auto& edge : node.inp_edges()) {
    const std::string source = gu::is_const_pin(edge.driver)
                                   ? "const:" + gu::hydrate_const(edge.driver).to_string()
                                   : std::format("value:b{}:u{}", gu::bits_of(edge.driver), gu::is_unsign(edge.driver));
    inputs.push_back(std::format("{}<-{}", edge.sink.get_port_id(), source));
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
  uint64_t lo = 1469598103934665603ULL;
  uint64_t hi = 7809847782465536322ULL;
  for (const unsigned char c : text) {
    lo ^= c;
    lo *= 1099511628211ULL;
    hi ^= static_cast<uint64_t>(c) + 0x9e;
    hi *= 14029467366897019727ULL;
    hi ^= hi >> 29;
  }
  lo ^= text.size();
  hi ^= text.size() << 1;
  return {lo, hi};
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

bool is_loop_carry(const hhds::Occurrence_node& node, const hhds::Occurrence_edge& edge) {
  if (!node.is_loop_subnode() || edge.driver.get_master_node() != node || edge.sink.get_master_node() != node) {
    return false;
  }
  for (const auto& carry : node.subnode_group().carries()) {
    if (carry.output_port() == edge.driver.get_port_id() && carry.input_port() == edge.sink.get_port_id()) {
      return true;
    }
  }
  return false;
}

bool is_loop_carry_input(const hhds::Occurrence_node& node, const hhds::Occurrence_edge& edge) {
  if (!node.is_loop_subnode()) {
    return false;
  }
  return std::ranges::any_of(node.subnode_group().carries(),
                             [&](const auto& carry) { return carry.input_port() == edge.sink.get_port_id(); });
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
    neighbors.reserve(sites[i].node.inp_edges().size() + sites[i].node.out_edges().size());
    for (const auto& edge : sites[i].node.inp_edges()) {
      if (is_loop_carry(sites[i].node, edge)) {
        continue;
      }
      Neighbor   neighbor;
      auto&      fields = neighbor.fields;
      const auto it     = occurrence_index.find(edge.driver.get_master_node().get_occurrence_index());
      fields[0]         = 0;
      fields[1]         = edge.sink.get_port_id();
      if (it != occurrence_index.end()) {
        neighbor.site = it->second;
        fields[4]     = edge.driver.get_port_id();
      } else {
        const auto pin = edge.driver.base_pin();
        fields[2]      = 1;
        fields[4]      = pin.is_invalid() ? 0 : pin.get_port_id();
        fields[5]      = 3;
        if (!pin.is_invalid() && gu::is_const_pin(pin)) {
          fields[5]        = 0;
          const auto value = stable_hash128(gu::hydrate_const(pin).to_string());
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
  for (size_t round = 0; round < std::min<size_t>(sites.size() + 1, 32); ++round) {
    for (size_t i = 0; i < sites.size(); ++i) {
      auto& neighbors = adjacency[i];
      for (auto& neighbor : neighbors) {
        if (neighbor.site != Color_plan::invalid_index) {
          neighbor.fields[3] = classes[neighbor.site];
        }
      }
      std::ranges::sort(neighbors, [](const Neighbor& a, const Neighbor& b) { return a.fields < b.fields; });
      Refinement_hash_builder hash;
      hash.append_u64(seeds[i][0]);
      hash.append_u64(seeds[i][1]);
      hash.append_u64(neighbors.size());
      for (const auto& neighbor : neighbors) {
        for (const uint64_t field : neighbor.fields) {
          hash.append_u64(field);
        }
      }
      descriptions[i] = hash.finish();
    }
    auto next = classes_of(descriptions);
    if (next == classes) {
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
    const auto clock = lc::sink_driver_hier(node, "clock_pin");
    if (clock.is_invalid()) {
      return std::nullopt;  // combinational arrays are data colors, not state actions
    }
    const auto posclk = lc::sink_driver_hier(node, "posclk");
    if (!posclk.is_invalid() && gu::is_const_pin(posclk) && gu::hydrate_const(posclk).is_known_false()) {
      // Today's emitter is rise-only for memories. Keep that behavior visible
      // as an incomplete edge-class decision instead of silently treating a
      // secondary/falling memory exactly like a flop.
      versioning_complete = false;
    }
    return true;
  }
  if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
    const auto posclk   = lc::sink_driver_hier(node, "posclk");
    const bool positive = !(!posclk.is_invalid() && gu::is_const_pin(posclk) && gu::hydrate_const(posclk).is_known_false());
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
  if (enable.is_invalid() || (gu::is_const_pin(enable) && gu::hydrate_const(enable).is_known_true())) {
    // An always-open latch is a combinational buffer, not a staged state
    // update. The future emitter must lower that role explicitly.
    versioning_complete = false;
    return std::nullopt;
  }
  if (gu::is_const_pin(enable) && gu::hydrate_const(enable).is_known_false()) {
    return std::nullopt;  // permanently held state
  }
  const auto root = lc::control_root(enable);
  if (!root.net.is_invalid() && clocks.is_clock(root.net)) {
    return root.inverted;  // !clk closes on rise; clk closes on fall
  }
  return true;  // data-gated latch: the established end-of-period update slot
}

}  // namespace

Color_plan Color_plan::discover(hhds::Graph* root, bool include_observations) {
  Color_plan plan;
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
        for (const auto& edge : discovery.lift(output).inp_edges()) {
          if (const auto site = index.find(edge.driver.get_master_node().get_occurrence_index()); site != index.end()) {
            mark_live(site->second);
            driver_id = plan.sites_[site->second].structural_id;
          }
        }
      } else {
        for (const auto& edge : output.inp_edges()) {
          if (const auto site = find_body_driver_site(body.graph, body.path, edge.driver.get_master_node())) {
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
      for (const auto& edge : plan.sites_[i].node.inp_edges()) {
        const auto drv = edge.driver.get_master_node();
        std::print(stderr, "[plan] loop site {} sink pid {} <- driver pid {} op {} self={} indexed={}\n", i, edge.sink.get_port_id(),
                   edge.driver.get_port_id(), drv.is_invalid() ? -1 : static_cast<int>(gu::type_op_of(drv)),
                   !drv.is_invalid() && drv == plan.sites_[i].node, !drv.is_invalid() && index.contains(drv.get_occurrence_index()));
      }
    }
    for (const auto& edge : plan.sites_[i].node.inp_edges()) {
      const auto driver = edge.driver.get_master_node();
      if (driver.is_invalid()) {
        continue;
      }
      // A carry input has TWO drivers: the compact node's own carry self-edge
      // (the eager chain inside the wrapper — not a parent-level dependency)
      // and the parent's SEED, which ordinal 0 consumes. Only the self-edge is
      // skipped; skipping every carry edge marked the seed cone dead, and a
      // runtime seed (a packed array built before the loop) then reached the
      // wrapper as `0 /*UNRESOLVED-CYCLE*/` (a constant seed hid it).
      if (plan.sites_[i].kind == Site_kind::loop_control && is_loop_carry_input(plan.sites_[i].node, edge)
          && driver == plan.sites_[i].node) {
        continue;
      }
      if (const auto it = index.find(driver.get_occurrence_index()); it != index.end()) {
        mark_live(it->second);
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
      std::print(stderr, "[plan] site {} kind {} op {} live {} id {}\n", i, static_cast<int>(st.kind),
                 static_cast<int>(gu::type_op_of(st.node)), st.live, st.structural_id.substr(0, 40));
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
      for (const auto& edge : plan.sites_[consumer].node.inp_edges()) {
        const auto name = Ntype::get_sink_name(Ntype_op::Memory, static_cast<int>(edge.sink.get_port_id()));
        if (name == "undef" && gu::is_const_pin(edge.driver) && !gu::hydrate_const(edge.driver).is_known_false()) {
          plan.summary_.runtime_random = true;
          break;
        }
      }
    }
    for (const auto& edge : plan.sites_[consumer].node.inp_edges()) {
      // Unknown literal bits are not runtime randomness in simulation. Under
      // sim.unknown_zero cgen_sim's sim_const_text() concretizes them to zero;
      // by default sim_const_expr() draws them from hlop's seeded PRNG ONCE,
      // behind a `static const`, so the literal is still a constant across
      // periods. Only operations that draw on EVERY period (currently
      // ordering="none" memory collisions above) require random scheduling.
      const auto producer_it = index.find(edge.driver.get_master_node().get_occurrence_index());
      if (producer_it == index.end() || !plan.sites_[producer_it->second].live) {
        continue;
      }
      Dependency dep;
      dep.producer      = producer_it->second;
      dep.consumer      = consumer;
      dep.producer_port = edge.driver.get_port_id();
      dep.consumer_port = edge.sink.get_port_id();
      if (is_loop_carry(plan.sites_[consumer].node, edge) && dep.producer == consumer) {
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
    // MEASURED on XiangShan `Backend` 2026-08-18: `self-edges-dropped=0`. No
    // design has produced one; the 307,612 blocked sites there come from a
    // 77-hop combinational ring, NOT from self edges (see the cycle extractor
    // below, and docs/opt_loop_incr.md). Do not read this guard as the fix for
    // that stall.
    if (producer == consumer) {
      if (plan.summary_.self_edges_dropped == 0) {  // one witness is enough; the count carries the rest
        plan.errors_.push_back(std::format("version site {} precedes ITSELF: an unsatisfiable ordering constraint, i.e. a "
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
  const auto find_body_site = [&](hhds::Graph* body, const hhds::Occurrence_path& body_path,
                                  const hhds::Node_class& driver) -> std::optional<size_t> {
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
  const auto                add_value_use = [&](const auto& edge, size_t consumer, uint32_t consumer_input, State_version version) {
    auto producer_it = index.find(edge.driver.get_master_node().get_occurrence_index());
    bool top_input   = producer_it == index.end() && gu::is_graph_input_pin(edge.driver);
    if (!top_input && (producer_it == index.end() || !plan.sites_[producer_it->second].live)) {
      return;
    }
    size_t        producer_base       = top_input ? Color_plan::invalid_index : producer_it->second;
    hhds::Port_id producer_port       = edge.driver.get_port_id();
    uint32_t      producer_shift      = 0;
    uint32_t      producer_width      = 0;
    uint32_t      producer_extract_lo = 0;
    uint32_t      producer_extract_hi = 0;
    bool          preextracted        = false;
    int           lo                  = -1;
    int           hi                  = -1;

    // A packed child output may be a word-level cycle while the exact slice
    // demanded by its parent is acyclic. Resolve constant Get_mask and the
    // common And(SRA(word,k),low-mask) idiom through the callee's proven
    // output-slice summary and bind the exact leaf value.
    const auto& consumer_base          = plan.sites_[plan.version_sites_[consumer].base_site];
    const auto  consumer_op            = gu::type_op_of(consumer_base.node);
    const bool  slice_debug            = std::getenv("LIVEHD_SIM_COLOR_DEBUG") != nullptr;
    uint32_t    output_boundary_width  = 0;
    bool        output_boundary_unsign = gu::is_unsign(edge.driver);
    if (consumer_op == Ntype_op::Get_mask && edge.sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
      for (const auto& input : consumer_base.node.inp_edges()) {
        if (input.sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Get_mask, "mask") && gu::is_const_pin(input.driver)) {
          std::tie(lo, hi) = gu::hydrate_const(input.driver).get_mask_range();
          break;
        }
      }
    }
    // Occurrence traversal also erases a child's GraphIO output node. Recover
    // that public carrier before fusing the child producer into its parent
    // consumer. The child expression itself can be wider (for example, a
    // packed Set_mask chain), but those internal bits are not visible across
    // the declared module boundary.
    const auto driver_node  = edge.driver.get_master_node();
    const auto driver_steps = driver_node.path().steps();
    if (driver_steps.size() > consumer_base.node.path().steps().size()) {
      const auto driver_graph = driver_node.get_graph();
      const auto driver_io    = driver_graph == nullptr ? nullptr : driver_graph->get_io();
      if (driver_io != nullptr) {
        for (const auto& decl : driver_io->get_output_pin_decls()) {
          const auto output  = driver_graph->get_output_pin(decl.name);
          bool       matches = false;
          for (const auto& output_edge : output.inp_edges()) {
            matches |= output_edge.driver.get_master_node().get_class_index()
                           == edge.driver.base_pin().get_master_node().get_class_index()
                       && output_edge.driver.get_port_id() == edge.driver.get_port_id();
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
      hhds::Occurrence_pin crossing          = edge.driver;
      if (consumer_op == Ntype_op::Get_mask && edge.sink.get_port_id() == 0) {
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
        if (lo == 0 && hi > lo && !gu::is_const_pin(crossing)) {
          const auto shift_node = crossing.get_master_node();
          if (shift_node.path() == consumer_base.node.path() && gu::type_op_of(shift_node) == Ntype_op::SRA) {
            hhds::Occurrence_pin value;
            hhds::Occurrence_pin amount;
            for (const auto& input : shift_node.inp_edges()) {
              if (input.sink.get_port_id() == 0) {
                value = input.driver;
              } else {
                amount = input.driver;
              }
            }
            if (!value.is_invalid() && !amount.is_invalid() && gu::is_const_pin(amount)) {
              const auto shift = gu::hydrate_const(amount);
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
      } else if (consumer_op == Ntype_op::And && !gu::is_const_pin(edge.driver)) {
        int mask_width = -1;
        for (const auto& input : consumer_base.node.inp_edges()) {
          if (!gu::is_const_pin(input.driver)) {
            continue;
          }
          const auto [mask_lo, mask_hi] = gu::hydrate_const(input.driver).get_mask_range();
          if (mask_lo == 0 && mask_hi > 0) {
            mask_width = mask_hi;
            break;
          }
        }
        for (int depth = 0; depth < 8 && !crossing.is_invalid() && !gu::is_const_pin(crossing); ++depth) {
          const auto           wrapper = crossing.get_master_node();
          const auto           op      = gu::type_op_of(wrapper);
          hhds::Occurrence_pin value;
          bool                 transparent = op == Ntype_op::Sext;
          if (op == Ntype_op::Sext || op == Ntype_op::Get_mask) {
            hhds::Occurrence_pin mask;
            for (const auto& input : wrapper.inp_edges()) {
              if (input.sink.get_port_id() == 0) {
                value = input.driver;
              } else {
                mask = input.driver;
              }
            }
            transparent |= mask.is_invalid();
            if (!mask.is_invalid() && gu::is_const_pin(mask)) {
              const auto constant  = gu::hydrate_const(mask);
              transparent         |= constant.is_just_i64() && constant.to_just_i64() == -1;
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
          for (const auto& input : shift_node.inp_edges()) {
            if (input.sink.get_port_id() == 0) {
              value = input.driver;
            } else {
              amount = input.driver;
            }
          }
          if (!value.is_invalid() && !amount.is_invalid() && gu::is_const_pin(amount)) {
            const auto shift = gu::hydrate_const(amount);
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
          for (const auto& input : crossing_node.inp_edges()) {
            if (input.sink.get_port_id() == 0) {
              value = input.driver;
            } else if (input.sink.get_port_id() == 2 && gu::is_const_pin(input.driver)) {
              mask = input.driver;
            }
          }
          bool transparent = false;
          if (!value.is_invalid() && !mask.is_invalid()) {
            const auto constant = gu::hydrate_const(mask);
            transparent         = constant.is_just_i64() && constant.to_just_i64() == -1;
            if (!transparent && gu::is_unsign(value)) {
              const auto [mask_lo, mask_hi] = constant.get_mask_range();
              const auto value_bits         = gu::bits_of(value);
              transparent                   = mask_lo == 0 && value_bits > 0 && mask_hi >= value_bits;
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
        for (int depth = 0; depth < 16 && !crossing.is_invalid() && !gu::is_const_pin(crossing); ++depth) {
          const auto packed    = crossing.get_master_node();
          const auto packed_op = gu::type_op_of(packed);
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
            // OCCURRENCE edge rather than lanes[hit].value: the occurrence
            // driver has already crossed whatever GraphIO boundary sits between
            // the two bodies, which is what makes a lane fed from the caller
            // resolvable at all. One value may drive several lanes, so the pid
            // -- never pin identity -- is the sound key.
            hhds::Occurrence_pin value;
            for (const auto& input : packed.inp_edges()) {
              if (input.sink.get_port_id() == static_cast<hhds::Port_id>(2 * hit)) {
                value = input.driver;
                break;
              }
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
          for (const auto& input : packed.inp_edges()) {
            switch (input.sink.get_port_id()) {
              case 0 : base = input.driver; break;
              case 2 : mask = input.driver; break;
              case 4 : value = input.driver; break;
              default: break;
            }
          }
          if (mask.is_invalid() || !gu::is_const_pin(mask)) {
            break;
          }
          const auto constant = gu::hydrate_const(mask);
          if (constant.has_unknowns() || constant.is_negative()) {
            break;
          }
          const auto [write_lo, write_hi] = constant.get_mask_range();
          if (write_lo < 0 || write_hi <= write_lo) {
            break;
          }
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
        // position_in_whole == false means the consumer's SRA was absorbed and
        // its operand must arrive LSB-aligned, so the walk is only bindable
        // when it landed exactly on the requested field's low bit -- the same
        // `slice.lo == lo` rule the child-output arm below applies, restated in
        // the coordinates this walk left behind. surface_shift then belongs to
        // the absorbed SRA, not to the operand.
        if (rebased && (position_in_whole || lo == 0) && !crossing.is_invalid() && !gu::is_const_pin(crossing)) {
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
      if (hi > lo && !crossing.is_invalid()) {
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
          const auto sub          = crossing_node.base_node();
          child                   = sub.get_subnode_graph();
          output_port             = crossing.get_port_id();
          producer_path           = crossing_node.path();
          have_child_path         = child != nullptr;
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
              for (const auto& output_edge : output.inp_edges()) {
                // HHDS can expose equivalent multi-driver pins through
                // different handle encodings (notably pid 0). Match the
                // semantic driver identity, not the raw pin class index.
                if (output_edge.driver.get_master_node().get_class_index()
                        == crossing.base_pin().get_master_node().get_class_index()
                    && output_edge.driver.get_port_id() == crossing.base_pin().get_port_id()) {
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
                    || gu::is_const_pin(slice.leaf)) {
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
    for (int depth = 0; !top_input && producer_base != Color_plan::invalid_index && depth < 64
                            && gu::type_op_of(plan.sites_[producer_base].node) == Ntype_op::Sub
                            && !plan.sites_[producer_base].node.is_loop_subnode();
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
        for (const auto& output_edge : output.inp_edges()) {
          if (gu::is_graph_input_pin(output_edge.driver)) {
            for (const auto& input_edge : sub_occurrence.inp_edges()) {
              if (input_edge.sink.get_port_id() != output_edge.driver.get_port_id()) {
                continue;
              }
              const auto bound = index.find(input_edge.driver.get_master_node().get_occurrence_index());
              top_input = bound == index.end() && gu::is_graph_input_pin(input_edge.driver);
              if (top_input || (bound != index.end() && plan.sites_[bound->second].live)) {
                producer_base = top_input ? Color_plan::invalid_index : bound->second;
                producer_port = input_edge.driver.get_port_id();
                resolved      = true;
              }
              break;
            }
          } else if (!gu::is_const_pin(output_edge.driver)) {
            const auto leaf = find_body_site(child.get(), sub_occurrence.path(), output_edge.driver.get_master_node());
            if (leaf && plan.sites_[*leaf].live) {
              producer_base = *leaf;
              producer_port = output_edge.driver.get_port_id();
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
    if (consumer_op == Ntype_op::Get_mask && edge.sink.get_port_id() == Ntype::get_sink_pid(Ntype_op::Get_mask, "a") && lo >= 0
        && hi > lo) {
      producer_extract_lo = static_cast<uint32_t>(lo);
      producer_extract_hi = static_cast<uint32_t>(hi);
      producer_shift      = 0;
      producer_width      = producer_extract_hi - producer_extract_lo;
      preextracted        = true;
    }

    const size_t producer = top_input ? Color_plan::invalid_index : producer_version(producer_base, version, producer_port);
    const auto   key      = std::tuple{producer,
                                       consumer,
                                       producer_port,
                                       producer_shift,
                                       producer_extract_lo,
                                       producer_extract_hi,
                                       edge.sink.get_port_id(),
                                       consumer_input,
                                       version,
                                       top_input};
    if (!exact_value_uses.insert(key).second) {
      return;
    }
    const uint32_t width
        = producer_width != 0 ? producer_width : static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(edge.driver)));
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
      for (const auto& base_edge : consumer_base.node.base_node().inp_edges()) {
        if (definition_input++ != consumer_input) {
          continue;
        }
        if (gu::is_graph_input_pin(base_edge.driver)) {
          consumer_width = static_cast<uint32_t>(std::max<int32_t>(1, gu::bits_of(base_edge.driver)));
        }
        break;
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
                  edge.sink.get_port_id(),
                  consumer_input,
                  width,
                  consumer_width,
                  preextracted ? true : (output_boundary_width != 0 ? output_boundary_unsign : gu::is_unsign(edge.driver)),
                  top_input,
                  preextracted});
    if (!top_input) {
      add_version_edge(producer, consumer, consumer_width);
    }
  };

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
    const Execution_slot commit_slot   = *rising ? Execution_slot::rise_commit : Execution_slot::fall_commit;
    const size_t         update        = ensure_version(base, input_version, Version_role::state_update, commit_slot, 0);
    state_updates[base]                = update;
    uint32_t consumer_input            = 0;
    for (const auto& edge : plan.sites_[base].node.inp_edges()) {
      if (is_timing_input(edge.sink)) {
        ++consumer_input;
        continue;
      }
      add_value_use(edge, update, consumer_input++, input_version);
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
    for (const auto& edge : plan.sites_[consumer].node.inp_edges()) {
      const auto producer_it = index.find(edge.driver.get_master_node().get_occurrence_index());
      if (producer_it == index.end()) {
        continue;
      }
      const bool timing_path = is_timing_input(edge.sink);
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
    // body. Its occurrence edges have already crossed the child's GraphIO, so
    // they are the authoritative binding even when that input is used only by
    // a pure input-to-output alias and no ordinary child site mentions it.
    for (const auto& site : plan.sites_) {
      if ((site.kind != Site_kind::instance && site.kind != Site_kind::conditional_control) || site.node.path() != body.path
          || site.node.get_subnode_gid() != body.graph->get_gid()) {
        continue;
      }
      for (const auto& edge : site.node.inp_edges()) {
        if (edge.sink.get_port_id() == input_port) {
          return edge.driver;
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
        if (gu::is_const_pin(driver)) {
          const auto constant = gu::hydrate_const(driver);
          const auto literal  = constant.to_pyrope();
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
            if (gu::is_const_pin(*resolved)) {
              const auto constant = gu::hydrate_const(*resolved);
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
        for (const auto& edge : discovery.lift(output).inp_edges()) {
          add_output_driver(edge.driver);
        }
      } else {
        for (const auto& edge : output.inp_edges()) {
          add_output_driver(edge.driver);
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
      for (const auto& edge : site.node.inp_edges()) {
        occurrence_drivers.try_emplace(edge.sink.base_pin().get_class_index(), edge.driver);
      }
      for (const auto& base_edge : site.node.base_node().inp_edges()) {
        if (!gu::is_graph_input_pin(base_edge.driver)) {
          continue;
        }
        if (const auto found = occurrence_drivers.find(base_edge.sink.get_class_index()); found != occurrence_drivers.end()) {
          resolved_inputs.try_emplace(base_edge.driver.get_port_id(), found->second);
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
      const bool constant    = gu::is_const_pin(*resolved);
      if (!top_input && !constant && (producer_it == index.end() || !plan.sites_[producer_it->second].live)) {
        plan.observations_.push_back(Observation{prefix + decl.name, "unbound", true, decl.port_id});
        continue;
      }
      const std::string literal = constant ? gu::hydrate_const(*resolved).to_pyrope() : std::string{};
      for (const auto version : {State_version::pre_rise, State_version::post_fall}) {
        const size_t producer = (top_input || constant)
                                    ? Color_plan::invalid_index
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
      for (const auto& edge : base_site.node.inp_edges()) {
        const int    raw  = static_cast<int>(edge.sink.get_port_id());
        const auto   name = Ntype::get_sink_name(Ntype_op::Memory, raw);
        const size_t port = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
        if (name == "type" && gu::is_const_pin(edge.driver)) {
          type = static_cast<int>(gu::hydrate_const(edge.driver).to_just_i64());
        } else if (name == "fwd") {
          fwd_matrix = edge.driver;
        } else if (name == "undef") {
          undef_matrix = edge.driver;
        } else if (name == "update") {
          whole = true;
        } else if (name == "update_enable" || name == "reset" || name == "init" || name == "bits" || name == "size"
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
          } else if (name.ends_with("rdport") && gu::is_const_pin(edge.driver)) {
            ports[port].rd = !gu::hydrate_const(edge.driver).is_known_false();
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
        if (matrix.is_invalid() || !gu::is_const_pin(matrix)) {
          return 0;
        }
        const auto value  = gu::hydrate_const(matrix);
        int        prefix = 0;
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
      uint32_t consumer_input = 0;
      for (const auto& edge : base_site.node.inp_edges()) {
        const int    raw  = static_cast<int>(edge.sink.get_port_id());
        const auto   name = Ntype::get_sink_name(Ntype_op::Memory, raw);
        const size_t port = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
        bool         used = false;
        if (whole && !registered && (consumer_site.output_port == Ntype::Memory_readall_pid || target != nullptr)) {
          used = name == "update" || name == "update_enable" || name == "reset" || name == "init";
        }
        if (target != nullptr && type != 1 && port < ports.size()) {
          const auto& shape       = ports[port];
          const bool  port_enable = name != "update_enable" && name.ends_with("enable");
          used                   |= (&shape == target && (name.ends_with("addr") || port_enable))
                                  || (!shape.rd && shape.wridx >= 0 && shape.wridx < prefix
                                      && (name.ends_with("addr") || port_enable || name.ends_with("din")));
        }
        if (used) {
          add_value_use(edge, consumer_version, consumer_input, consumer_site.version);
        }
        ++consumer_input;
      }
      continue;
    }
    uint32_t consumer_input = 0;
    for (const auto& edge : base_site.node.inp_edges()) {
      // Only the carry SELF-edge stays inside the native kernel; the parent's
      // seed of that carry is an ordinary value use (see the liveness walk).
      if (base_site.kind == Site_kind::loop_control && is_loop_carry_input(base_site.node, edge)
          && edge.driver.get_master_node() == base_site.node) {
        ++consumer_input;
        continue;
      }
      add_value_use(edge, consumer_version, consumer_input++, consumer_site.version);
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

  constexpr size_t kExactConvexVersionLimit = 50'000;

  // Validate and order the fine-color DAG. Slot order is a hard legality
  // constraint even when two colors have no explicit value edge; the later
  // Schedule construction uses one barrier/control boundary between slots
  // instead of materializing a quadratic all-to-all precedence relation.
  std::vector<uint32_t>                                 indegree(plan.version_sites_.size(), 0);
  std::vector<std::vector<std::pair<size_t, uint64_t>>> successors(plan.version_sites_.size());
  for (const auto& edge : plan.version_dependencies_) {
    ++indegree[edge.consumer];
    successors[edge.producer].emplace_back(edge.consumer, edge.boundary_bits);
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
    return std::tie(a.slot, site_path_rank[a.base_site], a.structural_id, lhs)
           > std::tie(b.slot, site_path_rank[b.base_site], b.structural_id, rhs);
  };
  std::priority_queue<size_t, std::vector<size_t>, decltype(later)> ready(later);
  for (size_t i = 0; i < indegree.size(); ++i) {
    if (indegree[i] == 0) {
      ready.push(i);
    }
  }
  // On plans too large for exact convex contraction, keep a newly-ready
  // consumer beside its producer when both belong to the same occurrence
  // body. Prefer the widest dependency when several become ready together.
  // This changes only the legal topological order used by the linear
  // coarsener; small plans retain their original deterministic order.
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
    plan.version_sites_[current].execution_order = order++;
    std::vector<std::pair<size_t, uint64_t>> newly_ready;
    for (const auto& [successor, bits] : successors[current]) {
      if (--indegree[successor] == 0) {
        newly_ready.emplace_back(successor, bits);
      }
    }
    for (const auto& [successor, bits] : newly_ready) {
      const auto& current_site   = plan.version_sites_[current];
      const auto& successor_site = plan.version_sites_[successor];
      const bool  same_surface   = plan.version_sites_.size() > kExactConvexVersionLimit && current_site.slot == successor_site.slot
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
    for (size_t i = 0; i < indegree.size(); ++i) {
      if (indegree[i] != 0) {
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
    // indegree>0 after Kahn stalls is on or downstream of a cycle, so an
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
      const auto   is_residual = [&indegree](const Color_plan::Version_dependency& dep) {
        return indegree[dep.consumer] != 0 && indegree[dep.producer] != 0;
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
      for (size_t seed = 0; seed < indegree.size() && back_edge.empty(); ++seed) {
        if (indegree[seed] == 0 || dfs_color[seed] != kWhite) {
          continue;
        }
        // (site, next-successor-index) frames, so the walk is heap-allocated
        // rather than a recursion over a 3 M-node graph.
        std::vector<std::pair<size_t, size_t>> stack{{seed, 0}};
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
        constexpr size_t kMaxHops = 64;
        const size_t     shown    = std::min(back_edge.size(), kMaxHops);
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
            via_of.try_emplace({plan.version_sites_[back_edge[i]].base_site,
                                plan.version_sites_[back_edge[(i + 1) % back_edge.size()]].base_site},
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
          hops += std::format("\n    {:>2}. {} op={} name={} graph={} path={} nid={} depth={} ge={}{}{}",
                              i,
                              vs.structural_id.substr(0, 10),
                              Ntype::get_name(gu::type_op_of(site.node)),
                              gu::has_name(site.node.base_node()) ? gu::node_name_of(site.node) : std::string_view{"-"},
                              graph_name_of(site.node),
                              library == nullptr ? std::string{} : hhds::format_occurrence_path(*library, site.node.path()),
                              static_cast<unsigned long long>(site.node.get_debug_nid() >> 2),
                              site.node.path().steps().size(),
                              site.gate_equivalents,
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
          const auto out_via = via_of.find(
              std::pair<size_t, size_t>{plan.version_sites_[idx].base_site, plan.version_sites_[next].base_site});
          const auto in_via = via_of.find(
              std::pair<size_t, size_t>{plan.version_sites_[prev].base_site, plan.version_sites_[idx].base_site});
          const auto output_port = plan.version_sites_[idx].output_port;
          const auto child       = site.node.base_node().get_subnode_graph();
          const auto& reach      = port_reach.of(child);
          const auto slices      = reach.out_slices.find(static_cast<uint32_t>(output_port));
          const auto inputs      = reach.out2ins.find(static_cast<uint32_t>(output_port));
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
                                 back_edge.size(), hist, boundaries.empty() ? "none" : boundaries, hops,
                                 back_edge.size() > shown ? "\n    ... (truncated)" : "");
      } else {
        cycle_desc = "; cycle=NONE FOUND (blocked sites are downstream of one, or the residual graph is malformed)";
      }
    }
    plan.errors_.push_back(
        std::format("fine-color dependency cycle remains after state and compact-loop carry cuts; blocked-count={} witnesses={}{}{}",
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

  // Greedy legality-constrained coarsening. Start with one fine color per
  // version site and contract connected same-slot DATA colors. The conservative
  // An edge A->B is contractible exactly when no alternate A...B path exists;
  // otherwise contraction would turn that path into a cycle. Check that
  // condition directly instead of the old degree-one sufficient rule, which
  // stranded nearly every branch/reconvergence cone as a separate runtime
  // task. State/control sites remain staged singleton colors.
  const size_t                                             nversions = plan.version_sites_.size();
  std::vector<size_t>                                      parent(nversions);
  std::vector<std::vector<size_t>>                         members(nversions);
  std::vector<uint64_t>                                    color_ge(nversions, 0);
  std::vector<bool>                                        mergeable(nversions, false);
  std::vector<absl::flat_hash_set<size_t>>                 predecessors(nversions), color_successors(nversions);
  absl::flat_hash_map<std::pair<size_t, size_t>, uint64_t> edge_bits;
  for (size_t i = 0; i < nversions; ++i) {
    parent[i] = i;
    members[i].push_back(i);
    color_ge[i]  = plan.sites_[plan.version_sites_[i].base_site].gate_equivalents;
    // State reads are ordinary old-Q values, and state-update sites only stage
    // pending values until the slot barrier commits them. Both are therefore
    // legal members of a same-slot acyclic color; keeping every endpoint as a
    // runtime singleton recreates per-register scheduler overhead.
    mergeable[i] = true;
  }
  for (const auto& edge : plan.version_dependencies_) {
    if (edge.producer == edge.consumer) {
      continue;
    }
    color_successors[edge.producer].insert(edge.consumer);
    predecessors[edge.consumer].insert(edge.producer);
    edge_bits[{edge.producer, edge.consumer}] += edge.boundary_bits;
  }
  const auto root_of = [&](size_t node) {
    while (parent[node] != node) {
      node = parent[node];
    }
    return node;
  };

  struct Merge_candidate {
    uint64_t score          = 0;
    size_t   producer_order = 0;
    size_t   consumer_order = 0;
    size_t   producer       = 0;
    size_t   consumer       = 0;
    uint64_t epoch          = 0;
  };
  const auto lower_priority = [](const Merge_candidate& a, const Merge_candidate& b) {
    if (a.score != b.score) {
      return a.score < b.score;
    }
    return std::tie(a.producer_order, a.consumer_order) > std::tie(b.producer_order, b.consumer_order);
  };
  std::priority_queue<Merge_candidate, std::vector<Merge_candidate>, decltype(lower_priority)> candidates(lower_priority);
  std::vector<size_t>                                                                          structural_order(nversions);
  std::iota(structural_order.begin(), structural_order.end(), 0);
  std::ranges::sort(structural_order, [&](size_t a, size_t b) {
    return plan.version_sites_[a].structural_id < plan.version_sites_[b].structural_id;
  });
  std::vector<size_t> structural_rank(nversions);
  for (size_t rank = 0; rank < structural_order.size(); ++rank) {
    structural_rank[structural_order[rank]] = rank;
  }
  absl::flat_hash_map<std::pair<size_t, size_t>, uint64_t>                            queued_epoch;
  uint64_t                                                                            next_epoch   = 1;
  constexpr uint64_t                                                                  kSoftColorGe = 50'000;
  absl::flat_hash_map<const hhds::Graph*, hhds::Occurrence_path>                      first_body_occurrence;
  absl::flat_hash_map<const hhds::Graph*, uint64_t>                                   definition_ge;
  absl::flat_hash_map<const hhds::Graph*, absl::flat_hash_set<hhds::Occurrence_path>> body_occurrences;
  absl::flat_hash_set<const hhds::Graph*>                                             reused_bodies;
  const bool         collect_hier_reuse       = nversions > kExactConvexVersionLimit && plan.summary_.version_dag_acyclic;
  constexpr uint64_t kHierReuseMinGe          = 2'048;
  constexpr uint64_t kHierReuseMaxGe          = 12'000;
  constexpr uint64_t kHierReuseMaxCutBits     = 128;
  constexpr size_t   kHierReuseMaxCutSlots    = 8;
  constexpr uint64_t kHierReuseMinScore       = 200;
  constexpr size_t   kHierReuseMinOccurrences = 4;
  constexpr size_t   kHierReuseMaxOccurrences = 128;
  struct Reuse_path_node {
    std::map<Occurrence_step_key, size_t> children;
    size_t                                parent     = Color_plan::invalid_index;
    size_t                                depth      = 0;
    uint64_t                              local_ge   = 0;
    uint64_t                              subtree_ge = 0;
  };
  std::vector<Reuse_path_node>                       reuse_path_trie(1);
  absl::flat_hash_map<hhds::Occurrence_path, size_t> reuse_path_node;
  for (const auto& site : plan.sites_) {
    const auto* definition = site.node.get_graph();
    body_occurrences[definition].insert(site.node.path());
    const auto [it, inserted] = first_body_occurrence.emplace(definition, site.node.path());
    if (inserted || it->second == site.node.path()) {
      definition_ge[definition] += site.gate_equivalents;
    }
    if (!inserted && it->second != site.node.path()) {
      reused_bodies.insert(definition);
    }
    if (collect_hier_reuse) {
      size_t trie_node = 0;
      for (const auto& step : site.node.path().steps()) {
        const auto key   = occurrence_step_key(step);
        const auto found = reuse_path_trie[trie_node].children.find(key);
        if (found == reuse_path_trie[trie_node].children.end()) {
          const size_t child = reuse_path_trie.size();
          reuse_path_trie[trie_node].children.emplace(key, child);
          reuse_path_trie.push_back(Reuse_path_node{{}, trie_node, reuse_path_trie[trie_node].depth + 1, 0, 0});
          trie_node = child;
        } else {
          trie_node = found->second;
        }
      }
      reuse_path_trie[trie_node].local_ge += site.gate_equivalents;
      reuse_path_node.try_emplace(site.node.path(), trie_node);
    }
  }
  if (collect_hier_reuse) {
    for (size_t node = reuse_path_trie.size(); node-- > 0;) {
      reuse_path_trie[node].subtree_ge += reuse_path_trie[node].local_ge;
      if (node == 0) {
        continue;
      }
      reuse_path_trie[reuse_path_trie[node].parent].subtree_ge += reuse_path_trie[node].subtree_ge;
    }
  }
  // Small repeated definitions remain canonical-kernel islands: sharing their
  // generated evaluator is more valuable than removing one task boundary, and
  // it keeps identical occurrences on the same ABI. Large repeated bodies are
  // allowed to fuse with their callers because their per-cycle boundary and
  // dispatch cost dominates the modest generated-code reuse.
  constexpr uint64_t                                   kReusableKernelMaxGe          = 1'024;
  constexpr uint64_t                                   kReusableKernelMinGe          = 128;
  constexpr size_t                                     kReusableKernelMaxOccurrences = 256;
  std::vector<std::pair<uint64_t, const hhds::Graph*>> reuse_candidates;
  for (const auto* definition : reused_bodies) {
    const auto occurrences = body_occurrences[definition].size();
    const auto ge          = definition_ge[definition];
    if (ge < kReusableKernelMinGe || ge > kReusableKernelMaxGe || occurrences > kReusableKernelMaxOccurrences) {
      continue;
    }
    const uint64_t duplicated_ge = ge * (occurrences - 1);
    if (duplicated_ge >= 2'048) {
      reuse_candidates.emplace_back(duplicated_ge, definition);
    }
  }
  std::ranges::sort(reuse_candidates, [](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return lhs.first > rhs.first;
    }
    const auto lhs_io   = lhs.second->get_io();
    const auto rhs_io   = rhs.second->get_io();
    const auto lhs_name = std::string(lhs_io ? lhs_io->get_name() : "");
    const auto rhs_name = std::string(rhs_io ? rhs_io->get_name() : "");
    if (lhs_name != rhs_name) {
      return lhs_name < rhs_name;
    }
    return lhs_io != nullptr && rhs_io != nullptr && lhs_io->get_gid() < rhs_io->get_gid();
  });
  struct Hier_reuse_candidate {
    uint64_t           score         = 0;
    uint64_t           subtree_ge    = 0;
    uint64_t           max_cut_bits  = 0;
    size_t             max_cut_slots = 0;
    size_t             occurrences   = 0;
    bool               uniform       = false;
    const hhds::Graph* definition    = nullptr;
  };
  std::vector<Hier_reuse_candidate> hier_reuse_candidates;
  if (collect_hier_reuse) {
    for (const auto* definition : reused_bodies) {
      const auto occurrence = first_body_occurrence.find(definition);
      if (occurrence == first_body_occurrence.end()) {
        continue;
      }
      const auto path_node = reuse_path_node.find(occurrence->second);
      if (path_node == reuse_path_node.end()) {
        continue;
      }
      const size_t occurrences    = body_occurrences[definition].size();
      uint64_t     subtree_ge     = reuse_path_trie[path_node->second].subtree_ge;
      uint64_t     max_subtree_ge = subtree_ge;
      for (const auto& path : body_occurrences[definition]) {
        const auto node = reuse_path_node.find(path);
        if (node == reuse_path_node.end()) {
          continue;
        }
        subtree_ge     = std::min(subtree_ge, reuse_path_trie[node->second].subtree_ge);
        max_subtree_ge = std::max(max_subtree_ge, reuse_path_trie[node->second].subtree_ge);
      }
      hier_reuse_candidates.push_back(
          Hier_reuse_candidate{0, subtree_ge, 0, 0, occurrences, subtree_ge == max_subtree_ge, definition});
    }
  }
  if (collect_hier_reuse) {
    std::vector<bool> candidate_cut_root(reuse_path_trie.size(), false);
    for (const auto& candidate : hier_reuse_candidates) {
      if (!candidate.uniform || candidate.subtree_ge < kHierReuseMinGe || candidate.subtree_ge > kHierReuseMaxGe
          || candidate.occurrences < kHierReuseMinOccurrences || candidate.occurrences > kHierReuseMaxOccurrences) {
        continue;
      }
      for (const auto& path : body_occurrences[candidate.definition]) {
        const auto node = reuse_path_node.find(path);
        if (node != reuse_path_node.end()) {
          candidate_cut_root[node->second] = true;
        }
      }
    }
    std::vector<size_t>                            cut_slots(reuse_path_trie.size(), 0);
    std::vector<uint64_t>                          cut_bits(reuse_path_trie.size(), 0);
    absl::flat_hash_set<std::pair<size_t, size_t>> cut_values;
    const auto                                     record_cut = [&](size_t path_node, size_t producer, uint64_t bits) {
      if (candidate_cut_root[path_node] && cut_values.insert({path_node, producer}).second) {
        ++cut_slots[path_node];
        cut_bits[path_node] += bits;
      }
    };
    for (const auto& edge : plan.version_dependencies_) {
      size_t producer_path = reuse_path_node.at(plan.sites_[plan.version_sites_[edge.producer].base_site].node.path());
      size_t consumer_path = reuse_path_node.at(plan.sites_[plan.version_sites_[edge.consumer].base_site].node.path());
      while (reuse_path_trie[producer_path].depth > reuse_path_trie[consumer_path].depth) {
        record_cut(producer_path, edge.producer, edge.boundary_bits);
        producer_path = reuse_path_trie[producer_path].parent;
      }
      while (reuse_path_trie[consumer_path].depth > reuse_path_trie[producer_path].depth) {
        record_cut(consumer_path, edge.producer, edge.boundary_bits);
        consumer_path = reuse_path_trie[consumer_path].parent;
      }
      while (producer_path != consumer_path) {
        record_cut(producer_path, edge.producer, edge.boundary_bits);
        record_cut(consumer_path, edge.producer, edge.boundary_bits);
        producer_path = reuse_path_trie[producer_path].parent;
        consumer_path = reuse_path_trie[consumer_path].parent;
      }
    }
    for (auto& candidate : hier_reuse_candidates) {
      if (!candidate.uniform || candidate.subtree_ge < kHierReuseMinGe || candidate.subtree_ge > kHierReuseMaxGe
          || candidate.occurrences < kHierReuseMinOccurrences || candidate.occurrences > kHierReuseMaxOccurrences) {
        continue;
      }
      for (const auto& path : body_occurrences[candidate.definition]) {
        const auto node = reuse_path_node.find(path);
        if (node == reuse_path_node.end()) {
          continue;
        }
        candidate.max_cut_slots = std::max(candidate.max_cut_slots, cut_slots[node->second]);
        candidate.max_cut_bits  = std::max(candidate.max_cut_bits, cut_bits[node->second]);
      }
      const uint64_t duplicated_ge = candidate.subtree_ge * (candidate.occurrences - 1);
      candidate.score              = duplicated_ge / std::max<uint64_t>(1, candidate.max_cut_bits);
    }
  }
  std::ranges::sort(hier_reuse_candidates, [](const auto& lhs, const auto& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }
    if (lhs.subtree_ge != rhs.subtree_ge) {
      return lhs.subtree_ge > rhs.subtree_ge;
    }
    const auto lhs_io   = lhs.definition->get_io();
    const auto rhs_io   = rhs.definition->get_io();
    const auto lhs_name = std::string(lhs_io ? lhs_io->get_name() : "");
    const auto rhs_name = std::string(rhs_io ? rhs_io->get_name() : "");
    if (lhs_name != rhs_name) {
      return lhs_name < rhs_name;
    }
    return lhs_io != nullptr && rhs_io != nullptr && lhs_io->get_gid() < rhs_io->get_gid();
  });
  absl::flat_hash_set<const hhds::Graph*> selected_reuse_bodies;
  if (!reuse_candidates.empty()) {
    selected_reuse_bodies.insert(reuse_candidates.front().second);
  }
  // A repeated human module with a compact, uniform hierarchical closure is a
  // useful coarsening boundary: its live value cut is usually much smaller than
  // an arbitrary graph cut, and preserving the closure gives cgen a stable unit
  // for generated-code reuse and incremental regeneration. Keep this much
  // stricter than local-body reuse; isolating large or lightly repeated modules
  // prevents profitable fusion in their callers. Each closure is an induced
  // subset of the already validated version DAG, so it is acyclic by construction.
  const hhds::Graph* selected_hier_reuse = nullptr;
  for (const auto& candidate : hier_reuse_candidates) {
    if (candidate.uniform && candidate.subtree_ge >= kHierReuseMinGe && candidate.subtree_ge <= kHierReuseMaxGe
        && candidate.max_cut_bits <= kHierReuseMaxCutBits && candidate.max_cut_slots <= kHierReuseMaxCutSlots
        && candidate.score >= kHierReuseMinScore && candidate.occurrences >= kHierReuseMinOccurrences
        && candidate.occurrences <= kHierReuseMaxOccurrences) {
      selected_hier_reuse = candidate.definition;
      break;
    }
  }
  std::vector<bool> hierarchy_root(reuse_path_trie.size(), false);
  if (selected_hier_reuse != nullptr) {
    for (const auto& path : body_occurrences[selected_hier_reuse]) {
      const auto node = reuse_path_node.find(path);
      if (node != reuse_path_node.end()) {
        hierarchy_root[node->second] = true;
      }
    }
  }
  std::vector<size_t> hierarchy_owner(reuse_path_trie.size(), Color_plan::invalid_index);
  std::vector<size_t> version_hierarchy_owner(nversions, Color_plan::invalid_index);
  if (selected_hier_reuse != nullptr) {
    for (size_t node = 1; node < reuse_path_trie.size(); ++node) {
      hierarchy_owner[node] = hierarchy_root[node] ? node : hierarchy_owner[reuse_path_trie[node].parent];
    }
    for (size_t version = 0; version < nversions; ++version) {
      const auto& path = plan.sites_[plan.version_sites_[version].base_site].node.path();
      const auto  node = reuse_path_node.find(path);
      if (node != reuse_path_node.end()) {
        version_hierarchy_owner[version] = hierarchy_owner[node->second];
      }
    }
  }
  const auto         body_reuse_worth_preserving = [&](size_t color_root) {
    return std::ranges::any_of(members[color_root], [&](size_t member) {
      const auto* definition = plan.sites_[plan.version_sites_[member].base_site].node.get_graph();
      return selected_reuse_bodies.contains(definition);
    });
  };
  const auto crosses_reuse_boundary = [&](size_t lhs, size_t rhs) {
    const auto& lhs_path  = plan.sites_[plan.version_sites_[members[lhs].front()].base_site].node.path();
    const auto& rhs_path  = plan.sites_[plan.version_sites_[members[rhs].front()].base_site].node.path();
    const auto  lhs_owner = version_hierarchy_owner[members[lhs].front()];
    const auto  rhs_owner = version_hierarchy_owner[members[rhs].front()];
    const bool  crosses_hierarchy
        = lhs_owner != rhs_owner && (lhs_owner != Color_plan::invalid_index || rhs_owner != Color_plan::invalid_index);
    return crosses_hierarchy || (lhs_path != rhs_path && (body_reuse_worth_preserving(lhs) || body_reuse_worth_preserving(rhs)));
  };
  const auto crosses_control_boundary = [&](size_t lhs, size_t rhs) {
    return plan.version_sites_[members[lhs].front()].control_owner != plan.version_sites_[members[rhs].front()].control_owner;
  };
  const auto enqueue = [&](size_t producer, size_t consumer) {
    producer = root_of(producer);
    consumer = root_of(consumer);
    if (producer == consumer || !color_successors[producer].contains(consumer) || !mergeable[producer] || !mergeable[consumer]
        || plan.version_sites_[members[producer].front()].slot != plan.version_sites_[members[consumer].front()].slot
        || crosses_control_boundary(producer, consumer) || crosses_reuse_boundary(producer, consumer)
        || color_ge[producer] > kSoftColorGe - std::min<uint64_t>(color_ge[consumer], kSoftColorGe)) {
      return;
    }
    const uint64_t score = 128 + edge_bits[{producer, consumer}];
    const auto     key   = std::pair{producer, consumer};
    if (queued_epoch.contains(key)) {
      return;
    }
    const uint64_t epoch = next_epoch++;
    queued_epoch[key]    = epoch;
    candidates.push(Merge_candidate{score,
                                    structural_rank[members[producer].front()],
                                    structural_rank[members[consumer].front()],
                                    producer,
                                    consumer,
                                    epoch});
  };
  const bool coarsen_supported = plan.summary_.version_dag_acyclic;
  if (coarsen_supported && nversions > kExactConvexVersionLimit) {
    // Exact repeated reachability is quadratic at whole-Minion scale. A global
    // topological interval is convex by construction: every vertex on a path
    // between two members has an execution rank inside the same interval.
    // Use a linear interval coarsener at this scale, but retain the same
    // selected repeated-definition boundaries as the exact path. Those islands
    // are what let cgen emit one verified canonical kernel and bind it to many
    // occurrences; crossing them here silently disables reuse on the designs
    // that need it most.
    std::vector<size_t> execution_order(nversions);
    std::iota(execution_order.begin(), execution_order.end(), 0);
    std::ranges::sort(execution_order, {}, [&](size_t version) { return plan.version_sites_[version].execution_order; });
    size_t current = Color_plan::invalid_index;
    for (const size_t version : execution_order) {
      if (!mergeable[version]) {
        current = Color_plan::invalid_index;
        continue;
      }
      bool can_join = current != Color_plan::invalid_index
                      && plan.version_sites_[members[current].front()].slot == plan.version_sites_[version].slot
                      && plan.version_sites_[members[current].front()].control_owner == plan.version_sites_[version].control_owner
                      && !crosses_reuse_boundary(current, version)
                      && color_ge[current] <= kSoftColorGe - std::min<uint64_t>(color_ge[version], kSoftColorGe);
      if (!can_join) {
        current = version;
        continue;
      }
      parent[version] = current;
      members[current].push_back(version);
      members[version].clear();
      color_ge[current]  += color_ge[version];
      color_ge[version]   = 0;
      mergeable[version]  = false;
    }
  }
  if (coarsen_supported && nversions <= kExactConvexVersionLimit) {
    for (const auto& edge : plan.version_dependencies_) {
      enqueue(edge.producer, edge.consumer);
    }
    while (!candidates.empty()) {
      const auto candidate = candidates.top();
      candidates.pop();
      const auto queued_it = queued_epoch.find({candidate.producer, candidate.consumer});
      if (queued_it == queued_epoch.end() || queued_it->second != candidate.epoch) {
        continue;
      }
      queued_epoch.erase(queued_it);
      size_t producer = root_of(candidate.producer);
      size_t consumer = root_of(candidate.consumer);
      if (producer == consumer || !color_successors[producer].contains(consumer)) {
        continue;
      }
      if (producer != candidate.producer || consumer != candidate.consumer) {
        enqueue(producer, consumer);
        continue;
      }
      std::vector<size_t>         merge_roots;
      uint64_t                    merged_ge = 0;
      // Contract the complete convex A...B region, not just A and B. If an
      // alternate path exists, merging only its endpoints would create a cycle;
      // swallowing every node that is both reachable from A and can reach B is
      // the exact legal contraction and preserves the topological quotient.
      absl::flat_hash_set<size_t> forward{producer};
      std::vector<size_t>         reach_pending{producer};
      while (!reach_pending.empty()) {
        const size_t current = reach_pending.back();
        reach_pending.pop_back();
        for (const size_t successor : color_successors[current]) {
          const size_t successor_root = root_of(successor);
          if (forward.insert(successor_root).second) {
            reach_pending.push_back(successor_root);
          }
        }
      }
      absl::flat_hash_set<size_t> reverse{consumer};
      reach_pending.assign(1, consumer);
      while (!reach_pending.empty()) {
        const size_t current = reach_pending.back();
        reach_pending.pop_back();
        for (const size_t predecessor : predecessors[current]) {
          const size_t predecessor_root = root_of(predecessor);
          if (reverse.insert(predecessor_root).second) {
            reach_pending.push_back(predecessor_root);
          }
        }
      }
      for (const size_t reachable : forward) {
        if (!reverse.contains(reachable)) {
          continue;
        }
        if (!mergeable[reachable]
            || plan.version_sites_[members[reachable].front()].slot != plan.version_sites_[members[producer].front()].slot
            || crosses_control_boundary(producer, reachable) || crosses_reuse_boundary(producer, reachable)
            || merged_ge > kSoftColorGe - std::min<uint64_t>(color_ge[reachable], kSoftColorGe)) {
          merge_roots.clear();
          break;
        }
        merged_ge += color_ge[reachable];
        merge_roots.push_back(reachable);
      }
      if (merge_roots.size() < 2) {
        continue;
      }
      std::ranges::sort(merge_roots);
      absl::flat_hash_set<size_t> merge_set(merge_roots.begin(), merge_roots.end());

      absl::flat_hash_set<size_t> new_predecessors;
      absl::flat_hash_set<size_t> new_successors;
      for (const size_t merged : merge_roots) {
        for (const size_t predecessor : predecessors[merged]) {
          const size_t predecessor_root = root_of(predecessor);
          if (!merge_set.contains(predecessor_root)) {
            new_predecessors.insert(predecessor_root);
          }
        }
        for (const size_t successor : color_successors[merged]) {
          const size_t successor_root = root_of(successor);
          if (!merge_set.contains(successor_root)) {
            new_successors.insert(successor_root);
          }
        }
      }
      bool local_cycle = false;
      for (const size_t p : new_predecessors) {
        local_cycle |= new_successors.contains(p);
      }
      if (local_cycle) {
        continue;
      }

      const size_t                          keep = merge_roots.front();
      absl::flat_hash_map<size_t, uint64_t> incoming_bits;
      absl::flat_hash_map<size_t, uint64_t> outgoing_bits;
      for (const size_t p : new_predecessors) {
        for (const size_t merged : merge_roots) {
          incoming_bits[p] += edge_bits[{p, merged}];
        }
      }
      for (const size_t s : new_successors) {
        for (const size_t merged : merge_roots) {
          outgoing_bits[s] += edge_bits[{merged, s}];
        }
      }
      for (const size_t p : new_predecessors) {
        for (const size_t merged : merge_roots) {
          color_successors[p].erase(merged);
        }
        color_successors[p].insert(keep);
      }
      for (const size_t s : new_successors) {
        for (const size_t merged : merge_roots) {
          predecessors[s].erase(merged);
        }
        predecessors[s].insert(keep);
      }
      for (const size_t merged : merge_roots) {
        if (merged == keep) {
          continue;
        }
        parent[merged] = keep;
        members[keep].insert(members[keep].end(), members[merged].begin(), members[merged].end());
        members[merged].clear();
        color_ge[merged]  = 0;
        mergeable[merged] = false;
        predecessors[merged].clear();
        color_successors[merged].clear();
      }
      color_ge[keep]         = merged_ge;
      mergeable[keep]        = true;
      predecessors[keep]     = std::move(new_predecessors);
      color_successors[keep] = std::move(new_successors);
      for (const auto& [p, bits] : incoming_bits) {
        edge_bits[{p, keep}] = bits;
      }
      for (const auto& [s, bits] : outgoing_bits) {
        edge_bits[{keep, s}] = bits;
      }
      for (const size_t p : predecessors[keep]) {
        queued_epoch.erase({p, keep});
        enqueue(p, keep);
      }
      for (const size_t s : color_successors[keep]) {
        queued_epoch.erase({keep, s});
        enqueue(keep, s);
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
      return plan.version_sites_[a].structural_id < plan.version_sites_[b].structural_id;
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
    pending_colors.push_back(std::move(pending_color));
  }
  std::ranges::sort(pending_colors, [](const Pending_color& a, const Pending_color& b) {
    return std::tie(a.color.slot, a.color.structural_id) < std::tie(b.color.slot, b.color.structural_id);
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
      for (size_t version = 0; version < plan.version_sites_.size(); ++version) {
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
    if (use.top_input) {
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
  std::ranges::sort(plan.boundary_slots_,
                    [](const Boundary_slot& a, const Boundary_slot& b) { return a.structural_id < b.structural_id; });
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
      serialization += "|ambiguous-no-reuse:" + color.structural_id;
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

bool Color_plan::validate_retained_handles() const {
  uint64_t edge_count = 0;
  for (const auto& node : outer_nodes_) {
    edge_count += node.inp_edges().size();
    edge_count += node.out_edges().size();
  }
  for (const auto& site : sites_) {
    edge_count += site.node.inp_edges().size();
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
  return result;
}

void Color_plan::write_report(std::string_view path) const {
  File_output output(path);
  output.append(report());
}

}  // namespace livehd::sim
