// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "artifact.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <type_traits>

namespace livehd::usyn {
namespace {
constexpr uint32_t magic   = 0x4e595355;  // USYN in little-endian order
constexpr uint32_t version = Frozen_region::version;
struct Refusal {
  Status      status;
  const char* reason;
};
[[noreturn]] void invalid(const char* reason) { throw Refusal{Status::invalid, reason}; }
[[noreturn]] void exhausted() { throw Refusal{Status::search_exhausted, "region artifact resource limit"}; }

struct Archive {
  Budget&                work;
  const Artifact_limits& limits;
  uint64_t               objects   = 0;
  uint64_t               allocated = 0;
  void                   allocate(uint64_t n) {
    if (n > limits.decoded_bytes - allocated) {
      exhausted();
    }
    allocated += n;
  }
  void spend(uint64_t n) {
    if (!work.spend(n)) {
      exhausted();
    }
  }
  void count(uint64_t n, uint64_t limit) {
    if (n > limit || n > limits.objects - objects) {
      exhausted();
    }
    spend(n);
    objects += n;
  }
};

struct Writer : Archive {
  static constexpr bool reading = false;
  std::string           bytes;
  void                  room(uint64_t n) {
    if (n > limits.bytes - bytes.size()) {
      exhausted();
    }
    spend(n);
  }
  template <class T>
  void scalar(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
      scalar(uint8_t(value));
    } else {
      static_assert(std::is_integral_v<T>);
      room(sizeof(T));
      using U         = std::make_unsigned_t<T>;
      const auto bits = std::bit_cast<U>(value);
      for (size_t i = 0; i < sizeof(T); ++i) {
        bytes.push_back(static_cast<char>(bits >> (i * 8)));
      }
    }
  }
  void string(std::string_view value) {
    if (value.size() > limits.string_bytes) {
      exhausted();
    }
    allocate(value.size());
    scalar(uint32_t(value.size()));
    room(value.size());
    bytes.append(value);
  }
  template <class T, class F>
  void sequence(const T& values, uint64_t limit, F transfer) {
    count(values.size(), limit);
    allocate(uint64_t(values.size()) * sizeof(typename T::value_type) * 2);
    scalar(uint32_t(values.size()));
    for (const auto& value : values) {
      transfer(value);
    }
  }
};

struct Reader : Archive {
  static constexpr bool reading = true;
  std::string_view      bytes;
  size_t                offset = 0;
  void                  room(uint64_t n) {
    if (n > bytes.size() - offset) {
      invalid("truncated region artifact");
    }
    spend(n);
  }
  template <class T>
  void scalar(T& value) {
    if constexpr (std::is_same_v<T, bool>) {
      uint8_t raw = 0;
      scalar(raw);
      if (raw > 1) {
        invalid("invalid artifact boolean");
      }
      value = raw;
    } else {
      static_assert(std::is_integral_v<T>);
      room(sizeof(T));
      using U = std::make_unsigned_t<T>;
      U bits  = 0;
      for (size_t i = 0; i < sizeof(T); ++i) {
        bits |= U(static_cast<uint8_t>(bytes[offset++])) << (i * 8);
      }
      value = std::bit_cast<T>(bits);
    }
  }
  void string(std::string& value) {
    uint32_t size = 0;
    scalar(size);
    if (size > limits.string_bytes) {
      exhausted();
    }
    room(size);
    allocate(size);
    value.assign(bytes.substr(offset, size));
    offset += size;
  }
  template <class T, class F>
  void sequence(std::vector<T>& values, uint64_t limit, F transfer) {
    uint32_t size = 0;
    scalar(size);
    count(size, limit);
    allocate(uint64_t(size) * sizeof(T) * 2);
    // Every serialized element contains at least one byte. Reject impossible
    // lengths before allocating even when the caller admits many objects.
    if (size > bytes.size() - offset) {
      invalid("truncated artifact vector");
    }
    values.resize(size);
    for (auto& value : values) {
      transfer(value);
    }
  }
};

template <class A, class... T>
void fields(A& a, T&... v) {
  (a.scalar(v), ...);
}
template <class A, class T>
void enumeration(A& a, T& value, uint32_t last) {
  uint32_t raw = static_cast<uint32_t>(value);
  a.scalar(raw);
  if (raw > last) {
    invalid("invalid artifact enum");
  }
  if constexpr (A::reading) {
    value = static_cast<T>(raw);
  }
}
template <class A, class T>
void optional(A& a, T& value) {
  bool present = value.has_value();
  a.scalar(present);
  if constexpr (A::reading) {
    if (present) {
      value.emplace();
    } else {
      value.reset();
    }
  }
  if (present) {
    a.scalar(*value);
  }
}
template <class A, class T>
void signal(A& a, T& s) {
  fields(a, s.id, s.inverted);
}
template <class A, class T>
void ref(A& a, T& r) {
  enumeration(a, r.space, 1);
  fields(a, r.index, r.inverted);
}
template <class A, class T>
void metrics(A& a, T& m) {
  fields(a, m.transistors, m.stack, m.branches, m.support, m.positive, m.negative);
}
template <class A, class T>
void formula(A& a, T& f) {
  a.scalar(f.output_inverted);
  a.sequence(f.nodes, a.limits.formula_nodes, [&](auto& n) {
    enumeration(a, n.kind, 3);
    fields(a, n.left, n.right, n.variable, n.inverted);
  });
}
template <class A, class T>
void truth(A& a, T& t) {
  a.scalar(t.inputs);
  if (t.inputs > max_logical_inputs) {
    invalid("invalid artifact function support");
  }
  a.sequence(t.words, std::max(uint64_t{1}, (uint64_t{1} << t.inputs) / 64), [&](auto& word) { a.scalar(word); });
  if (!valid_truth_table(t)) {
    invalid("invalid artifact truth table");
  }
}
template <class A, class T>
void graph(A& a, T& g) {
  uint32_t size = g.size();
  a.scalar(size);
  a.count(size, a.limits.nodes);
  a.allocate(uint64_t(size) * (sizeof(Xag::Node) + 96));
  if (!size) {
    invalid("artifact graph lacks constant node");
  }
  if constexpr (A::reading) {
    for (uint32_t i = 0; i < size; ++i) {
      Xag::Kind kind{};
      enumeration(a, kind, 3);
      Xsignal actual;
      if (kind == Xag::Kind::constant) {
        if (i != 0) {
          invalid("noncanonical artifact constant");
        }
      } else if (kind == Xag::Kind::source) {
        std::string name;
        a.string(name);
        actual = g.input(std::move(name));
      } else {
        Xsignal left, right;
        signal(a, left);
        signal(a, right);
        if (left.id >= i || right.id >= i) {
          invalid("artifact graph has a forward reference");
        }
        actual = kind == Xag::Kind::and_gate ? g.land(left, right) : g.lxor(left, right);
      }
      if (actual != Xsignal{i, false}) {
        invalid("noncanonical artifact graph node");
      }
    }
  } else {
    for (Id i = 0; i < size; ++i) {
      const auto& n = g.node(i);
      enumeration(a, n.kind, 3);
      if (n.kind == Xag::Kind::source) {
        a.string(g.input_names()[n.source_index]);
      }
      if (n.kind == Xag::Kind::and_gate || n.kind == Xag::Kind::xor_gate) {
        signal(a, n.inputs[0]);
        signal(a, n.inputs[1]);
      }
    }
  }
}
template <class A, class T>
void netlist(A& a, T& n) {
  a.scalar(n.policy_version);
  enumeration(a, n.residual_kind, 2);
  fields(a,
         n.clock_phases,
         n.gates.logical_inputs,
         n.gates.stack,
         n.gates.branches,
         n.costs.static_and,
         n.costs.static_xor,
         n.costs.static_not,
         n.costs.domino,
         n.costs.domino_latch,
         n.estimated_cost.static_logic,
         n.estimated_cost.inverters,
         n.estimated_cost.domino);
  graph(a, n.native);
  a.sequence(n.inputs, a.limits.nodes, [&](auto& s) { signal(a, s); });
  a.sequence(n.outputs, a.limits.nodes, [&](auto& o) {
    a.string(o.name);
    signal(a, o.signal);
  });
  a.sequence(n.state, a.limits.nodes, [&](auto& s) {
    a.string(s.name);
    a.scalar(s.init);
    signal(a, s.q);
    signal(a, s.reference_d);
    a.scalar(s.domain);
    optional(a, s.domino_latch);
  });
  a.sequence(n.cells, a.limits.nodes, [&](auto& c) {
    enumeration(a, c.kind, 2);
    fields(a, c.phase, c.state);
    a.string(c.name);
    for (auto& output : c.outputs) {
      a.string(output);
    }
    a.sequence(c.inputs, max_logical_inputs, [&](auto& r) { ref(a, r); });
    a.sequence(c.rails, 2 * max_logical_inputs, [&](auto& rail) {
      a.scalar(rail.input);
      ref(a, rail.producer);
    });
    truth(a, c.function);
    formula(a, c.formula);
    metrics(a, c.metrics);
  });
}
template <class A, class T>
void span(A& a, T& s) {
  optional(a, s.source_id);
  optional(a, s.file_id);
  a.string(s.file);
  optional(a, s.start_byte);
  optional(a, s.end_byte);
  optional(a, s.start_line);
  optional(a, s.start_col);
  optional(a, s.end_line);
  optional(a, s.end_col);
}
template <class A, class T>
void value(A& a, T& v) {
  std::string bytes;
  if constexpr (!A::reading) {
    if (v.size < 0 || uint64_t(v.size) * 16 + 3 > a.limits.string_bytes) {
      exhausted();
    }
    a.spend(uint64_t(v.size) * 16 + 3);
    a.allocate(uint64_t(v.size) * 16);
    bytes = v.serialize();
  }
  a.string(bytes);
  if constexpr (A::reading) {
    if (bytes.size() < 3) {
      invalid("invalid artifact constant");
    }
    const auto     type  = static_cast<uint8_t>(bytes[0]);
    const uint32_t words = uint32_t(static_cast<uint8_t>(bytes[1])) * 256 + static_cast<uint8_t>(bytes[2]);
    if ((type > 4 && type != 255) || words > 32767 || bytes.size() != 3 + uint64_t(words) * 16) {
      invalid("invalid artifact constant encoding");
    }
    for (uint32_t i = 0; i < words * 8; ++i) {
      if (static_cast<uint8_t>(bytes[3 + words * 8 + i]) & ~static_cast<uint8_t>(bytes[3 + i])) {
        invalid("invalid artifact unknown-bit plane");
      }
    }
    a.spend(bytes.size());
    a.allocate(uint64_t(words) * 16);
    v = Dlop::unserialize(bytes);
  }
}
template <class A, class T>
void source_signal(A& a, T& s) {
  fields(a, s.node, s.port, s.present, s.constant, s.bits);
  value(a, s.value);
  if (s.constant && (!s.present || !s.value.is_numeric() || s.value.size <= 0)) {
    invalid("invalid artifact constant signal");
  }
}
template <class A, class T>
void source(A& a, T& s) {
  a.string(s.source_graph);
  enumeration(a, s.scope, 2);
  a.scalar(s.logical_boundary);
  a.sequence(s.sources, a.limits.nodes, [&](auto& row) {
    a.scalar(row.node);
    a.string(row.name);
    span(a, row.span);
    enumeration(a, row.role, 5);
    fields(a, row.bits, row.stages, row.width_known);
    source_signal(a, row.q);
    source_signal(a, row.data);
    source_signal(a, row.clock);
    source_signal(a, row.clock_root);
    source_signal(a, row.enable);
    source_signal(a, row.reset);
    source_signal(a, row.initial);
    fields(a, row.neg_clock, row.clock_edge_known, row.neg_reset, row.async_reset, row.power_on, row.icg, row.translated_bits);
  });
  a.sequence(s.clocks, a.limits.nodes, [&](auto& c) {
    source_signal(a, c.output);
    source_signal(a, c.clock);
    source_signal(a, c.enable);
    fields(a, c.latch_node, c.parent);
    a.string(c.name);
    span(a, c.span);
  });
  a.sequence(s.bits, a.limits.nodes, [&](auto& b) {
    fields(a, b.source, b.stage, b.bit, b.latch);
    a.string(b.name);
    fields(a, b.q.node, b.q.inverted, b.d.node, b.d.inverted);
  });
  a.sequence(s.controls, a.limits.nodes, [&](auto& c) {
    a.scalar(c.source);
    enumeration(a, c.kind, 2);
    a.sequence(c.outputs, a.limits.nodes, [&](auto& o) { a.scalar(o); });
  });
}

void validate(std::string_view module, synth::State_target target, const Endpoint_netlist& n, const synth::Source_state_table& s,
              std::span<const uint32_t> bits, Budget& work, const Artifact_limits& limits) {
  if (module.empty() || !s.logical_boundary || bits.size() != n.state.size() || s.bits.size() != bits.size()) {
    invalid("invalid artifact semantic boundary");
  }
  auto expanded = expand_endpoint_netlist(n, work, limits.nodes, limits.formula_nodes);
  if (expanded.status != Status::feasible) {
    throw Refusal{expanded.status, "invalid or exhausted artifact netlist"};
  }
  auto exported = export_lnet(expanded, work, limits.nodes);
  if (!exported.net) {
    throw Refusal{exported.status, "invalid or exhausted artifact expansion"};
  }
  if (!synth::validate_state_controls(s, *exported.net) || !synth::validate_state_target(s, target)) {
    invalid("invalid artifact controls or target");
  }
  std::vector<bool>                                        seen(s.bits.size());
  std::vector<uint64_t>                                    counts(s.sources.size());
  std::set<uint64_t>                                       nodes;
  std::set<std::tuple<uint32_t, uint32_t, uint32_t>>       identities;
  std::map<std::tuple<uint64_t, uint32_t, bool>, uint32_t> domains;
  for (const auto& row : s.sources) {
    if (!work.spend()) {
      exhausted();
    }
    if (!nodes.insert(row.node).second || row.icg < -1 || (row.icg >= 0 && uint32_t(row.icg) >= s.clocks.size())) {
      invalid("invalid artifact source identity or clock gate");
    }
  }
  for (size_t i = 0; i < s.clocks.size(); ++i) {
    const auto parent = s.clocks[i].parent;
    if (parent < -1 || (parent >= 0 && static_cast<size_t>(parent) >= i)) {
      invalid("invalid artifact clock-gate parent");
    }
  }
  for (size_t i = 0; i < bits.size(); ++i) {
    if (!work.spend()) {
      exhausted();
    }
    if (bits[i] >= s.bits.size() || seen[bits[i]]) {
      invalid("invalid artifact state-bit correspondence");
    }
    seen[bits[i]] = true;
    const auto& b = s.bits[bits[i]];
    if (b.source >= s.sources.size() || b.latch != i || b.name.empty() || b.q.inverted
        || !identities.emplace(b.source, b.stage, b.bit).second) {
      invalid("invalid artifact source bit identity");
    }
    const auto& row = s.sources[b.source];
    if (b.stage >= row.stages || b.bit >= row.bits || (row.async_reset && !row.reset.present)
        || row.power_on != (row.initial.present && !row.reset.present)) {
      invalid("invalid artifact state semantics");
    }
    ++counts[b.source];
    uint32_t domain = unknown_clock_domain;
    if (row.clock_edge_known && row.clock.present && !row.clock.constant && row.clock.bits == 1) {
      const auto key = std::tuple{row.clock.node, row.clock.port, row.neg_clock};
      domain         = domains.try_emplace(key, static_cast<uint32_t>(domains.size())).first->second;
    }
    if (domain != n.state[i].domain) {
      invalid("artifact clock-domain correspondence mismatch");
    }
    const bool eligible = s.scope == synth::State_scope::logic && row.role == synth::State_role::register_candidate
                          && row.clock_edge_known && !row.neg_clock;
    if (n.state[i].domino_latch.has_value() != eligible) {
      invalid("artifact DominoLatch eligibility mismatch");
    }
  }
  for (size_t i = 0; i < s.sources.size(); ++i) {
    const auto&    row      = s.sources[i];
    const uint64_t expected = uint64_t(row.bits) * row.stages;
    if (counts[i] != row.translated_bits || (counts[i] && counts[i] != expected)
        || (row.role == synth::State_role::register_candidate && (!expected || counts[i] != expected))) {
      invalid("incomplete artifact state translation");
    }
  }
}

void limits_valid(const Artifact_limits& limits) {
  if (!limits.bytes || !limits.decoded_bytes || !limits.objects || !limits.nodes || !limits.string_bytes || !limits.formula_nodes) {
    invalid("invalid artifact resource limits");
  }
}

// Detect damaged local artifacts before allocating their payload. Semantic
// validation remains mandatory; this checksum is not an authentication scheme.
uint64_t checksum(std::string_view bytes, Budget& work) {
  if (!work.spend(bytes.size())) {
    exhausted();
  }
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : bytes) {
    hash = (hash ^ byte) * 1099511628211ULL;
  }
  return hash;
}

