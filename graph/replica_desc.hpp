// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Replica_desc — the compile-time replication descriptor carried by a
// `Ntype_op::Sub` node, per `todo_loop_cond_sub.md`.
//
// One Sub node with a descriptor denotes `count` spatial instances of the same
// callee definition. Graph size stays O(1) in `count`; realized hardware and
// state remain O(count). Replica ordinal `r` is in [0,count) and the
// source-visible loop value is `first + r*step`.
//
// Two rules make this representation safe for every generic traversal:
//   - carries are DESCRIPTOR MAPPINGS, never graph self-edges. An ordinary Sub
//     input keeps exactly one external driver (the initial value for replica 0)
//     and `carries` says that output `output_pid` of replica `r` becomes input
//     `input_pid` of replica `r+1`. No false combinational cycle is created and
//     the single-driver contract is untouched.
//   - a descriptor never implies `loop_break`. That HHDS Type bit doubles as
//     `forward_is_source` and is stamped automatically from the callee's
//     contents; replication must not change traversal order.
//
// `is_replicated_sub(node)` means exactly "this node carries a valid
// descriptor" — never inferred from a name, port id, self-edge, or callee
// contents.
//
// The descriptor serializes as a self-describing string attribute whose FIRST
// field is the version (see `serialize`), following the coloring_info pattern.
// A payload whose version is unknown fails to parse with a stale-artifact
// diagnostic rather than being silently ignored: at the hhds layer an
// unregistered attr tag is a dbg-only assert and an opt-build null deref, so
// the version check has to live here.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "attrs.hpp"
#include "cell.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Bumped only on an incompatible payload change. A reader that meets a newer
// version refuses the artifact instead of guessing.
inline constexpr uint16_t Replica_desc_version = 1;

struct Replica_carry {
  hhds::Port_id input_pid  = livehd::Port_invalid;  // destination on replica r+1
  hhds::Port_id output_pid = livehd::Port_invalid;  // source on replica r

  [[nodiscard]] bool operator==(const Replica_carry& o) const noexcept {
    return input_pid == o.input_pid && output_pid == o.output_pid;
  }
};

struct Replica_desc {
  int64_t  first = 0;
  int64_t  step  = 1;  // never zero
  uint64_t count = 0;  // zero is legal: instantiates nothing

  // Role-marked boundary ports. `index_input` is absent when the body never
  // reads the loop value; `activation_input` is present iff the callee is
  // activation-capable; `next_active_output` is present iff `break` chains
  // activation along the ordinal (rule 13). All three participate in
  // descriptor equality — a chained-vs-unchained activation is a semantic
  // difference that nothing else in the graph records.
  std::optional<hhds::Port_id> index_input;
  std::optional<hhds::Port_id> activation_input;
  std::optional<hhds::Port_id> next_active_output;

  std::vector<Replica_carry> carries;

  [[nodiscard]] bool operator==(const Replica_desc& o) const noexcept {
    return first == o.first && step == o.step && count == o.count && index_input == o.index_input
           && activation_input == o.activation_input && next_active_output == o.next_active_output && carries == o.carries;
  }

  // Source-visible loop value of ordinal `r`. Returns nullopt when the value
  // would overflow int64 (the descriptor validator rejects such a descriptor,
  // so a valid descriptor never returns nullopt for r < count).
  [[nodiscard]] std::optional<int64_t> index_at(uint64_t ordinal) const;

  // Smallest signed width that represents every generated index. The index
  // boundary port is SIGNED (the emitted Verilog localparam is signed), so an
  // unsigned minimal width would silently wrap: domain 0..=15 needs 5 signed
  // bits, not 4. Returns 1 for an empty domain.
  [[nodiscard]] int index_signed_bits() const;

  // True when `output_pid` feeds some carry (one output may source several
  // carries; only destinations must be unique).
  [[nodiscard]] bool is_carry_source(hhds::Port_id output_pid) const;
  [[nodiscard]] bool is_carry_dest(hhds::Port_id input_pid) const;

  // Structural problems that make the descriptor unrealizable. Empty string
  // means valid. Checked by the graph validator and by every consumer that
  // deserializes a descriptor, so a hand-built or stale graph cannot smuggle a
  // malformed domain into a code generator.
  [[nodiscard]] std::string validate() const;

  // `version=N;...` — the version is always the first field so a reader can
  // refuse before interpreting anything else.
  [[nodiscard]] std::string serialize() const;
};

// Parses a serialized descriptor. Returns nullopt and fills `err` (when
// non-null) on a malformed payload, an unknown version, or a payload that
// fails `validate()`.
[[nodiscard]] std::optional<Replica_desc> replica_desc_from_string(std::string_view txt, std::string* err = nullptr);

// ---------------------------------------------------------------------------
// Node-level accessors. The descriptor rides `livehd::attrs::replica_desc`, a
// string-valued per-node attribute that persists with the graph body.
// ---------------------------------------------------------------------------

// True iff `node` is a Sub carrying the descriptor ATTRIBUTE — whether or not
// this build can parse it. This is the ONE shared predicate; every
// occurrence-blind consumer gates on it, and a payload it cannot read must make
// those guards REFUSE rather than fall back to "an ordinary single instance".
[[nodiscard]] bool is_replicated_sub(const hhds::Node_class& node);

// The descriptor of `node`, or nullopt when the node carries none. When the
// attribute is PRESENT but unparseable (unknown version, corrupt payload) this
// also returns nullopt and fills `err`: a caller that got `true` from
// `is_replicated_sub` must treat that as a hard failure, never as "absent".
[[nodiscard]] std::optional<Replica_desc> replica_desc_of(const hhds::Node_class& node, std::string* err = nullptr);

// Attaches `desc` to `node`. `node` must be a Sub and `desc` must validate;
// both are checked with an assert (a caller that can fail should call
// `validate()` and report a diagnostic itself).
void set_replica_desc(const hhds::Node_class& node, const Replica_desc& desc);

void del_replica_desc(const hhds::Node_class& node);

// True iff any node in `g` carries a descriptor. Cheap enough to call at pass
// entry: it is the library-level gate that keeps occurrence-blind passes from
// silently treating a compact node as one physical instance.
[[nodiscard]] bool graph_has_replicated_subs(hhds::Graph* g);

}  // namespace livehd::graph_util