Artifact_bytes encode(std::string_view module, synth::State_target target, const Endpoint_netlist& n,
                      const synth::Source_state_table& s, std::span<const uint32_t> bits, Budget& work,
                      const Artifact_limits& limits) {
  Artifact_bytes result;
  try {
    limits_valid(limits);
    Writer a{
        {work, limits},
        {}
    };
    a.scalar(magic);
    a.scalar(version);
    a.string(module);
    enumeration(a, target, 1);
    netlist(a, n);
    source(a, s);
    a.sequence(bits, limits.nodes, [&](auto& b) { a.scalar(b); });
    validate(module, target, n, s, bits, work, limits);
    a.scalar(checksum(a.bytes, work));
    result.bytes  = std::move(a.bytes);
    result.status = Status::feasible;
  } catch (const Refusal& r) {
    result.status = r.status;
    result.reason = r.reason;
  }
  return result;
}
}  // namespace

Artifact_bytes serialize_artifact(std::string_view module, synth::State_target target, const Stateful_region& region, Budget& work,
                                  const Artifact_limits& limits) {
  if (!region.frozen) {
    return {Status::invalid, {}, "region has no frozen netlist"};
  }
  return encode(module, target, *region.frozen, region.source, region.state_bits, work, limits);
}
Artifact_bytes serialize_artifact(const Frozen_region& region, Budget& work, const Artifact_limits& limits) {
  return encode(region.module_name, region.target, region.netlist, region.source, region.state_bits, work, limits);
}
Artifact_result deserialize_artifact(std::string_view bytes, synth::State_target requested_target, Budget& work,
                                     const Artifact_limits& limits) {
  Artifact_result result;
  try {
    limits_valid(limits);
    if (bytes.size() > limits.bytes) {
      exhausted();
    }
    if (bytes.size() < 16) {
      invalid("truncated region artifact");
    }
    const auto payload = bytes.substr(0, bytes.size() - 8);
    Reader     footer{
        {work, limits},
        bytes.substr(payload.size())
    };
    uint64_t digest = 0;
    footer.scalar(digest);
    if (checksum(payload, work) != digest) {
      invalid("region artifact checksum mismatch");
    }
    Reader a{
        {work, limits},
        payload
    };
    uint32_t tag = 0, format = 0;
    fields(a, tag, format);
    if (tag != magic || format != version) {
      invalid("incompatible region artifact format");
    }
    Frozen_region region;
    a.string(region.module_name);
    enumeration(a, region.target, 1);
    netlist(a, region.netlist);
    source(a, region.source);
    a.sequence(region.state_bits, limits.nodes, [&](auto& b) { a.scalar(b); });
    if (a.offset != payload.size()) {
      invalid("trailing bytes in region artifact");
    }
    // Always apply the current target, even to a valid CMOS artifact. The
    // stored target remains provenance and cannot waive DOMINO restrictions.
    if (requested_target != synth::State_target::cmos && requested_target != synth::State_target::domino) {
      invalid("invalid requested artifact target");
    }
    validate(region.module_name, region.target, region.netlist, region.source, region.state_bits, work, limits);
    if (!synth::validate_state_target(region.source, requested_target)) {
      invalid("artifact source violates requested target");
    }
    result.region = std::move(region);
    result.status = Status::feasible;
  } catch (const Refusal& r) {
    result.status = r.status;
    result.reason = r.reason;
  }
  return result;
}
namespace {
constexpr uint32_t selection_magic   = 0x43595355;  // USYC
// Version 16 replaces the bare search work by the search's credit floor;
// version 17 adds the structural work replayed on a hit and the identity
// fallback count.
// Version 23 adds P1 policy, bounded preparation evidence and its work.
// Version 25 adds mux-balancing options and evidence.
constexpr uint32_t selection_version = 25;

template <class A, class T>
void costs(A& a, T& c) {
  fields(a, c.static_logic, c.inverters, c.domino);
}
template <class A, class T>
void credit_fields(A& a, T& c) {
  fields(a, c.work, c.floor, c.bound, c.credits);
}
// Budget::credit_floor() invariants: consumed work never exceeds the floor, a
// bound search's credits cover both, and an unbound one records no credits.
void credit_valid(const Credit_floor& c) {
  if (c.work > c.floor || (c.bound ? c.floor > c.credits : c.credits != 0)) {
    invalid("inconsistent search credit floor");
  }
}
// The structural work of a region includes its logical admission stage.
void structural_valid(uint64_t structural, const Logical_report& report) {
  if (structural < report.work.admission) {
    invalid("inconsistent structural work");
  }
}
template <class A, class T>
void strings(A& a, T& values) {
  a.sequence(values, a.limits.nodes, [&](auto& s) { a.string(s); });
}
template <class A, class T>
void decisions(A& a, T& endpoints) {
  a.sequence(endpoints, a.limits.nodes, [&](auto& e) {
    a.scalar(e.state_index);
    a.string(e.name);
    a.scalar(e.whole_cone);
    a.string(e.origin);
    a.sequence(e.cells, a.limits.nodes, [&](auto& c) {
      fields(a, c.phase, c.latch);
      formula(a, c.formula);
      metrics(a, c.metrics);
      a.sequence(c.inputs, max_logical_inputs, [&](auto& s) { signal(a, s); });
      a.sequence(c.producers, max_logical_inputs, [&](auto& p) { a.scalar(p); });
    });
  });
}
template <class A, class T>
void residual_evidence(A& a, T& s) {
  fields(a,
         s.cost_before,
         s.cost_after,
         s.sop_roots,
         s.sop_cofactors,
         s.sweep_confirmations,
         s.sweep_wins,
         s.balance_groups,
         s.balance_wins,
         s.balance_duplicates,
         s.mux_chains,
         s.mux_arms,
         s.mux_wins,
         s.rewrite_windows,
         s.rewrite_wins,
         s.resub_windows,
         s.resub_wins,
         s.candidates,
         s.depth_rejections,
         s.cost_rejections,
         s.reference_visits,
         s.skipped,
         s.exhausted);
  strings(a, s.limits);
}
template <class A, class T>
void evidence(A& a, T& r) {
  costs(a, r.before);
  costs(a, r.after_pairs);
  costs(a, r.after_residual);
  costs(a, r.after);
  fields(a, r.feedback_attempts, r.feedback_wins, r.feedback_rounds, r.residual_accepted, r.exhausted, r.identity_fallbacks);
  fields(a, r.work.admission, r.work.p1, r.work.selection, r.work.pairs, r.work.residual, r.work.feedback, r.work.cleanup);
  auto& p = r.pairs;
  fields(a,
         p.candidates,
         p.attempts,
         p.combinations,
         p.choices,
         p.choice_combinations,
         p.wins,
         p.work,
         p.trials,
         p.refreshes,
         p.requeues,
         p.stale_skips,
         p.shared_nodes,
         p.more_than_two,
         p.domain_skips,
         p.support_skips,
         p.window_skips,
         p.gain_skips,
         p.bounded_windows,
         p.fanout_windows,
         p.fanout_ports,
         p.fanout_skips,
         p.fanout_wins,
         p.joint_windows,
         p.joint_source_pairs,
         p.joint_partitions,
         p.joint_candidates,
         p.joint_divisors,
         p.joint_combinations,
         p.joint_wins,
         p.joint_care_windows,
         p.joint_care_partitions,
         p.joint_care_phases,
         p.joint_care_attempts,
         p.joint_care_retained,
         p.joint_care_bytes,
         p.joint_care_combinations,
         p.joint_care_wins,
         p.joint_recode_windows,
         p.joint_recode_partitions,
         p.joint_recode_encodings,
         p.joint_recode_phases,
         p.joint_recode_attempts,
         p.joint_recode_retained,
         p.joint_recode_bytes,
         p.joint_recode_combinations,
         p.joint_recode_wins,
         p.exhausted);
  residual_evidence(a, r.p1);
  residual_evidence(a, r.residual);
  a.sequence(r.initial, a.limits.nodes, [&](auto& e) {
    fields(a,
           e.whole_admitted,
           e.window_used,
           e.exhausted,
           e.one_cell_attempts,
           e.two_phase_attempts,
           e.new_divisor_attempts,
           e.single_divisor_attempts,
           e.parallel_divisor_attempts,
           e.deferred_divisor_bytes,
           e.existing_care_attempts,
           e.existing_care_images,
           e.divisor_images,
           e.divisor_image_hits,
           e.image_cache_bytes,
           e.local_attempts,
           e.local_wins,
           e.local_work,
           e.completion_phases,
           e.completion_attempts,
           e.residual_attempts,
           e.boundaries,
           e.boundary_trials,
           e.boundary_replacements,
           e.boundary_bytes_peak,
           e.boundary_wins,
           e.functions,
           e.function_hits,
           e.analysis_tables,
           e.analysis_hits,
           e.analysis_cache_bytes,
           e.cost_visits,
           e.admission_work,
           e.one_cell_work,
           e.boundary_work,
           e.boundary_two_cell_work,
           e.boundary_multi_cell_work,
           e.two_phase_work,
           e.residual_work);
    strings(a, e.limits);
  });
}
void options(Writer& a, const Logical_options& o) {
  const auto& e = o.endpoint;
  fields(a,
         e.gates.logical_inputs,
         e.gates.stack,
         e.gates.branches,
         e.functions.max_cubes,
         e.functions.max_formula_nodes,
         e.functions.factoring_choices,
         e.window.inputs,
         e.window.nodes,
         e.cost.static_and,
         e.cost.static_xor,
         e.cost.static_not,
         e.cost.domino,
         e.cost.domino_latch,
         e.clock_phases,
         e.boundaries,
         e.divisor_partitions,
         e.care_phases,
         e.boundary_bytes,
         e.gate_cache_entries,
         e.gate_cache_bytes,
         e.image_cache_entries,
         e.image_cache_bytes,
         e.local_divisors,
         e.local_candidates,
         e.cost_nodes,
         e.candidate_work,
         e.tier_work,
         e.fast_accept);
  const auto& r = o.residual;
  fields(a,
         r.and_cost,
         r.xor_cost,
         r.depth_slack,
         r.max_nodes,
         r.rewrite_cuts,
         r.balance_dup_limit,
         r.sweep_inputs,
         r.sweep_table_words,
         r.p1_sweep_inputs,
         r.mux_balance_min_arms,
         r.mux_balance_area_pct,
         r.windows,
         r.window_nodes,
         r.resub_inputs,
         r.divisors,
         r.divisor_scan,
         r.inserted,
         r.window_work,
         r.stage_work,
         r.zero_gain,
         r.npn4,
         r.sweep,
         r.balance,
         r.mux_balance,
         r.rewrite,
         r.resubstitute);
  fields(a,
         o.max_nodes,
         o.endpoint_work,
         o.pair_candidates,
         o.pair_trials,
         o.pair_choices,
         o.pair_inputs,
         o.pair_work,
         o.pre_optimize,
         o.optimize_residual,
         o.feedback);
}
void validate_decisions(const Stateful_region& s, const Logical_report& report, Budget& work, const Artifact_limits& limits) {
  // Every counted identity fallback is published as an identity endpoint.
  const auto identities = std::count_if(s.selected.endpoints.begin(), s.selected.endpoints.end(), [](const auto& e) {
    return e.origin == "identity";
  });
  if (!s.frozen || report.initial.size() != s.selected.endpoints.size() || report.feedback_rounds > 1
      || report.identity_fallbacks > static_cast<uint64_t>(identities)) {
    invalid("incomplete cached endpoint decisions");
  }
  const auto& n = *s.frozen;
  if (report.after.static_logic != n.estimated_cost.static_logic || report.after.inverters != n.estimated_cost.inverters
      || report.after.domino != n.estimated_cost.domino) {
    invalid("cached decision cost mismatch");
  }
  Logical_options o;
  o.endpoint.gates                       = n.gates;
  o.endpoint.cost                        = n.costs;
  o.endpoint.clock_phases                = n.clock_phases;
  o.max_nodes                            = limits.nodes;
  o.endpoint.functions.max_formula_nodes = limits.formula_nodes;
  std::vector<uint32_t> domains;
  for (const auto& state : n.state) {
    domains.push_back(state.domain);
  }
  auto frozen = freeze_endpoint_netlist(s.selected, o, work, domains);
  if (!frozen.netlist) {
    throw Refusal{frozen.status, "invalid cached endpoint decisions"};
  }
  Writer original{
      {work, limits},
      {}
  },
      rebuilt{{work, limits}, {}};
  netlist(original, n);
  netlist(rebuilt, *frozen.netlist);
  if (original.bytes != rebuilt.bytes) {
    invalid("cached endpoint decisions do not match frozen netlist");
  }
}
std::string_view checked_payload(std::string_view bytes, Budget& work, const Artifact_limits& limits) {
  limits_valid(limits);
  if (bytes.size() > limits.bytes) {
    exhausted();
  }
  if (bytes.size() < 16) {
    invalid("truncated selection record");
  }
  const auto payload = bytes.substr(0, bytes.size() - 8);
  Reader     footer{
      {work, limits},
      bytes.substr(payload.size())
  };
  uint64_t digest = 0;
  footer.scalar(digest);
  if (checksum(payload, work) != digest) {
    invalid("selection record checksum mismatch");
  }
  return payload;
}

// Version 2 drops the definition name, source graph, spans, raw node numbers
// and internal boundary-net names from the logical identity; version 3 drops
// the search credits (the record's credit floor decides reuse).
constexpr uint32_t identity_version = 3;

// Local node numbers identify controls only within one source graph, and a
// recompile renumbers them. The identity keeps their equalities by first use.
// A constant is its value: its pin is a constant-pool slot that any other
// constant added to the graph can shift.
struct Canonical_nodes {
  std::map<uint64_t, uint32_t> ids;
  uint32_t operator()(uint64_t node) { return ids.try_emplace(node, static_cast<uint32_t>(ids.size())).first->second; }
};
void identity_signal(Writer& a, Canonical_nodes& nodes, const synth::Source_signal& s) {
  fields(a, s.present, s.constant);
  if (s.present) {
    if (!s.constant) {
      a.scalar(nodes(s.node));
      a.scalar(s.port);
    }
    a.scalar(s.bits);
    value(a, s.value);
  }
}
// Every semantic field of a snapshot, without its provenance. A row with Q is
// a register/latch and keeps its matched spelling; any other row's spelling is
// a diagnostic identity (node number and a nearby wire), replaced by the
// owner's stable barrier name when one is supplied.
void identity_source(Writer& a, const synth::Source_state_table& s, std::span<const std::string> barriers) {
  enumeration(a, s.scope, 2);
  a.scalar(s.logical_boundary);
  Canonical_nodes nodes;
  size_t          index = 0;
  a.sequence(s.sources, a.limits.nodes, [&](auto& row) {
    a.scalar(nodes(row.node));
    a.string(row.q.present ? std::string_view{row.name}
                           : (barriers.empty() ? std::string_view{} : std::string_view{barriers[index]}));
    ++index;
    enumeration(a, row.role, 5);
    fields(a, row.bits, row.stages, row.width_known);
    for (const auto* signal : {&row.q, &row.data, &row.clock, &row.clock_root, &row.enable, &row.reset, &row.initial}) {
      identity_signal(a, nodes, *signal);
    }
    fields(a, row.neg_clock, row.clock_edge_known, row.neg_reset, row.async_reset, row.power_on, row.icg, row.translated_bits);
  });
  a.sequence(s.clocks, a.limits.nodes, [&](auto& c) {
    identity_signal(a, nodes, c.output);
    identity_signal(a, nodes, c.clock);
    identity_signal(a, nodes, c.enable);
    a.scalar(nodes(c.latch_node));
    a.scalar(c.parent);
    a.string(c.name);
  });
  a.sequence(s.bits, a.limits.nodes, [&](auto& b) {
    fields(a, b.source, b.stage, b.bit, b.latch);
    a.string(b.name);
    fields(a, b.q.node, b.q.inverted, b.d.node, b.d.inverted);
  });
  a.sequence(s.controls, a.limits.nodes, [&](auto& c) {
    a.scalar(c.source);
    enumeration(a, c.kind, 2);
    a.sequence(c.outputs, a.limits.nodes, [&](auto& o) { a.scalar(o); });
  });
}

// Source spellings of a canonical graph, replaced at the given positions;
// nullopt when every spelling already matches. Structural hashing reproduces
// every node id of a canonical graph; one that does not is refused rather
// than guessed.
std::optional<Xag> renamed_graph(const Xag& graph, std::span<const Xsignal> positions, std::span<const std::string> spellings,
                                 Budget& work) {
  auto names   = graph.input_names();
  bool changed = false;
  for (size_t i = 0; i < positions.size(); ++i) {
    const auto id = positions[i].id;
    if (id >= graph.size() || graph.node(id).kind != Xag::Kind::source || graph.node(id).source_index >= names.size()) {
      invalid("cached boundary position is not a graph source");
    }
    auto& name = names[graph.node(id).source_index];
    changed    = changed || name != spellings[i];
    name       = spellings[i];
  }
  if (!changed) {
    return std::nullopt;
  }
  Xag out;
  for (Id i = 0; i < graph.size(); ++i) {
    if (!work.spend()) {
      exhausted();
    }
    const auto& n = graph.node(i);
    Xsignal     actual;
    if (n.kind == Xag::Kind::source) {
      actual = out.input(names[n.source_index]);
      if (out.node(actual.id).source_index != n.source_index) {
        invalid("noncanonical cached graph sources");
      }
    } else if (n.kind == Xag::Kind::and_gate) {
      actual = out.land(n.inputs[0], n.inputs[1]);
    } else if (n.kind == Xag::Kind::xor_gate) {
      actual = out.lxor(n.inputs[0], n.inputs[1]);
    } else if (i != 0) {
      invalid("noncanonical cached graph constant");
    }
    if (actual != Xsignal{i, false}) {
      invalid("noncanonical cached graph node");
    }
  }
  return out;
}
template <class S>
std::vector<Xsignal> state_sources(const std::vector<S>& state) {
  std::vector<Xsignal> q;
  q.reserve(state.size());
  for (const auto& s : state) {
    q.push_back(s.q);
  }
  return q;
}
}  // namespace

Artifact_bytes serialize_logical_identity(const synth::Lnet& raw, const synth::Source_state_table& snapshot,
                                          const Identity_names& names, const Logical_options& logical, uint64_t producer,
                                          std::string_view context, Budget& work, const Artifact_limits& limits) {
  Artifact_bytes result;
  try {
    limits_valid(limits);
    if ((!names.inputs.empty() && names.inputs.size() != raw.inputs().size())
        || (!names.outputs.empty() && names.outputs.size() != raw.outputs().size())
        || (!names.barriers.empty() && names.barriers.size() != snapshot.sources.size())) {
      invalid("identity names do not match the region boundary");
    }
    Writer a{
        {work, limits},
        {}
    };
    a.scalar(selection_version);
    a.scalar(identity_version);
    a.scalar(Frozen_region::version);
    a.scalar(Endpoint_netlist::version);
    a.scalar(producer);
    a.string(context);
    options(a, logical);
    identity_source(a, snapshot, names.barriers);
    a.count(raw.size(), limits.nodes);
    a.scalar(uint32_t(raw.size()));
    for (synth::Lid i = 0; i < raw.size(); ++i) {
      auto kind = raw.kind(i);
      enumeration(a, kind, 2);
      a.sequence(raw.fanins(i), synth::Lnet::kMaxFanin, [&](auto& f) { a.scalar(f); });
      a.sequence(raw.table(i), 4, [&](auto& word) { a.scalar(word); });
    }
    // RAW port spellings are region-boundary net names: only the top-level IO
    // a position binds is identity. Latch spellings are register names.
    size_t index = 0;
    a.sequence(raw.inputs(), limits.nodes, [&](auto& p) {
      a.scalar(p.node);
      a.string(names.inputs.empty() ? std::string_view{} : std::string_view{names.inputs[index]});
      ++index;
    });
    index = 0;
    a.sequence(raw.outputs(), limits.nodes, [&](auto& p) {
      fields(a, p.node, p.created);
      a.string(names.outputs.empty() ? std::string_view{} : std::string_view{names.outputs[index]});
      ++index;
    });
    a.sequence(raw.latches(), limits.nodes, [&](auto& s) {
      fields(a, s.init, s.q, s.d);
      a.string(s.name);
    });
    strings(a, names.ports);
    result.status = Status::feasible;
    result.bytes  = std::move(a.bytes);
  } catch (const Refusal& r) {
    result.status = r.status;
    result.reason = r.reason;
  }
  return result;
}

Status rebind_selection(Selection_record& record, std::string_view module, const synth::Lnet& raw,
                        const synth::Source_state_table& source, Budget& work, const Artifact_limits& limits) {
  try {
    limits_valid(limits);
    auto& cached = record.selected;
    if (module.empty() || !cached.frozen) {
      invalid("cache record lacks a frozen netlist");
    }
    // Only provenance may differ: compare both snapshots in identity form.
    Writer before{
        {work, limits},
        {}
    },
        after{{work, limits}, {}};
    identity_source(before, cached.source, {});
    identity_source(after, source, {});
    if (before.bytes != after.bytes) {
      invalid("cached source semantics differ from the fresh translation");
    }
    // The fresh import yields exactly the spellings and state names that a
    // cold search of this translation starts from.
    auto fresh = import_semantic_region(raw, source, synth::State_target::cmos, work, limits.nodes);
    if (!fresh.region) {
      throw Refusal{fresh.status == Status::search_exhausted ? Status::search_exhausted : Status::invalid,
                    "fresh region import refused"};
    }
    const auto& logic    = fresh.region->logic;
    auto&       netlist  = *cached.frozen;
    auto&       expanded = cached.selected.logic;
    const auto  sizes    = [&](const auto& region) {
      return region.inputs.size() == logic.inputs.size() && region.state.size() == logic.state.size()
             && region.outputs.size() == logic.outputs.size();
    };
    if (fresh.region->state_bits != cached.state_bits || !sizes(netlist) || !sizes(expanded)) {
      invalid("cached boundary differs from the fresh translation");
    }
    for (size_t i = 0; i < logic.state.size(); ++i) {
      if (!work.spend()) {
        exhausted();
      }
      if (logic.state[i].name != netlist.state[i].name || logic.state[i].name != expanded.state[i].name
          || fresh.region->domains[i] != netlist.state[i].domain) {
        invalid("cached state names or clock domains differ from the fresh translation");
      }
    }
    // Positions: every input, then every state source, in logical order.
    std::vector<std::string> spellings;
    spellings.reserve(logic.inputs.size() + logic.state.size());
    const auto spelling = [&](Xsignal s) { return logic.graph.input_names()[logic.graph.node(s.id).source_index]; };
    for (const auto input : logic.inputs) {
      spellings.push_back(spelling(input));
    }
    for (const auto& state : logic.state) {
      spellings.push_back(spelling(state.q));
    }
    const auto positions = [&](const auto& region) {
      auto all = region.inputs;
      for (const auto q : state_sources(region.state)) {
        all.push_back(q);
      }
      return all;
    };
    auto native = renamed_graph(netlist.native, positions(netlist), spellings, work);
    auto graph  = renamed_graph(expanded.graph, positions(expanded), spellings, work);
    // Commit only after every check: refusal leaves the record unchanged.
    if (native) {
      netlist.native = std::move(*native);
    }
    if (graph) {
      expanded.graph = std::move(*graph);
    }
    for (size_t j = 0; j < logic.outputs.size(); ++j) {
      netlist.outputs[j].name  = logic.outputs[j].name;
      expanded.outputs[j].name = logic.outputs[j].name;
    }
    cached.source      = source;
    record.module_name = module;
    return Status::feasible;
  } catch (const Refusal& r) {
    return r.status;
  }
}

Artifact_bytes serialize_selection_record(std::string_view key, std::string_view module, const Stateful_region& selected,
                                          const Logical_report& report, const Credit_floor& credit, uint64_t structural,
                                          Budget& work, const Artifact_limits& limits) {
  Artifact_bytes result;
  try {
    limits_valid(limits);
    if (!selected.frozen) {
      invalid("cache record lacks frozen netlist");
    }
    credit_valid(credit);
    structural_valid(structural, report);
    Writer a{
        {work, limits},
        {}
    };
    a.scalar(selection_magic);
    a.scalar(selection_version);
    a.string(key);
    a.string(module);
    credit_fields(a, credit);
    a.scalar(structural);
    netlist(a, *selected.frozen);
    source(a, selected.source);
    a.sequence(selected.state_bits, limits.nodes, [&](auto& b) { a.scalar(b); });
    decisions(a, selected.selected.endpoints);
    evidence(a, report);
    validate(module, synth::State_target::cmos, *selected.frozen, selected.source, selected.state_bits, work, limits);
    validate_decisions(selected, report, work, limits);
    a.scalar(checksum(a.bytes, work));
    result.status = Status::feasible;
    result.bytes  = std::move(a.bytes);
  } catch (const Refusal& r) {
    result.status = r.status;
    result.reason = r.reason;
  }
  return result;
}
Selection_record_result deserialize_selection_record(std::string_view bytes, std::string_view key, synth::State_target target,
                                                     Budget& work, const Artifact_limits& limits) {
  Selection_record_result result;
  try {
    if (target != synth::State_target::cmos && target != synth::State_target::domino) {
      invalid("unknown requested state target");
    }
    Reader a{
        {work, limits},
        checked_payload(bytes, work, limits)
    };
    uint32_t tag = 0, format = 0;
    fields(a, tag, format);
    if (tag != selection_magic || format != selection_version) {
      invalid("incompatible selection record");
    }
    std::string stored_key;
    a.string(stored_key);
    if (stored_key != key) {
      invalid("selection record identity mismatch");
    }
    Selection_record record;
    a.string(record.module_name);
    credit_fields(a, record.credit);
    credit_valid(record.credit);
    a.scalar(record.structural);
    auto& s = record.selected;
    s.frozen.emplace();
    netlist(a, *s.frozen);
    source(a, s.source);
    a.sequence(s.state_bits, limits.nodes, [&](auto& b) { a.scalar(b); });
    decisions(a, s.selected.endpoints);
    evidence(a, record.report);
    if (a.offset != a.bytes.size()) {
      invalid("trailing selection record bytes");
    }
    structural_valid(record.structural, record.report);
    validate(record.module_name, target, *s.frozen, s.source, s.state_bits, work, limits);
    s.selected.logic = expand_endpoint_netlist(*s.frozen, work, limits.nodes, limits.formula_nodes);
    if (s.selected.logic.status != Status::feasible) {
      throw Refusal{s.selected.logic.status, "cached expansion refused"};
    }
    s.selected.cost = s.frozen->estimated_cost;
    validate_decisions(s, record.report, work, limits);
    result.record = std::move(record);
    result.status = Status::feasible;
  } catch (const Refusal& r) {
    result.status = r.status;
    result.reason = r.reason;
  }
  return result;
}
}  // namespace livehd::usyn
