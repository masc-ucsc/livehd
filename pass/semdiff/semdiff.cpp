// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "semdiff.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "node_util.hpp"

namespace livehd::semdiff {

namespace gu = livehd::graph_util;

namespace {

// ---- hashing ---------------------------------------------------------------
// A self-contained 64-bit mixer (no external dep). Signatures are compared by
// value equality across the two designs, hashed in one process, so the mix only
// needs to be deterministic within the run.
constexpr uint64_t mix64(uint64_t x) {
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}
constexpr uint64_t hcombine(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U))); }
uint64_t           hstr(std::string_view s) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

// State cells are cut points (their data input is not followed): structure does
// not flow through them unless matching_names seeds them by hierarchical name.
bool is_state(Ntype_op op) {
  return op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory;
}

// A Sub whose body holds state is a cut point TOO. hhds encodes is_loop_break in
// bit 0 of Ntype_op (graph/cell.hpp:109 static_asserts; IO=39, Memory=41, Flop=43,
// Latch=45, Fflop=47, Sub=49 are the odd ones) and HOISTS every loop_break node to
// a topological SOURCE — emitted before its drivers, exactly like a Flop
// (hhds/graph.cpp:2451-2453 "cut point: no ordering edges lead INTO it"). A Flop
// survives that only because we PRE-SEED its fsig/bsig, which makes the emission
// order irrelevant; an unseeded Sub instead fails the readiness check and starves
// its whole fanout cone. Sub instances only carry the bit when their body has
// state, so `is_loop_break()` is exactly the right question — and it is the SAME
// bit hhds used to order the graph, so the two can never disagree (a stale
// not-loop_break Sub is not hoisted, gets a normal structural fsig, and needs no
// seed). IO is NOT here: it is two singleton nodes below kFirstUserNodeIdx that
// forward_class never emits, and its pins already resolve by name.
bool is_cut(const hhds::Node_class& node, bool blackbox_subs = false) {
  auto op = gu::type_op_of(node);  // LINK-based: set_subnode re-stamps the raw type
  // blackbox_subs (incremental region reuse): EVERY Sub is a cut point, not just
  // loop_break ones -- its inputs are still folded here (input rewiring is caught)
  // but its outputs are seeded sources, so a comb loop through a submodule breaks
  // and the compare keys on the Sub's IO wiring, not its (separately-cached) body.
  return is_state(op) || (op == Ntype_op::Sub && (blackbox_subs || node.is_loop_break()));
}

// Name-normalization mirror of pass/lec/encode.cpp's flop_state_key (kept local
// so pass/semdiff stays free of the cvc5-heavy pass/lec dep): strip the SSA /
// hierarchical decoration so the same RTL register matches across front-ends.
std::string normalize_reg_name(std::string_view raw) {
  std::string_view s = raw;
  // Slang transports an escaped Verilog identifier as a backtick-delimited
  // LGraph name (`bank.x`). Pyrope already carries the canonical `bank.x`.
  // The delimiters are reader syntax, not part of the RTL state identity.
  if (s.size() >= 2 && s.front() == '`' && s.back() == '`') {
    s.remove_prefix(1);
    s.remove_suffix(1);
  }
  if (!s.empty() && s.front() == '$') {
    if (auto e = s.find('$', 1); e != std::string_view::npos) {
      s.remove_prefix(e + 1);
    }
  }
  if (auto p = s.find("___ssa_"); p != std::string_view::npos) {
    s = s.substr(0, p);
  }
  return std::string{s};
}

std::string state_key(hhds::Graph* g, const hhds::Node_class& node) {
  if (auto origin = node.attr(livehd::attrs::aggregate_origin); origin.has() && !origin.get().empty()) {
    return "n:" + normalize_reg_name(origin.get());
  }
  auto pn = gu::pin_name_of(node.get_driver_pin(0));
  if (pn.empty()) {
    pn = gu::node_name_of(node);
  }
  if (auto nm = normalize_reg_name(pn); !nm.empty()) {
    return "n:" + nm;
  }
  auto ref = node.attr(hhds::attrs::srcid);
  if (ref.has()) {
    auto span = g->source_locator().resolve_span(ref.get());
    if (!span.file.empty()) {
      return std::format("s:{}-{}@{}", span.start_byte.value_or(0), span.end_byte.value_or(0), span.file);
    }
  }
  return "f:" + std::to_string(static_cast<uint64_t>(node.get_debug_nid()));
}

// The cross-side identity of a cut point. A cut Sub is keyed by its INSTANCE
// hierarchical name (node_kind_key already folds the def gid, so the def identity
// is covered separately); a state cell keeps its existing state_key.
std::string cut_point_key(hhds::Graph* g, const hhds::Node_class& node) {
  if (gu::type_op_of(node) == Ntype_op::Sub) {
    return "u:" + normalize_reg_name(node.get_hier_name());
  }
  return state_key(g, node);
}

// Width of a node's first sized driver pin (0 = unknown). Folded into the key so
// differing literal widths never falsely match.
int32_t node_out_bits(const hhds::Node_class& node) {
  for (const auto& e : node.out_edges()) {
    if (auto b = gu::bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
}

// A Sub's INTERFACE identity: def name + every port's name/width/sign, folded
// order-independently (XOR over the decls). Round-trip stable -- keyed on names,
// never on port_id/get_subnode_gid (both are node-id-like handles the abc cache
// round-trip reassigns; see seed_cut_subs). 0 for a non-Sub or a Sub with no
// resolvable IO. Used by the traversal verify to enforce the blackbox contract:
// a child-body edit leaves this unchanged, a child-interface change breaks it.
uint64_t sub_iface_key(const hhds::Node_class& node) {
  if (gu::type_op_of(node) != Ntype_op::Sub) {
    return 0;
  }
  auto io = node.get_subnode_io();
  if (!io) {
    return 0;
  }
  uint64_t h    = hcombine(hstr("\x01subif"), hstr(io->get_name()));
  uint64_t acc  = 0;
  auto     fold = [&](const auto& decls, uint64_t tag) {
    for (const auto& p : decls) {
      uint64_t ph  = hcombine(tag, hstr(p.name));
      ph           = hcombine(ph, static_cast<uint64_t>(p.bits));
      ph           = hcombine(ph, p.unsign ? 1ULL : 2ULL);
      acc         ^= ph;
    }
  };
  fold(io->get_input_pin_decls(), hstr("\x01i"));
  fold(io->get_output_pin_decls(), hstr("\x01o"));
  return hcombine(h, acc);
}

// Cross-design identity of a native compact-loop descriptor. Port ids are
// allocation-local, so resolve every role/carry endpoint through the callee IO
// and fold names plus declared shape. This participates in BOTH structural
// identity and canonical_digest through node_kind_key: a descriptor-only edit
// must never hit the no-solver semdiff shortcut or a warm verdict-cache entry.
uint64_t loop_descriptor_key(const hhds::Node_class& node) {
  auto loop = node.subnode_loop();
  auto io   = node.get_subnode_io();
  if (!loop || !io) {
    return 0;
  }

  auto port_key = [&](hhds::Port_id pid, bool input) {
    const auto& decls = input ? io->get_input_pin_decls() : io->get_output_pin_decls();
    for (const auto& p : decls) {
      if (p.port_id != pid) {
        continue;
      }
      uint64_t h = hcombine(hstr(input ? "\x01li" : "\x01lo"), hstr(p.name));
      h          = hcombine(h, static_cast<uint64_t>(p.bits));
      return hcombine(h, p.unsign ? 1ULL : 2ULL);
    }
    // Malformed descriptors are rejected by HHDS validation / occurrence
    // materialization. Keep the structural key fail-closed meanwhile: an
    // unresolved pid is side-local and therefore cannot match by coincidence.
    const std::string_view bad_tag = input ? std::string_view{"\x01" "bad-li"} : std::string_view{"\x01" "bad-lo"};
    return hcombine(hstr(bad_tag), static_cast<uint64_t>(pid));
  };

  uint64_t h = hstr("\x01subnode-loop-v1");
  h          = hcombine(h, static_cast<uint64_t>(loop->first));
  h          = hcombine(h, static_cast<uint64_t>(loop->step));
  h          = hcombine(h, loop->count);
  h          = hcombine(h, loop->index_input ? port_key(*loop->index_input, true) : hstr("\x01no-index"));
  h          = hcombine(h, loop->activation_input ? port_key(*loop->activation_input, true) : hstr("\x01no-active"));
  h          = hcombine(h, loop->next_active_output ? port_key(*loop->next_active_output, false) : hstr("\x01no-next-active"));

  std::vector<uint64_t> carries;
  for (const auto& c : node.subnode_group().carries()) {
    carries.push_back(hcombine(port_key(c.input_port(), true), port_key(c.output_port(), false)));
  }
  std::sort(carries.begin(), carries.end());
  for (uint64_t c : carries) {
    h = hcombine(h, c);
  }
  return h;
}

// op + width + subnode identity: the local "kind" part of a node's key, shared
// by the forward and backward signatures (a Sub also folds in its def gid so two
// instances of different modules never match).
uint64_t node_kind_key(const hhds::Node_class& node) {
  uint64_t h = hcombine(0x5e3d1ff0ULL, static_cast<uint64_t>(gu::type_op_of(node)));
  h          = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(node_out_bits(node))));
  if (auto gid = node.get_subnode_gid(); gid != hhds::Gid_invalid) {
    h = hcombine(h, static_cast<uint64_t>(gid));
  }
  if (node.is_loop_subnode()) {
    h = hcombine(h, loop_descriptor_key(node));
  }
  return h;
}

// Fold a port-grouped operand list into a signature: commutative-normalize
// WITHIN each sink-port class (sort the operand sigs that share a port) but
// never across ports — so Sum's added (port A) and subtracted (port B) operands
// stay distinct while `a+b == b+a`.
uint64_t fold_operands(uint64_t base, absl::flat_hash_map<int, std::vector<uint64_t>>& by_port) {
  std::vector<int> ports;
  ports.reserve(by_port.size());
  for (auto& [p, _] : by_port) {
    ports.push_back(p);
  }
  std::sort(ports.begin(), ports.end());
  uint64_t h = base;
  for (int p : ports) {
    h        = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(p)) | (1ULL << 40U));  // port marker
    auto& vs = by_port[p];
    std::sort(vs.begin(), vs.end());
    for (uint64_t v : vs) {
      h = hcombine(h, v);
    }
  }
  return h;
}

// ---- tier-2 state pairing (full-match, the simplified SynAlign scheme) ------
// Pair the state cells tier-1 name matching left unmatched, by structural
// signature: a cell pairs when the RESOLVED POINTS in its transitive fan-in
// (SRP) and fan-out (ERP) — graph IOs + already-paired state cells, walked over
// the sequential (state-to-state through comb) graph — are EXACTLY equal on
// both sides and the signature is unambiguous (one candidate per side). Newly
// paired cells become resolved points themselves; iterate to a fixed point.
// Full-match only, ambiguity-conservative: no half/partial relaxation — the
// paper's full matches were the 100%-precise tier, and lec re-verifies anyway.
// Clock/reset/comptime pins are excluded (globally shared, no discriminating
// information); init values fold into the signature (the 2f-lec pair
// precondition: never pair state whose reset values differ).

// The sink ports of a state cell that carry DATA (traced by the sequential
// walk). Everything else — clock, reset, polarity, pipe bounds — is excluded.
bool data_sink_port(Ntype_op op, int pid) {
  switch (op) {
    case Ntype_op::Flop:
    case Ntype_op::Latch: return pid == 3 || pid == 4;              // din, enable
    case Ntype_op::Fflop: return pid == 0 || pid == 3 || pid == 5;  // valid, din, stop
    case Ntype_op::Memory:
      return pid == 0 || pid == 3 || pid == 4 || pid == 12 || pid == 13;  // addr, din, enable, update, update_enable
    default: return true;                                                 // comb node: every input is data
  }
}

struct State_cell {
  hhds::Node_class                           node;
  std::string                                key;            // state_key (tier-1 identity; mangled under name_noise)
  std::string                                truth;          // name_noise only: the original key (empty = key is original)
  std::string                                aggregate_key;  // empty unless this is an SROA leaf
  bool                                       is_mem = false;
  uint64_t                                   kind   = 0;   // op + bits + init fold (local identity)
  std::vector<uint32_t>                      preds;        // state cells feeding a data pin through comb
  std::vector<uint32_t>                      succs;        // state cells fed from a driver pin through comb
  std::vector<uint64_t>                      in_anchors;   // graph-input tokens feeding a data pin
  std::vector<uint64_t>                      out_anchors;  // graph-output tokens reached from a driver pin
  // Transitive closure over the state graph, with the min HOP DISTANCE (BFS
  // level). Distance-annotated resolved points are a deliberate refinement over
  // the paper's plain sets: a linear chain (in -> A -> B -> out) is permanently
  // ambiguous under {in}/{out} set equality, while (in,1)/(in,2) tells A from B.
  // Annotation only refines buckets, and lec re-verifies every pair anyway.
  std::vector<std::pair<uint32_t, uint32_t>> reach_b;  // (index, dist>=1)
  std::vector<std::pair<uint32_t, uint32_t>> reach_f;
  uint64_t                                   token   = 0;  // nonzero = resolved (tier-1 seed or tier-2 pair)
  bool                                       t1_pair = false, t1_group = false, t2_pair = false, ambiguous = false;
  uint64_t                                   kind_nw    = 0;      // `kind` WITHOUT the width term (see collect_state)
  bool                                       kind_clash = false;  // unpaired: cross-side SRP/ERP match refused by the
                                                                  // kind fold (op/init — the pair precondition)
};

struct State_side {
  std::vector<State_cell>                          cells;
  absl::flat_hash_map<hhds::Class_index, uint32_t> index;  // node -> cells[] slot
  absl::flat_hash_map<uint64_t, std::string>       label;  // explain only: RP token -> name
};

State_side collect_state(hhds::Graph* g, const Semdiff_options& opts) {
  const bool want_labels = opts.explain_noise > 0;
  State_side ss;
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    auto op = gu::type_op_of(node);
    if (!is_persistent_state(node)) {
      continue;
    }
    State_cell c;
    c.node = node;
    c.key  = state_key(g, node);
    if (auto origin = node.attr(livehd::attrs::aggregate_origin); origin.has()) {
      c.aggregate_key = normalize_reg_name(origin.get());
    }
    c.is_mem = op == Ntype_op::Memory;
    c.kind   = hcombine(hstr("\x01skind"), static_cast<uint64_t>(op));
    // COMMIT CLASS folds into the identity for Flop AND Latch (2f-latch M2),
    // mirroring the initial-value fold below. `posclk` means posedge-vs-negedge
    // on a flop and enable-active-high-vs-low on a latch; either way two cells
    // that commit on OPPOSITE edges are not the same state element and must not
    // be keyed alike. This is keying HYGIENE, not a soundness hole — the
    // compare-point obligation pass already catches a yosys-imported polarity
    // flip functionally — but a wrong key wastes a pairing slot and produces a
    // misleading diff.
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
      // Absent posclk == the default (posedge / active-high), so an unset pin
      // and an explicit `true` must fold IDENTICALLY or the two spellings of
      // the same cell would stop pairing.
      bool pos = true;
      if (auto pc = gu::get_driver_of_sink_name(node, "posclk"); !pc.is_invalid() && gu::is_const_pin(pc)) {
        pos = !gu::hydrate_const(pc).is_known_false();
      }
      c.kind = hcombine(c.kind, hstr(pos ? "\x01commit+" : "\x01commit-"));
    }
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop) {
      // Reset/init value folds into the identity — the 2f-lec precondition:
      // state with differing reset values must never pair.
      if (auto init_d = gu::get_driver_of_sink_name(node, "initial"); !init_d.is_invalid() && gu::is_const_pin(init_d)) {
        c.kind = hcombine(c.kind, hstr(gu::hydrate_const(init_d).serialize()));
      }
    } else if (op == Ntype_op::Memory) {
      if (auto size_d = gu::get_driver_of_sink_name(node, "size"); !size_d.is_invalid() && gu::is_const_pin(size_d)) {
        c.kind = hcombine(c.kind, hstr(gu::hydrate_const(size_d).serialize()));
      }
    }
    // WIDTH is folded in LAST, and kept in a separate twin, because it is the
    // one part of the pair precondition that is not an identity: independently
    // imported implementations can describe the SAME state element with
    // different valid widths. Folding width into the identity refused such
    // pairs as a "kind/init mismatch" -- the flops then got free, independent
    // power-on symbols and the miter could refute at step 1 on the initial value.
    //
    // Relaxing it is sound because the miter already crosses widths: query.cpp
    // mints ONE shared symbol per cut at the MIN width of the two sides and
    // Encoder::seed_state fits it to each side's local width with
    // BITVECTOR_SIGN_EXTEND / BITVECTOR_ZERO_EXTEND according to that side's
    // signedness (encode.cpp fit_to). The extra high bits are therefore not
    // assumed equal -- they are derived, and if they are observable the miter
    // still refutes.
    //
    // MEMORY is the exception and keeps width in its identity: the shared state
    // is a cvc5 ARRAY sort built from the exact `size x bits` shape, and there
    // is no extension path for an array element. Two memories of different data
    // width are genuinely different state.
    c.kind_nw = c.kind;
    if (c.is_mem) {
      c.kind_nw = hcombine(c.kind_nw, static_cast<uint64_t>(static_cast<uint32_t>(node_out_bits(node))));
    }
    c.kind = hcombine(c.kind, static_cast<uint64_t>(static_cast<uint32_t>(node_out_bits(node))));
    ss.index.emplace(node.get_class_index(), static_cast<uint32_t>(ss.cells.size()));
    ss.cells.push_back(std::move(c));
  }

  // Immediate sequential neighbors: BFS through comb nodes only, so each cell
  // records the state cells / graph IOs one comb hop away in the state graph.
  for (auto& c : ss.cells) {
    auto op = gu::type_op_of(c.node);

    // Backward: data-pin fan-in.
    absl::flat_hash_set<hhds::Class_index> seen;
    std::vector<hhds::Pin_class>           work;
    for (const auto& e : c.node.inp_edges()) {
      if (data_sink_port(op, e.sink.get_port_id())) {
        work.push_back(e.driver);
      }
    }
    while (!work.empty()) {
      auto drv = work.back();
      work.pop_back();
      if (gu::is_graph_input_pin(drv)) {
        uint64_t t = hcombine(hstr("\x01in"), hstr(drv.get_pin_name()));
        if (want_labels) {
          ss.label.try_emplace(t, "in:" + std::string{drv.get_pin_name()});
        }
        c.in_anchors.push_back(t);
        continue;
      }
      if (gu::is_const_pin(drv)) {
        continue;
      }
      auto m = drv.get_master_node();
      if (auto it = ss.index.find(m.get_class_index()); it != ss.index.end()) {
        c.preds.push_back(it->second);
        continue;
      }
      if (seen.insert(m.get_class_index()).second) {
        for (const auto& e : m.inp_edges()) {
          work.push_back(e.driver);
        }
      }
    }

    // Forward: fan-out until the next state cell's DATA pin or a graph output.
    // An edge landing on a clock/reset pin of downstream state is ignored.
    seen.clear();
    std::vector<hhds::Pin_class> fwork;
    for (const auto& e : c.node.out_edges()) {
      fwork.push_back(e.sink);
    }
    while (!fwork.empty()) {
      auto snk = fwork.back();
      fwork.pop_back();
      if (gu::is_graph_output_pin(snk)) {
        uint64_t t = hcombine(hstr("\x01out"), hstr(snk.get_pin_name()));
        if (want_labels) {
          ss.label.try_emplace(t, "out:" + std::string{snk.get_pin_name()});
        }
        c.out_anchors.push_back(t);
        continue;
      }
      auto m = snk.get_master_node();
      if (auto it = ss.index.find(m.get_class_index()); it != ss.index.end()) {
        if (data_sink_port(gu::type_op_of(m), snk.get_port_id())) {
          c.succs.push_back(it->second);
        }
        continue;
      }
      if (seen.insert(m.get_class_index()).second) {
        for (const auto& e : m.out_edges()) {
          fwork.push_back(e.sink);
        }
      }
    }

    auto dedupe = [](auto& v) {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    dedupe(c.preds);
    dedupe(c.succs);
    dedupe(c.in_anchors);
    dedupe(c.out_anchors);
  }

  // Transitive closure over the state graph (SRP/ERP accumulate ALL resolved
  // points in the transitive cone, not just the frontier — traversal passes
  // through resolved and unresolved cells alike, so the closure is static and
  // computed once; per round only the member tokens change). BFS => the stored
  // distance is the MIN hop count.
  auto close = [&](std::vector<uint32_t> State_cell::* step, std::vector<std::pair<uint32_t, uint32_t>> State_cell::* out) {
    std::vector<uint8_t> mark(ss.cells.size());
    for (auto& c : ss.cells) {
      std::fill(mark.begin(), mark.end(), 0);
      auto& res = c.*out;
      for (uint32_t i : c.*step) {
        if (mark[i] == 0) {
          mark[i] = 1;
          res.emplace_back(i, 1);
        }
      }
      for (size_t head = 0; head < res.size(); ++head) {
        auto [i, d] = res[head];  // BFS: res doubles as the queue
        for (uint32_t j : ss.cells[i].*step) {
          if (mark[j] == 0) {
            mark[j] = 1;
            res.emplace_back(j, d + 1);
          }
        }
      }
      std::sort(res.begin(), res.end());
    }
  };
  close(&State_cell::preds, &State_cell::reach_b);
  close(&State_cell::succs, &State_cell::reach_f);
  return ss;
}

// One direction's resolved points as (base token, min hop dist) items, sorted
// and deduped: the cell's own IO anchors (dist 1), the closure members'
// resolved tokens (their hop dist), and the members' IO anchors (dist+1).
// Commutative by construction — this annotated set is the identity the
// full-match compares, and what the explain dump renders/diffs.
std::vector<std::pair<uint64_t, uint32_t>> rp_items(const State_side& ss, const State_cell& c, bool backward) {
  const auto&                                reach = backward ? c.reach_b : c.reach_f;
  const auto&                                own   = backward ? c.in_anchors : c.out_anchors;
  std::vector<std::pair<uint64_t, uint32_t>> items;
  items.reserve(own.size() + reach.size() * 2);
  for (uint64_t t : own) {
    items.emplace_back(t, 1);
  }
  for (auto [i, d] : reach) {
    const auto& m = ss.cells[i];
    if (m.token != 0) {
      items.emplace_back(m.token, d);
    }
    for (uint64_t t : backward ? m.in_anchors : m.out_anchors) {
      items.emplace_back(t, d + 1);
    }
  }
  std::sort(items.begin(), items.end());
  items.erase(std::unique(items.begin(), items.end()), items.end());
  return items;
}

uint64_t rp_signature(const State_side& ss, const State_cell& c, bool backward) {
  uint64_t h = hstr("\x01rp");
  for (auto [t, d] : rp_items(ss, c, backward)) {
    h = hcombine(h, hcombine(t, d));
  }
  return h;
}

// Tier-1 (name) + tier-2 (full-match) state pairing. Fills stats + the exported
// pair/unpaired lists, assigns resolved tokens, and reports per-cell outcomes
// under dump_state.
void pair_state(hhds::Graph* ga, hhds::Graph* gb, State_side& sa, State_side& sb, const Semdiff_options& opts, Match_result& res) {
  State_stats& st            = res.state;
  auto         logical_total = [](const State_side& ss) {
    absl::flat_hash_set<std::string> groups;
    for (const auto& c : ss.cells) {
      groups.insert(c.aggregate_key.empty() ? std::format("physical:{}", c.node.get_debug_nid())
                                            : std::format("aggregate:{}", c.aggregate_key));
    }
    return static_cast<uint32_t>(groups.size());
  };
  st.a_total = logical_total(sa);
  st.b_total = logical_total(sb);
  for (const auto& c : sa.cells) {
    st.a_mems += c.is_mem ? 1 : 0;
  }
  for (const auto& c : sb.cells) {
    st.b_mems += c.is_mem ? 1 : 0;
  }

  // name_noise experiment (NL2NL-style): destroy a deterministic pseudo-random
  // fraction of IMPL-side keys — selected by key hash, so a colliding key group
  // noises as a unit and the selection is reproducible per seed. The original
  // key is kept as ground truth, so tier-2 recovery is scored for CORRECTNESS,
  // not just coverage.
  if (opts.name_noise > 0.0) {
    const auto threshold = static_cast<uint64_t>(opts.name_noise * 1e6);
    for (auto& c : sb.cells) {
      if (mix64(hstr(c.key) ^ mix64(opts.noise_seed)) % 1000000 < threshold) {
        c.truth  = c.key;
        c.key   += "\x01!noise";
        ++st.noised;
      }
    }
  }

  // Tier-1: state_key across the sides. 1:1 => a certain pair; a colliding key
  // present on both sides => one symmetric group (seeded alike, exactly the
  // matching_names contract); a one-sided key stays unresolved.
  absl::flat_hash_map<std::string, std::vector<uint32_t>> akeys, bkeys;
  for (uint32_t i = 0; i < sa.cells.size(); ++i) {
    akeys[sa.cells[i].key].push_back(i);
  }
  for (uint32_t i = 0; i < sb.cells.size(); ++i) {
    bkeys[sb.cells[i].key].push_back(i);
  }
  if (opts.matching_names) {
    auto valid_aggregate_group = [](const State_side& ss, const std::vector<uint32_t>& group) {
      if (group.size() <= 1) {
        return true;  // the opposite side may be the unexpanded aggregate
      }
      absl::flat_hash_set<int32_t>             ordinals;
      absl::flat_hash_map<int32_t, int32_t>    ordinal_to_source;
      std::vector<std::pair<int32_t, int32_t>> packed_ranges;
      int32_t                                  declared_extent = 0;
      for (uint32_t i : group) {
        const auto& c = ss.cells[i];
        if (c.aggregate_key.empty()) {
          return false;
        }
        auto extent = c.node.attr(livehd::attrs::aggregate_extent);
        auto lane   = c.node.attr(livehd::attrs::aggregate_lane_ordinal);
        auto source = c.node.attr(livehd::attrs::aggregate_source_index);
        auto offset = c.node.attr(livehd::attrs::aggregate_bit_offset);
        auto width  = c.node.attr(livehd::attrs::aggregate_bit_width);
        if (!extent.has() || !lane.has() || !source.has() || !offset.has() || !width.has() || extent.get() <= 0 || lane.get() < 0
            || lane.get() >= extent.get() || offset.get() < 0 || width.get() <= 0
            || offset.get() > std::numeric_limits<int32_t>::max() - width.get()) {
          return false;
        }
        if (declared_extent == 0) {
          declared_extent = extent.get();
        } else if (declared_extent != extent.get()) {
          return false;
        }
        ordinals.insert(lane.get());
        auto [source_it, inserted] = ordinal_to_source.try_emplace(lane.get(), source.get());
        if (!inserted && source_it->second != source.get()) {
          return false;
        }
        packed_ranges.emplace_back(offset.get(), offset.get() + width.get());
      }
      // An array-of-struct has several physical field leaves per lane, so the
      // group may be larger than its source array extent. The complete set of
      // distinct lane ordinals must still cover exactly all declared entries.
      // The absolute packed ranges must also reconstruct one gap-free,
      // non-overlapping aggregate; otherwise name equality alone could bless
      // incomplete or duplicated provenance.
      if (ordinals.size() != static_cast<size_t>(declared_extent)) {
        return false;
      }
      std::sort(packed_ranges.begin(), packed_ranges.end());
      int32_t cursor = 0;
      for (const auto& [lo, hi] : packed_ranges) {
        if (lo != cursor) {
          return false;
        }
        cursor = hi;
      }
      return cursor > 0;
    };
    for (auto& [key, av] : akeys) {
      auto it = bkeys.find(key);
      if (it == bkeys.end()) {
        continue;
      }
      uint64_t tok = hcombine(hstr("\x01state"), hstr(key));
      if (opts.explain_noise > 0) {
        sa.label.try_emplace(tok, "st:" + key);
        sb.label.try_emplace(tok, "st:" + key);
      }
      const bool one = av.size() == 1 && it->second.size() == 1;
      const bool aggregate_pair
          = !one && valid_aggregate_group(sa, av) && valid_aggregate_group(sb, it->second)
            && (std::any_of(av.begin(), av.end(), [&](uint32_t i) { return !sa.cells[i].aggregate_key.empty(); })
                || std::any_of(it->second.begin(), it->second.end(), [&](uint32_t i) {
                     return !sb.cells[i].aggregate_key.empty();
                   }));
      if (!one && !aggregate_pair) {
        // A name that collides within one side and is NOT a reaggregatable SROA
        // group gets no tier-1 token at all now (tier-2 decides it). Keep the
        // census `lhd pass semdiff` prints -- "key on both sides but colliding
        // within one" is still exactly what happened -- instead of letting
        // a_name_grouped/b_name_grouped read as zero for every collision.
        st.a_name_grouped += static_cast<uint32_t>(av.size());
        st.b_name_grouped += static_cast<uint32_t>(it->second.size());
        continue;
      }
      for (uint32_t i : av) {
        sa.cells[i].token    = tok;
        sa.cells[i].t1_pair  = true;
        sa.cells[i].t1_group = false;
      }
      for (uint32_t i : it->second) {
        sb.cells[i].token    = tok;
        sb.cells[i].t1_pair  = true;
        sb.cells[i].t1_group = false;
      }
      ++st.name_pairs;
      st.name_pairs_mem += sa.cells[av.front()].is_mem ? 1 : 0;
    }
  }

  // Caller-supplied certain pairs (lec.match): resolve each side by hier name
  // (the lec keying basis) or by the tier-1 state_key spelling, and give the
  // pair a shared resolved token — an anchor exactly like a name match. Names
  // that don't resolve 1:1 or land on an already-resolved cell are skipped
  // (the consumer owns the validity of its explicit pairs, and lec re-verifies
  // every downstream tier-2 pair anyway).
  if (!opts.seed_pairs.empty()) {
    constexpr uint32_t kDup         = UINT32_MAX;
    auto               build_lookup = [](State_side& ss) {
      absl::flat_hash_map<std::string, uint32_t> m;
      auto                                       add = [&](std::string n, uint32_t i) {
        if (n.empty()) {
          return;
        }
        auto [it, ins] = m.try_emplace(std::move(n), i);
        if (!ins && it->second != i) {
          it->second = kDup;
        }
      };
      for (uint32_t i = 0; i < ss.cells.size(); ++i) {
        add(normalize_reg_name(ss.cells[i].node.get_hier_name()), i);
        if (const auto& k = ss.cells[i].key; k.starts_with("n:")) {
          add(k.substr(2), i);
        }
      }
      return m;
    };
    auto la = build_lookup(sa);
    auto lb = build_lookup(sb);
    for (const auto& [an, bn] : opts.seed_pairs) {
      auto ia = la.find(normalize_reg_name(an));
      auto ib = lb.find(normalize_reg_name(bn));
      if (ia == la.end() || ib == lb.end() || ia->second == kDup || ib->second == kDup) {
        continue;
      }
      auto& ca = sa.cells[ia->second];
      auto& cb = sb.cells[ib->second];
      if (ca.token != 0 || cb.token != 0) {
        continue;  // already resolved by tier-1
      }
      uint64_t tok = hcombine(hstr("\x01seed"), hstr(normalize_reg_name(an)));
      ca.token     = tok;
      ca.t1_pair   = true;
      cb.token     = tok;
      cb.t1_pair   = true;
      ++st.seed_pairs;
    }
  }

  // Tier-2 fixed point: bucket the still-unresolved cells by (kind, SRP, ERP);
  // a 1:1 bucket across the sides pairs, the pair becomes a resolved point, and
  // the sharpened signatures go around again — until a round adds nothing or
  // synalign_maxiter caps it. Tier-1-complete common path: no unresolved cell
  // on a side means nothing can pair — the signature pass never runs
  // (2f-lec acceptance: all-names-match designs do zero tier-2 work).
  auto has_unresolved = [](const State_side& ss) {
    for (const auto& c : ss.cells) {
      if (c.token == 0) {
        return true;
      }
    }
    return false;
  };
  if (opts.state_pairing && has_unresolved(sa) && has_unresolved(sb)) {
    const uint32_t maxiter = std::max<uint32_t>(1, opts.synalign_maxiter);
    // TWO PHASES. The strict phase keeps the declared width in the identity, so
    // when both a same-width and a different-width candidate exist the
    // same-width one is taken first and nothing that pairs today changes. Only
    // the residue that STILL has no counterpart is then retried width-blind
    // (see collect_state for why that is sound). Doing it the other way round
    // would let a width-crossing pair win a slot from an exact one.
    bool           relaxed = false;
    for (uint32_t round = 1; round <= maxiter; ++round) {
      absl::flat_hash_map<uint64_t, std::vector<uint32_t>> asig, bsig;
      auto                                                 sig_of = [&](const State_side& ss, const State_cell& c) {
        uint64_t h = hcombine(relaxed ? c.kind_nw : c.kind, rp_signature(ss, c, /*backward=*/true));
        return hcombine(h, rp_signature(ss, c, /*backward=*/false));
      };
      for (uint32_t i = 0; i < sa.cells.size(); ++i) {
        if (sa.cells[i].token == 0) {
          asig[sig_of(sa, sa.cells[i])].push_back(i);
        }
      }
      for (uint32_t i = 0; i < sb.cells.size(); ++i) {
        if (sb.cells[i].token == 0) {
          bsig[sig_of(sb, sb.cells[i])].push_back(i);
        }
      }
      bool progress = false;
      for (auto& [sig, av] : asig) {
        auto it = bsig.find(sig);
        if (it == bsig.end()) {
          continue;
        }
        if (av.size() == 1 && it->second.size() == 1) {
          uint64_t tok = hcombine(hstr("\x02pair"), sig);
          if (opts.explain_noise > 0) {
            sa.label.try_emplace(tok, "pair:" + sa.cells[av.front()].key);
            sb.label.try_emplace(tok, "pair:" + sa.cells[av.front()].key);
          }
          sa.cells[av.front()].token           = tok;
          sa.cells[av.front()].t2_pair         = true;
          sb.cells[it->second.front()].token   = tok;
          sb.cells[it->second.front()].t2_pair = true;
          ++st.full_pairs;
          st.full_pairs_mem += sa.cells[av.front()].is_mem ? 1 : 0;
          // Export the concrete pair for the LEC consumer: raw hier names (the
          // node name attr lec keys flops by — NOT the driver-pin-based
          // state_key, which a reader may stamp differently).
          res.state_pairs.push_back(State_pair{sa.cells[av.front()].node.get_hier_name(),
                                               sb.cells[it->second.front()].node.get_hier_name(),
                                               sa.cells[av.front()].is_mem,
                                               round});
          progress = true;
          if (const auto& bt = sb.cells[it->second.front()].truth; !bt.empty()) {
            ++st.noised_recovered;
            st.noised_correct += bt == sa.cells[av.front()].key ? 1 : 0;
          }
          if (opts.dump_state) {
            std::print("semdiff[state]: full-match '{}' ({}) <-> '{}' ({}) round {}\n",
                       sa.cells[av.front()].key,
                       ga->get_name(),
                       sb.cells[it->second.front()].key,
                       gb->get_name(),
                       round);
          }
        }
      }
      if (progress) {
        st.rounds = round;
      }
      if (!progress && !relaxed && round < maxiter) {
        // The strict phase converged. Anything still unresolved could not find a
        // same-width counterpart; retry it width-blind before declaring it
        // unpaired. This is the phase switch, not a terminal round.
        relaxed = true;
        continue;
      }
      if (!progress || round == maxiter) {
        // Terminal round (converged, or capped by synalign_maxiter): cells
        // stuck in a both-sided but non-1:1 bucket are the AMBIGUOUS residue
        // (matcher headroom), distinct from no-counterpart. Cells that paired
        // THIS round carry a token and are excluded downstream.
        for (auto& [sig, av] : asig) {
          auto it = bsig.find(sig);
          if (it == bsig.end()) {
            continue;
          }
          if (av.size() != 1 || it->second.size() != 1) {
            for (uint32_t i : av) {
              sa.cells[i].ambiguous = true;
            }
            for (uint32_t i : it->second) {
              sb.cells[i].ambiguous = true;
            }
          }
        }

        // Kind-clash diagnostic: an unresolved cell whose SRP/ERP exist on the
        // other side only under a DIFFERENT kind (op/bits/init fold) was
        // refused by the pair precondition, not by structure — reported as
        // "kind/init mismatch" instead of "no full match" (a renamed flop
        // whose reset value was also edited is this class, and the report is
        // the user's cue to check the reset).
        // Structure-only probe: what would have matched if the pair
        // precondition were dropped entirely. With width no longer in the
        // identity for a flop, a `kind_clash` now means the op, the commit edge
        // or the RESET VALUE genuinely differ -- which is what the report tells
        // the user to go check.
        auto nokind_sig = [&](const State_side& ss, const State_cell& c) {
          return hcombine(rp_signature(ss, c, /*backward=*/true), rp_signature(ss, c, /*backward=*/false));
        };
        absl::flat_hash_set<uint64_t> a_nk, b_nk;
        for (const auto& c : sa.cells) {
          if (c.token == 0) {
            a_nk.insert(nokind_sig(sa, c));
          }
        }
        for (const auto& c : sb.cells) {
          if (c.token == 0) {
            b_nk.insert(nokind_sig(sb, c));
          }
        }
        for (auto& c : sa.cells) {
          c.kind_clash = c.token == 0 && !c.ambiguous && b_nk.contains(nokind_sig(sa, c));
        }
        for (auto& c : sb.cells) {
          c.kind_clash = c.token == 0 && !c.ambiguous && a_nk.contains(nokind_sig(sb, c));
        }

        // explain_noise: deep-dump noised-but-unrecovered impl cells against
        // their ground-truth ref twin — the mechanical "why no full match":
        // equal signatures => ambiguity (list the bucket), differing => show
        // exactly which (anchor, dist) items diverge, kind mismatch called out.
        if (opts.explain_noise > st.explained) {
          auto lbl = [](const State_side& ss, uint64_t base) {
            auto it2 = ss.label.find(base);
            return it2 == ss.label.end() ? std::format("tok:{:x}", base) : it2->second;
          };
          auto items_str = [&](const State_side& ss, const std::vector<std::pair<uint64_t, uint32_t>>& items) {
            std::string s;
            size_t      shown = 0;
            for (auto [t, d] : items) {
              if (shown++ == 24) {
                s += std::format(" …(+{} more)", items.size() - 24);
                break;
              }
              s += std::format(" ({},{})", lbl(ss, t), d);
            }
            return s;
          };
          auto print_diff = [&](std::string_view what, const State_cell& cr, const State_cell& ci, bool backward) {
            auto                                       ri = rp_items(sa, cr, backward);
            auto                                       ii = rp_items(sb, ci, backward);
            std::vector<std::pair<uint64_t, uint32_t>> only_r, only_i;
            std::set_difference(ri.begin(), ri.end(), ii.begin(), ii.end(), std::back_inserter(only_r));
            std::set_difference(ii.begin(), ii.end(), ri.begin(), ri.end(), std::back_inserter(only_i));
            if (only_r.empty() && only_i.empty()) {
              std::print("    {} sets EQUAL ({} items)\n", what, ri.size());
              return;
            }
            std::print("    {} diff: only-ref{} | only-impl{}\n", what, items_str(sa, only_r), items_str(sb, only_i));
          };
          for (uint32_t j = 0; j < sb.cells.size() && opts.explain_noise > st.explained; ++j) {
            auto& ci = sb.cells[j];
            if (ci.truth.empty() || ci.token != 0) {
              continue;
            }
            ++st.explained;
            uint64_t isig = sig_of(sb, ci);
            size_t   ibkt = bsig.contains(isig) ? bsig[isig].size() : 0;
            size_t   abkt = asig.contains(isig) ? asig[isig].size() : 0;
            std::print(
                "semdiff[explain] def '{}': impl '{}' ({} bits {}) noised, UNRECOVERED — same-sig candidates ref/impl {}/{}\n",
                gb->get_name(),
                ci.truth,
                Ntype::get_name(gu::type_op_of(ci.node)),
                node_out_bits(ci.node),
                abkt,
                ibkt);
            std::print("    impl SRP:{}\n", items_str(sb, rp_items(sb, ci, true)));
            std::print("    impl ERP:{}\n", items_str(sb, rp_items(sb, ci, false)));
            auto tw = akeys.find(ci.truth);
            if (tw == akeys.end() || tw->second.size() != 1) {
              std::print("    ref ground-truth twin '{}': not present 1:1 on ref side ({} candidates) — defs differ\n",
                         ci.truth,
                         tw == akeys.end() ? 0 : tw->second.size());
              continue;
            }
            const auto& cr = sa.cells[tw->second.front()];
            if (cr.token != 0) {
              std::print("    ref twin resolved elsewhere as {} — impl cell lost it (twin paired before this cell disambiguated)\n",
                         lbl(sa, cr.token));
              continue;
            }
            if (cr.kind != ci.kind) {
              std::print("    KIND differs (op/bits/init fold) — pair precondition refuses regardless of structure\n");
            }
            if (sig_of(sa, cr) == isig) {
              std::string mem_r, mem_i;
              size_t      shown = 0;
              for (uint32_t i2 : asig[isig]) {
                if (shown++ == 8) {
                  mem_r += " …";
                  break;
                }
                mem_r += " '" + sa.cells[i2].key + "'";
              }
              shown = 0;
              for (uint32_t j2 : bsig[isig]) {
                if (shown++ == 8) {
                  mem_i += " …";
                  break;
                }
                mem_i += " '" + (sb.cells[j2].truth.empty() ? sb.cells[j2].key : sb.cells[j2].truth) + "'";
              }
              std::print("    AMBIGUOUS: twin signature EQUAL — ref bucket:{} | impl bucket:{}\n", mem_r, mem_i);
            } else {
              std::print("    ref  SRP:{}\n", items_str(sa, rp_items(sa, cr, true)));
              std::print("    ref  ERP:{}\n", items_str(sa, rp_items(sa, cr, false)));
              print_diff("SRP", cr, ci, true);
              print_diff("ERP", cr, ci, false);
            }
          }
        }
        break;
      }
    }
  }

  auto finish = [&](hhds::Graph*              g,
                    State_side&               ss,
                    uint32_t&                 unpaired,
                    uint32_t&                 ambiguous,
                    std::vector<std::string>& names,
                    std::vector<std::string>& mem_diverged,
                    std::string_view          side) {
    absl::flat_hash_set<std::string> unresolved_groups;
    for (auto& c : ss.cells) {
      if (c.token == 0) {
        const auto group = c.aggregate_key.empty() ? std::format("physical:{}", c.node.get_debug_nid())
                                                   : std::format("aggregate:{}", c.aggregate_key);
        if (unresolved_groups.insert(group).second) {
          ++unpaired;
          ambiguous += c.ambiguous ? 1 : 0;
        }
        // A memory with a GENUINE mismatch (kind/init clash or no counterpart —
        // not mere symmetric ambiguity, which is occurrence-safe) must not be
        // force-collapsed by lec: its correspondence diverges. See 2f-lec guard.
        if (c.is_mem && !c.ambiguous) {
          mem_diverged.push_back(std::string(c.node.get_hier_name()));
        }
        if (opts.state_pairing) {
          names.push_back(c.node.get_hier_name()
                          + (c.ambiguous    ? " (ambiguous)"
                             : c.kind_clash ? " (kind/init mismatch)"
                                            : " (no full match)"));
        }
      }
      if (opts.dump_state) {
        std::print("semdiff[state]: {} '{}' {}{} {}\n",
                   side,
                   g->get_name(),
                   c.truth.empty() ? c.key : c.truth,
                   c.truth.empty() ? "" : " (noised)",
                   c.t1_pair     ? "name"
                   : c.t1_group  ? "name-group"
                   : c.t2_pair   ? "full"
                   : c.ambiguous ? "UNPAIRED(ambiguous)"
                                 : "UNPAIRED(no-counterpart)");
      }
    }
  };
  finish(ga, sa, st.a_unpaired, st.a_ambiguous, res.a_state_unpaired, res.a_mem_diverged, "ref");
  finish(gb, sb, st.b_unpaired, st.b_ambiguous, res.b_state_unpaired, res.b_mem_diverged, "impl");
}

// Per-side analysis: forward/backward signatures + node order.
struct Side {
  hhds::Graph*                                     g = nullptr;
  std::vector<hhds::Node_class>                    order;  // forward_class order
  // Identity Get_mask nodes are representation-only boundaries. They are not
  // members of the structural node bijection; signatures walk through them.
  std::vector<hhds::Node_class>                    transparent;
  absl::flat_hash_map<hhds::Class_index, uint64_t> fsig;   // forward signature
  absl::flat_hash_map<hhds::Class_index, uint64_t> bsig;   // backward signature
  absl::flat_hash_set<uint64_t>                    fvals;  // fsig value set
  absl::flat_hash_set<uint64_t>                    bvals;  // bsig value set
};

// A Get_mask is transparent only when its CONSTANT mask selects exactly every
// bit of the value pin's DECLARED width, in order, and the result declares the
// same width. This is deliberately based on pin metadata plus the literal mask
// -- never on inferred ranges. A widening/narrowing result or wider, narrower,
// shifted, sparse, unknown, or dynamic mask is real logic and must remain in
// the correspondence graph.
std::optional<hhds::Pin_class> identity_get_mask_input(const hhds::Node_class& node) {
  if (gu::type_op_of(node) != Ntype_op::Get_mask) {
    return std::nullopt;
  }
  const auto      a_pid = Ntype::get_sink_pid(Ntype_op::Get_mask, "a");
  const auto      m_pid = Ntype::get_sink_pid(Ntype_op::Get_mask, "mask");
  hhds::Pin_class a;
  hhds::Pin_class mask;
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == a_pid) {
      if (!a.is_invalid()) {
        return std::nullopt;
      }
      a = e.driver;
    } else if (e.sink.get_port_id() == m_pid) {
      if (!mask.is_invalid()) {
        return std::nullopt;
      }
      mask = e.driver;
    }
  }
  const int bits     = gu::bits_of(a);
  const int out_bits = node_out_bits(node);
  if (a.is_invalid() || mask.is_invalid() || bits <= 0 || out_bits != bits || !gu::is_const_pin(mask)) {
    return std::nullopt;
  }
  const auto value = gu::hydrate_const(mask);
  if (value.has_unknowns()) {
    return std::nullopt;
  }
  // -1 is the explicit all-source-bits sentinel used by Get_mask.
  if (value.is_just_i64() && value.to_just_i64() == -1) {
    return a;
  }
  if (value.is_negative()) {
    return std::nullopt;
  }
  const auto [first, end] = value.get_mask_range();
  if (first != 0 || end != bits) {
    return std::nullopt;
  }
  return a;
}

hhds::Pin_class skip_identity_get_masks(hhds::Pin_class pin) {
  for (int hops = 0; hops < 64 && !pin.is_invalid() && !gu::is_graph_input_pin(pin) && !gu::is_const_pin(pin); ++hops) {
    auto input = identity_get_mask_input(pin.get_master_node());
    if (!input) {
      break;
    }
    pin = *input;
  }
  return pin;
}

// Follow an outgoing edge through any identity Get_mask value inputs. The
// returned sinks are the real structural consumers used by the backward
// signature. A small hop cap is fail-closed: on malformed/cyclic wrapper
// topology the unresolved Get_mask sink remains and the match declines.
void collect_structural_sinks(const hhds::Pin_class& sink, std::vector<hhds::Pin_class>& out, int hops = 0) {
  if (sink.is_invalid() || gu::is_graph_output_pin(sink) || hops >= 64) {
    out.push_back(sink);
    return;
  }
  auto node  = sink.get_master_node();
  auto input = identity_get_mask_input(node);
  if (!input || sink.get_port_id() != Ntype::get_sink_pid(Ntype_op::Get_mask, "a")) {
    out.push_back(sink);
    return;
  }
  bool any = false;
  for (const auto& e : node.out_edges()) {
    any = true;
    collect_structural_sinks(e.sink, out, hops + 1);
  }
  if (!any) {
    out.push_back(sink);
  }
}

// The forward pass's operand rule, factored out so the compare-point obligation
// below folds operands EXACTLY the way the signatures did. Returns false when the
// driver has no forward signature yet (i.e. it is past a frontier).
//
// The const case is not optional: a pid-encoded const (e.g. a get_mask's mask)
// has no CONST_NODE in forward_class, so an obligation that only consulted fsig
// would miss a changed constant entirely.
bool resolve_driver(const Side& s, const hhds::Pin_class& drv, uint64_t& out) {
  auto structural_drv = skip_identity_get_masks(drv);
  if (gu::is_graph_input_pin(structural_drv)) {
    out = hcombine(hstr("\x01in"), hstr(structural_drv.get_pin_name()));
    return true;
  }
  if (gu::is_const_pin(structural_drv)) {
    // A constant operand (incl. the pid-encoded const that drives e.g. a
    // get_mask's mask) is anchored by value — its CONST_NODE is not a
    // forward_class node, so resolve it here or forward would stall and the
    // node would fall back to a coarser backward (symmetric) match.
    out = hcombine(hstr("\x01const"), hstr(gu::hydrate_const(structural_drv).serialize()));
    return true;
  }
  auto it = s.fsig.find(structural_drv.get_master_node().get_class_index());
  if (it == s.fsig.end()) {
    return false;
  }
  out = hcombine(it->second, static_cast<uint64_t>(static_cast<uint32_t>(structural_drv.get_port_id())));
  return true;
}

// Fold a compare point's inputs EXACTLY as the forward pass folds a node's, so a
// rewiring between compare points shows up as a differing csig. false = an operand
// had no forward signature, so the obligation is UNDECIDABLE (never "discharged").
bool cut_signature(const Side& s, const hhds::Node_class& node, uint64_t& out) {
  absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
  for (const auto& e : node.inp_edges()) {
    uint64_t dsig = 0;
    if (!resolve_driver(s, e.driver, dsig)) {
      return false;
    }
    by_port[e.sink.get_port_id()].push_back(dsig);
  }
  out = fold_operands(node_kind_key(node), by_port);
  return true;
}

Side analyze(hhds::Graph* g, const Semdiff_options& opts,
             const absl::flat_hash_map<hhds::Class_index, uint64_t>* state_seeds = nullptr) {
  Side s;
  s.g = g;

  // Seed state cells (cut points). With matching_names they get a cross-side
  // identity by hierarchical name so structure flows through them in BOTH
  // directions; without it they stay frontiers (a renamed flop => a gap).
  // structural_match may pass explicit seeds instead (state_pairing: tier-2
  // full-match pairs override/extend the name seeds).
  if (state_seeds != nullptr) {
    for (const auto& [ci, seed] : *state_seeds) {
      s.fsig[ci] = seed;
      s.bsig[ci] = seed;
    }
  } else if (opts.matching_names) {
    for (auto node : g->body().nodes(hhds::Node_order::forward)) {
      if (is_state(gu::type_op_of(node))) {
        uint64_t seed                  = hcombine(hstr("\x01state"), hstr(state_key(g, node)));
        s.fsig[node.get_class_index()] = seed;
        s.bsig[node.get_class_index()] = seed;
      }
    }
  }

  // ---- Forward pass: inputs/consts -> outputs (topological). A node is ready
  // when every fanin signal already has a forward signature.
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (identity_get_mask_input(node)) {
      s.transparent.push_back(node);
      continue;
    }
    s.order.push_back(node);
    auto ci = node.get_class_index();
    auto op = gu::type_op_of(node);

    if (s.fsig.contains(ci)) {  // pre-seeded state cell
      s.fvals.insert(s.fsig[ci]);
      continue;
    }
    if (op == Ntype_op::Nconst) {
      uint64_t h = hcombine(hstr("\x01const"), hstr(gu::const_value_of(node)));
      h          = hcombine(h, node_kind_key(node));
      s.fsig[ci] = h;
      s.fvals.insert(h);
      continue;
    }
    if (is_state(op)) {
      continue;  // unseeded state cell => forward frontier
    }

    bool                                            ready = true;
    absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
    for (const auto& e : node.inp_edges()) {
      uint64_t dsig = 0;
      if (!resolve_driver(s, e.driver, dsig)) {
        ready = false;
        break;
      }
      by_port[e.sink.get_port_id()].push_back(dsig);
    }
    if (!ready) {
      continue;  // past a frontier => no forward signature
    }
    uint64_t h = fold_operands(node_kind_key(node), by_port);
    s.fsig[ci] = h;
    s.fvals.insert(h);
  }

  // ---- Forward fixpoint. forward_class emits in the WORD-LEVEL graph's order,
  // which can be cyclic even when the region is acyclic once cut points (seeded
  // Subs / state cells) act as sources -- e.g. a packed struct read+written by
  // disjoint Get_mask/Set_mask ranges is a word-level self-loop but a bit-level
  // DAG. In that order a node may be visited before its fanin was signed and left
  // a frontier, spuriously stranding its (and its cone's) obligations as
  // cut_unknown. Re-sweep until no new signature appears: an order-only frontier
  // resolves once its inputs are ready; a node in a GENUINE combinational loop
  // never does and correctly stays a frontier. The result is order-invariant
  // (each fsig is a pure fold of its inputs' fsigs), so both compare sides
  // converge identically. The cap MUST scale with node count: a chain visited in
  // reverse by forward_class resolves one node per sweep, so up to |order| sweeps
  // are needed; a fixed cap would stop two same-structure sides at DIFFERENT
  // partial states (different nid order) and desync their decidability. The
  // no-change break is the real terminator (monotone: sweeps only add fsigs); +8
  // is slack.
  for (size_t iter = 0, cap = s.order.size() + 8; iter < cap; ++iter) {
    bool changed = false;
    for (const auto& node : s.order) {
      auto ci = node.get_class_index();
      if (s.fsig.contains(ci)) {
        continue;
      }
      auto op = gu::type_op_of(node);
      if (op == Ntype_op::Nconst || is_state(op)) {
        continue;  // const already signed; unseeded state is a real frontier
      }
      bool                                            ready = true;
      absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
      for (const auto& e : node.inp_edges()) {
        uint64_t dsig = 0;
        if (!resolve_driver(s, e.driver, dsig)) {
          ready = false;
          break;
        }
        by_port[e.sink.get_port_id()].push_back(dsig);
      }
      if (!ready) {
        continue;
      }
      s.fsig[ci] = fold_operands(node_kind_key(node), by_port);
      s.fvals.insert(s.fsig[ci]);
      changed = true;
    }
    if (!changed) {
      break;
    }
  }

  // ---- Backward pass: outputs -> inputs (reverse topological). A node is ready
  // when every fanout signal already has a backward signature.
  for (auto it = s.order.rbegin(); it != s.order.rend(); ++it) {
    auto node = *it;
    auto ci   = node.get_class_index();
    auto op   = gu::type_op_of(node);

    if (s.bsig.contains(ci)) {  // pre-seeded state cell
      s.bvals.insert(s.bsig[ci]);
      continue;
    }
    if (is_state(op)) {
      continue;  // unseeded state cell => backward frontier
    }

    bool                                            ready = true;
    bool                                            any   = false;
    absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
    std::vector<hhds::Pin_class>                    structural_sinks;
    for (const auto& e : node.out_edges()) {
      collect_structural_sinks(e.sink, structural_sinks);
    }
    for (const auto& snk : structural_sinks) {
      any = true;
      uint64_t usig;
      if (gu::is_graph_output_pin(snk)) {
        usig = hcombine(hstr("\x01out"), hstr(snk.get_pin_name()));
      } else {
        auto it2 = s.bsig.find(snk.get_master_node().get_class_index());
        if (it2 == s.bsig.end()) {
          ready = false;
          break;
        }
        usig = hcombine(it2->second, static_cast<uint64_t>(static_cast<uint32_t>(snk.get_port_id())));
      }
      by_port[snk.get_port_id()].push_back(usig);
    }
    if (!ready || !any) {
      continue;  // a dead node or one past a frontier has no backward signature
    }
    uint64_t h = fold_operands(node_kind_key(node), by_port);
    s.bsig[ci] = h;
    s.bvals.insert(h);
  }

  return s;
}

// The match class of a node: the (tag, value) pair that keys its cross-side id,
// so corresponding nodes — and a structurally identical symmetric set — collapse
// onto one id.
//
// Forward is AUTHORITATIVE: when forward-from-inputs reached a node (it has a
// forward signature) it has decided the node's identity from its input cone. If
// that signature has a counterpart on the other side it is a forward match;
// otherwise the node is genuinely distinct and backward must NOT rescue it (a
// backward symmetric group would otherwise re-merge two inputs forward just told
// apart, hiding the difference). Backward only fills in nodes PAST the forward
// frontier — the ones forward never reached (no forward signature).
using Classkey = std::pair<char, uint64_t>;

std::optional<Classkey> class_of(const Side& side, hhds::Class_index ci, const absl::flat_hash_set<uint64_t>& fcommon,
                                 const absl::flat_hash_set<uint64_t>& bcommon) {
  if (auto it = side.fsig.find(ci); it != side.fsig.end()) {
    return fcommon.contains(it->second) ? std::optional<Classkey>{Classkey{'F', it->second}} : std::nullopt;
  }
  if (auto it = side.bsig.find(ci); it != side.bsig.end() && bcommon.contains(it->second)) {
    return Classkey{'B', it->second};
  }
  return std::nullopt;
}

// Stamp `id` on the node and each of its distinct driver (output) pins.
void stamp(const hhds::Node_class& node, uint32_t id) {
  gu::set_match(node, id);
  absl::flat_hash_set<int> seen;
  for (const auto& e : node.out_edges()) {
    if (seen.insert(e.driver.get_port_id()).second) {
      gu::set_match(e.driver, id);
    }
  }
}

// Shared front half of a structural compare: seed state cells, analyze both
// sides, and discharge the compare-point obligations. Fills `sa`, `sb`, and every
// `res` field the obligations and state pairing produce. The caller then either
// stamps + counts (structural_match) or checks the bijection cheaply
// (structural_identical) -- both run byte-identical analysis this way.
void build_sides(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts, Side& sa, Side& sb, Match_result& res) {
  if (opts.matching_names || opts.state_pairing) {
    // Tier-1 (name) + tier-2 (full-match) state pairing first; the resolved
    // tokens seed the structural analysis so structure flows through paired
    // state in both directions. One-sided-key cells keep the plain name seed
    // under matching_names (bit-for-bit the pre-state_pairing behavior).
    State_side ssa = collect_state(a, opts);
    State_side ssb = collect_state(b, opts);
    pair_state(a, b, ssa, ssb, opts, res);
    absl::flat_hash_map<hhds::Class_index, uint64_t> seeds_a, seeds_b;
    auto build_seeds = [&](State_side& ss, absl::flat_hash_map<hhds::Class_index, uint64_t>& seeds) {
      for (auto& c : ss.cells) {
        uint64_t s = c.token;
        if (s == 0 && opts.matching_names) {
          s = hcombine(hstr("\x01state"), hstr(c.key));
        }
        if (s != 0) {
          seeds.emplace(c.node.get_class_index(), s);
        }
      }
    };
    build_seeds(ssa, seeds_a);
    build_seeds(ssb, seeds_b);
    // Cut Subs get the same treatment as a Flop: a name seed so structure flows
    // through the hoisted instance in BOTH directions. They are seeded HERE rather
    // than routed through collect_state/pair_state on purpose — that path feeds the
    // tier-2 SRP/ERP signatures, State_stats (`--stats` would count instances as
    // "registers"), and the full_pairs/seed_pairs counters pass/lec keys its
    // no-solver skip on. node_out_bits is also edge-order dependent on a
    // multi-output Sub. Gated on matching_names: without names there is nothing to
    // seed a Sub with, and today's behavior is preserved bit-for-bit.
    if (opts.matching_names) {
      auto seed_cut_subs = [&](hhds::Graph* g, absl::flat_hash_map<hhds::Class_index, uint64_t>& seeds) {
        for (auto node : g->body().nodes(hhds::Node_order::forward)) {
          if (gu::type_op_of(node) != Ntype_op::Sub) {
            continue;
          }
          // loop_break Subs are always cut points; blackbox_subs makes EVERY Sub
          // one (incremental region reuse -- see is_cut).
          if (!opts.blackbox_subs && !node.is_loop_break()) {
            continue;
          }
          uint64_t seed = hcombine(hstr("\x01sub"), hstr(cut_point_key(g, node)));
          if (opts.blackbox_subs) {
            // The two blackbox Subs must share an IDENTICAL interface: same def
            // AND every port's name/id/width/sign. An interface change (a resized
            // or added/removed/renamed port) invalidates the parent's wiring to
            // the child, so it MUST break the seed here and miss -- blackboxing
            // hides the child's body, never its boundary.
            if (auto io = node.get_subnode_io()) {
              // Def identity by NAME, not get_subnode_gid(): a body-less pre-body
              // decl is assigned a CREATION-ORDER gid that drifts across recompiles
              // (it poisoned every downstream fold -> cut_violated cascade). The
              // name is content-stable. IO folded order-INDEPENDENTLY (XOR): decl
              // iteration order is not guaranteed stable and each port already
              // carries its own port_id, so the XOR is a faithful set signature.
              seed                = hcombine(seed, hstr(io->get_name()));
              uint64_t io_acc     = 0;
              auto     fold_decls = [&](const auto& decls, uint64_t tag) {
                for (const auto& p : decls) {
                  // Key each port by its declared NAME, never its port_id: a
                  // port_id is a node-id-like handle the cache round-trip
                  // (copy_from/save/load) can reassign, so the cached and fresh
                  // pre-bodies would disagree on an unchanged region. The name is
                  // content-stable and already unique, so it is the faithful (and
                  // round-trip-safe) port identity. XOR => order-independent.
                  uint64_t ph  = hcombine(tag, hstr(p.name));
                  ph           = hcombine(ph, static_cast<uint64_t>(p.bits));
                  ph           = hcombine(ph, p.unsign ? 1ULL : 2ULL);
                  io_acc      ^= ph;
                }
              };
              fold_decls(io->get_input_pin_decls(), hstr("\x01i"));
              fold_decls(io->get_output_pin_decls(), hstr("\x01o"));
              seed = hcombine(seed, io_acc);
            }
          }
          seeds.emplace(node.get_class_index(), seed);
        }
      };
      seed_cut_subs(a, seeds_a);
      seed_cut_subs(b, seeds_b);
    }
    sa = analyze(a, opts, &seeds_a);
    sb = analyze(b, opts, &seeds_b);
  } else {
    sa = analyze(a, opts);
    sb = analyze(b, opts);
  }

  // ---- Compare-point obligations ------------------------------------------
  // The node-set bijection below cannot see a rewiring BETWEEN compare points: a
  // cut point's fsig is its name seed and never folds its din, and class_of is
  // forward-authoritative so the bsig that WOULD differ is discarded. Fold each
  // compare point's inputs with the forward pass's own operand rule and compare
  // the two sides PAIRWISE (strictly stronger than fcommon set membership).
  {
    absl::flat_hash_map<std::string, uint64_t> ka, kb;  // compare point -> csig
    absl::flat_hash_set<std::string>           ua, ub;  // ... or "undecidable"
    auto collect_cuts = [&](const Side& s, absl::flat_hash_map<std::string, uint64_t>& k, absl::flat_hash_set<std::string>& u) {
      for (const auto& node : s.order) {
        if (!is_cut(node, opts.blackbox_subs)) {
          continue;
        }
        uint64_t csig = 0;
        auto     key  = cut_point_key(s.g, node);
        if (cut_signature(s, node, csig)) {
          k.emplace(key, csig);
        } else {
          u.insert(key);  // an operand had no fsig: NEVER dischargeable
        }
      }
      // Graph outputs are compare points too — a swapped output perturbs only the
      // (discarded) backward signature, so the node set alone cannot see it.
      auto onode = s.g->get_output_node();
      if (!onode.is_invalid()) {
        for (const auto& e : onode.inp_edges()) {
          auto     key  = "o:" + std::string(gu::pin_name_of(e.sink));
          uint64_t dsig = 0;
          if (resolve_driver(s, e.driver, dsig)) {
            k.emplace(key, dsig);
          } else {
            u.insert(key);
          }
        }
      }
    };
    collect_cuts(sa, ka, ua);
    collect_cuts(sb, kb, ub);

    for (const auto& [key, va] : ka) {
      if (ub.contains(key)) {
        ++res.cut_obligations;
        ++res.cut_unknown;
        continue;
      }
      auto it = kb.find(key);
      if (it == kb.end()) {
        continue;  // one-sided compare point: the node-set diff already reports it
      }
      ++res.cut_obligations;
      if (it->second == va) {
        ++res.cut_discharged;
      } else {
        ++res.cut_violated;
        res.cut_violations.push_back(key);
      }
    }
    for (const auto& key : ua) {
      if (kb.contains(key) || ub.contains(key)) {
        ++res.cut_obligations;
        ++res.cut_unknown;  // ref side undecidable but the point exists on both
      }
    }
  }
}

// Signatures present on BOTH sides are matchable.
void common_values(const Side& sa, const Side& sb, absl::flat_hash_set<uint64_t>& fcommon, absl::flat_hash_set<uint64_t>& bcommon) {
  for (uint64_t v : sa.fvals) {
    if (sb.fvals.contains(v)) {
      fcommon.insert(v);
    }
  }
  for (uint64_t v : sa.bvals) {
    if (sb.bvals.contains(v)) {
      bcommon.insert(v);
    }
  }
}

}  // namespace

bool is_persistent_state(const hhds::Node_class& node) {
  const auto op = gu::type_op_of(node);
  if (op != Ntype_op::Memory) {
    return op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch;
  }
  auto type = gu::get_driver_of_sink_name(node, "type");
  if (!type.is_invalid() && gu::is_const_pin(type)) {
    const auto value = gu::hydrate_const(type);
    if (value.is_just_i64() && value.to_just_i64() == 2) {
      return false;
    }
  }
  return true;
}

Match_result structural_match(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts) {
  Match_result res;
  if (a == nullptr || b == nullptr) {
    return res;
  }
  Side sa, sb;
  build_sides(a, b, opts, sa, sb, res);

  absl::flat_hash_set<uint64_t> fcommon, bcommon;
  common_values(sa, sb, fcommon, bcommon);

  // Assign ids in a's forward_class order (deterministic); b reuses the map, so
  // a class shared across the two graphs lands on the same id on both sides.
  absl::flat_hash_map<Classkey, uint32_t> class2id;
  uint32_t                                next_id = 1;
  auto                                    assign  = [&](Side& side, uint32_t& matched, uint32_t& unmatched) {
    for (const auto& node : side.order) {
      auto     ck = class_of(side, node.get_class_index(), fcommon, bcommon);
      uint32_t id = 0;
      if (ck) {
        auto [it, inserted] = class2id.try_emplace(*ck, next_id);
        if (inserted) {
          ++next_id;
        }
        id = it->second;
        ++matched;
      } else {
        ++unmatched;
      }
      stamp(node, id);
    }
  };
  assign(sa, res.a_matched, res.a_unmatched);
  assign(sb, res.b_matched, res.b_unmatched);
  // Surface a transparent wrapper as part of its matched consumer region when
  // possible. It is intentionally not counted as a matched node (there is no
  // node counterpart on the other side), but leaving a stale/zero mark in a
  // stamped diff would misleadingly present the cancelled boundary artifact as
  // unique logic.
  auto stamp_transparent = [](Side& side) {
    for (const auto& node : side.transparent) {
      uint32_t id = 0;
      for (const auto& edge : node.out_edges()) {
        std::vector<hhds::Pin_class> sinks;
        collect_structural_sinks(edge.sink, sinks);
        for (const auto& sink : sinks) {
          if (sink.is_invalid() || gu::is_graph_output_pin(sink)) {
            continue;
          }
          id = gu::match_of(sink.get_master_node());
          if (id != 0) {
            break;
          }
        }
        if (id != 0) {
          break;
        }
      }
      stamp(node, id);
    }
  };
  stamp_transparent(sa);
  stamp_transparent(sb);
  res.regions = next_id - 1;

  // id_granularity=region: union ids that are adjacent through a matched edge in
  // `a`, then renumber. Ids are shared across sides, so the remap applies to b
  // too — re-stamp both graphs by reading back the pair id.
  if (opts.id_granularity == "region" && res.regions > 0) {
    std::vector<uint32_t> uf(res.regions + 1);
    for (uint32_t i = 0; i <= res.regions; ++i) {
      uf[i] = i;
    }
    std::function<uint32_t(uint32_t)> find = [&](uint32_t x) {
      while (uf[x] != x) {
        uf[x] = uf[uf[x]];
        x     = uf[x];
      }
      return x;
    };
    auto uni = [&](uint32_t x, uint32_t y) {
      x = find(x);
      y = find(y);
      if (x != y) {
        uf[std::max(x, y)] = std::min(x, y);
      }
    };
    for (auto node : a->body().nodes(hhds::Node_order::forward)) {
      uint32_t u = gu::match_of(node);
      if (u == 0) {
        continue;
      }
      for (const auto& e : node.out_edges()) {
        if (gu::is_graph_output_pin(e.sink)) {
          continue;
        }
        uint32_t v = gu::match_of(e.sink.get_master_node());
        if (v != 0) {
          uni(u, v);
        }
      }
    }
    absl::flat_hash_map<uint32_t, uint32_t> root2region;
    uint32_t                                next_region = 1;
    auto                                    region_of   = [&](uint32_t id) -> uint32_t {
      uint32_t r          = find(id);
      auto [it, inserted] = root2region.try_emplace(r, next_region);
      if (inserted) {
        ++next_region;
      }
      return it->second;
    };
    for (hhds::Graph* g : {a, b}) {
      for (auto node : g->body().nodes(hhds::Node_order::forward)) {
        uint32_t id = gu::match_of(node);
        if (id != 0) {
          stamp(node, region_of(id));
        }
      }
    }
    res.regions = next_region - 1;
  }

  uint32_t total = res.a_matched + res.a_unmatched + res.b_matched + res.b_unmatched;
  res.similarity = total == 0 ? 1.0 : static_cast<double>(res.a_matched + res.b_matched) / static_cast<double>(total);

  if (opts.verbose) {
    std::print("semdiff: ref {}/{} matched, impl {}/{} matched, {} regions, similarity {:.3f}\n",
               res.a_matched,
               res.a_matched + res.a_unmatched,
               res.b_matched,
               res.b_matched + res.b_unmatched,
               res.regions,
               res.similarity);
  }
  return res;
}

bool structural_identical(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  Side         sa, sb;
  Match_result res;
  build_sides(a, b, opts, sa, sb, res);  // fills cut_* and the state pair counts

  absl::flat_hash_set<uint64_t> fcommon, bcommon;
  common_values(sa, sb, fcommon, bcommon);

  // Node-set bijection: every node on BOTH sides must have a cross-side class.
  // The first orphan proves a difference -- record it and stop (no stamping, no
  // stats). The final verdict routes through is_structural_identity so this path
  // and lec's skip share ONE predicate and can never drift.
  for (const Side* s : {&sa, &sb}) {
    for (const auto& node : s->order) {
      if (!class_of(*s, node.get_class_index(), fcommon, bcommon)) {
        res.a_unmatched = 1;  // at least one orphan; the predicate only needs != 0
        return is_structural_identity(res);
      }
    }
  }
  // Reaching here IS the bijection: every node on both sides found a cross-side
  // class. The shared predicate also requires the match to be non-empty (an empty
  // match proves nothing), so report the one stat the completed bijection
  // implies -- the fast path builds no other stats, and leaving these at 0 would
  // make every identical pair read as "empty".
  res.a_matched = static_cast<uint32_t>(sa.order.size());
  res.b_matched = static_cast<uint32_t>(sb.order.size());
  return is_structural_identity(res);
}

// ===========================================================================
// Exact parallel-traversal equivalence -- the verify-on-inconclusive fallback.
// ---------------------------------------------------------------------------
// structural_identical()'s forward signing STALLS on a genuine combinational
// loop (mux feedback): the loop cone never gets a forward signature, so its
// compare-point obligations stay `cut_unknown` and the region is conservatively
// refused. When the two pre-bodies are in fact identical (a comment-only edit)
// that refusal is a FALSE MISS. This proves equivalence without a signature: it
// builds a real node BIJECTION by walking a and b in lockstep from their named
// sources (graph inputs, cut-point outputs, constants), then VERIFIES that every
// edge maps consistently under it. A traversal bijection is exact -- unlike a
// hash it cannot false-positive -- and a visited set makes the cycle a non-issue.
//
// Cut model matches structural_identical: state cells are always cut points, and
// (blackbox_subs) every Sub is one too -- its outputs are seeded sources, its
// inputs are compare-point obligations, keyed on the Sub's IO wiring (name/width/
// sign, never node ids). So a child-body edit does not break the parent while a
// child-interface change does, exactly as the signature path.
//
// DISCOVERY (may pick a wrong pairing under symmetry -- that only costs a false
// MISS) then VERIFY (the sole soundness guarantee -- an exact edge check that
// admits a reuse only for a genuine isomorphism).
namespace {

// The a<->b node bijection carried through the traversal.
using Bimap = absl::flat_hash_map<hhds::Class_index, hhds::Class_index>;

// The a-space canonical id of a PAIRED driver's master node: on the a side the
// node's own class index, on the b side the a-node it pairs with (via ba). So a
// descriptor built from either side references the SAME id space and the two are
// directly comparable. Returns false when the driver's node is not (yet) paired
// -- checked SYMMETRICALLY (ab for a, ba for b), so an unpaired fanin folds a
// frontier on both sides rather than resolving on one and stranding the other.
bool traversal_pairid(const hhds::Node_class& dm, bool a_side, const Bimap& ab, const Bimap& ba, uint64_t& out) {
  if (a_side) {
    if (!ab.contains(dm.get_class_index())) {
      return false;
    }
    out = static_cast<uint64_t>(dm.get_class_index().value);
    return true;
  }
  auto it = ba.find(dm.get_class_index());
  if (it == ba.end()) {
    return false;
  }
  out = static_cast<uint64_t>(it->second.value);
  return true;
}

// A driver's cross-side descriptor, canonicalized to a-space: a graph input by
// NAME, a constant by VALUE, any other node output by (a-space pair id, driver
// port) -- port by NAME for a Sub (round-trip-stable), by port_id otherwise
// (Ntype-fixed). `resolved` is false when the driver's node has no pairing yet
// (a discovery frontier); verify treats that as a mismatch, discovery as "?".
uint64_t driver_desc(const hhds::Pin_class& drv, bool a_side, const Bimap& ab, const Bimap& ba, bool& resolved) {
  resolved = true;
  if (gu::is_graph_input_pin(drv)) {
    return hcombine(hstr("\x01I"), hstr(drv.get_pin_name()));
  }
  if (gu::is_const_pin(drv)) {
    return hcombine(hstr("\x01C"), hstr(gu::hydrate_const(drv).serialize()));
  }
  auto     dm = drv.get_master_node();
  uint64_t pid;
  if (!traversal_pairid(dm, a_side, ab, ba, pid)) {
    resolved = false;
    return 0;
  }
  uint64_t h = hcombine(hstr("\x01N"), pid);
  if (gu::type_op_of(dm) == Ntype_op::Sub) {
    h = hcombine(h, hstr(drv.get_pin_name()));  // named output port
  } else {
    h = hcombine(h, static_cast<uint64_t>(static_cast<uint32_t>(drv.get_port_id())));
  }
  return h;
}

// One input edge's descriptor: the sink port (by NAME on a Sub, else port_id)
// combined with the driver descriptor. `ok` is cleared when the driver is
// unresolved (verify: mismatch).
uint64_t edge_desc(const hhds::Edge_class& e, bool sink_is_sub, bool a_side, const Bimap& ab, const Bimap& ba, bool& ok) {
  uint64_t sink_port
      = sink_is_sub ? hstr(e.sink.get_pin_name()) : static_cast<uint64_t>(static_cast<uint32_t>(e.sink.get_port_id()));
  bool     resolved;
  uint64_t d = driver_desc(e.driver, a_side, ab, ba, resolved);
  ok         = resolved;
  return hcombine(sink_port, d);
}

// The multiset of a node's input-edge descriptors (sorted), a-space canonical.
// `ok` is cleared if any driver is unresolved.
std::vector<uint64_t> input_descs(const hhds::Node_class& node, bool a_side, const Bimap& ab, const Bimap& ba, bool& ok) {
  ok                             = true;
  bool                  sink_sub = gu::type_op_of(node) == Ntype_op::Sub;
  std::vector<uint64_t> v;
  for (const auto& e : node.inp_edges()) {
    bool eok;
    v.push_back(edge_desc(e, sink_sub, a_side, ab, ba, eok));
    ok = ok && eok;
  }
  std::sort(v.begin(), v.end());
  return v;
}

}  // namespace

namespace {

// Two independent chains keep the virtual-expression comparison on the same
// collision footing as Canonical_digest. This is still only the discovery
// representation: folded_loop_identical's shape/accounting gates are what make
// the accepted mixed-loop class deliberately small and inspectable.
struct Fold_key {
  uint64_t h0                                 = 0;
  uint64_t h1                                 = 0;
  auto     operator<=>(const Fold_key&) const = default;
};

Fold_key fold_atom(std::string_view tag, std::string_view payload = {}) {
  const uint64_t t = hstr(tag);
  const uint64_t p = hstr(payload);
  return {hcombine(t, p), hcombine(hcombine(0xd6e8feb86659fd93ULL, t), p)};
}

Fold_key fold_join(Fold_key base, uint64_t value) {
  base.h0 = hcombine(base.h0, value);
  base.h1 = hcombine(base.h1, mix64(value ^ 0xa0761d6478bd642fULL));
  return base;
}

Fold_key fold_join(Fold_key base, Fold_key value) {
  base = fold_join(base, value.h0);
  return fold_join(base, value.h1);
}

int occurrence_width(const hhds::Occurrence_pin& pin) {
  if (pin.is_invalid()) {
    return 0;
  }
  if (gu::is_graph_input_pin(pin)) {
    auto io = pin.get_graph() == nullptr ? std::shared_ptr<hhds::GraphIO>{} : pin.get_graph()->get_io();
    if (io) {
      return gu::bits_of(pin, *io, pin.get_pin_name());
    }
  }
  return gu::bits_of(pin);
}

std::optional<hhds::Occurrence_pin> occurrence_value_input(const hhds::Occurrence_node& node, std::string_view name) {
  const auto           pid = Ntype::get_sink_pid(gu::type_op_of(node), name);
  hhds::Occurrence_pin found;
  for (const auto& edge : node.inp_edges()) {
    if (edge.sink.get_port_id() != pid) {
      continue;
    }
    if (!found.is_invalid()) {
      return std::nullopt;
    }
    found = edge.driver;
  }
  return found.is_invalid() ? std::nullopt : std::optional<hhds::Occurrence_pin>{found};
}

bool occurrence_all_bits_mask(const hhds::Occurrence_node& node) {
  if (gu::type_op_of(node) != Ntype_op::Get_mask) {
    return false;
  }
  auto mask = occurrence_value_input(node, "mask");
  if (!mask || !gu::is_const_pin(*mask)) {
    return false;
  }
  const auto value = gu::hydrate_const(*mask);
  return !value.has_unknowns() && value.is_just_i64() && value.to_just_i64() == -1;
}

// at_width's per-graph CACHE identity (not the cross-design fold key): the
// pin's structural occurrence plus the requested width. It must never be a
// display name — two nodes in one body are allowed to share attrs::name (cgen
// de-collides them only at emit time), so a name-keyed cache hands the second
// cone the FIRST cone's fold key and two different functions fold alike.
struct Memo_key {
  hhds::Occurrence_index pin;
  int                    width = 0;

  bool operator==(const Memo_key&) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const Memo_key& key) {
    return H::combine(std::move(h), key.pin, key.width);
  }
};

// Canonicalize a virtually-flattened combinational graph. The one intentional
// algebraic normalization is addition modulo the selected output width:
// intermediate Get_mask(-1) fits may move across an all-positive Sum when both
// sides of the fit are at least that modulus. This is the exact bit-vector
// identity (a+b mod 2^W), and is what lets a compact carry chain compare with
// cprop's single n-ary reduction without materializing either representation.
class Virtual_expr {
public:
  explicit Virtual_expr(hhds::Graph* graph) : graph_(graph), view_(graph->occurrences()) {}

  std::optional<std::vector<std::pair<std::string, Fold_key>>> outputs() {
    if (graph_ == nullptr || graph_->get_io() == nullptr) {
      return std::nullopt;
    }
    std::vector<std::pair<std::string, Fold_key>> result;
    auto                                          out = view_.lift(graph_->get_output_node());
    for (const auto& edge : out.inp_edges()) {
      const std::string name{edge.sink.get_pin_name()};
      const int         width = static_cast<int>(graph_->get_io()->get_bits(name));
      if (width <= 0) {
        return std::nullopt;
      }
      auto key = at_width(edge.driver, width);
      if (!key) {
        return std::nullopt;
      }
      result.emplace_back(name, *key);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return result;
  }

  bool every_live_node_consumed() {
    for (const auto& node : view_.nodes(hhds::Node_order::forward)) {
      const auto op = gu::type_op_of(node);
      if (op == Ntype_op::Nconst) {
        continue;
      }
      if (op == Ntype_op::Sub) {
        continue;  // occurrence view also surfaces the expanded wrapper
      }
      if (is_state(op)) {
        return false;
      }
      if (node.has_out_edges() && !visited_.contains(node.get_occurrence_index())) {
        return false;
      }
    }
    return !failed_;
  }

private:
  std::optional<int64_t> loop_index_value(const hhds::Occurrence_pin& pin) const {
    if (!gu::is_graph_input_pin(pin) || pin.path().steps().empty() || graph_ == nullptr || graph_->get_io() == nullptr
        || graph_->get_io()->get_library() == nullptr) {
      return std::nullopt;
    }
    const auto& step = pin.path().steps().back();
    if (!step.ordinal) {
      return std::nullopt;
    }
    const auto sub  = graph_->get_io()->get_library()->get_node(step.subnode);
    const auto loop = sub.subnode_loop();
    if (!loop || !loop->index_input || pin.get_port_id() != *loop->index_input) {
      return std::nullopt;
    }
    return loop->index_at(*step.ordinal);
  }

  std::optional<hhds::Occurrence_pin> loop_initial_driver(const hhds::Occurrence_pin& pin) const {
    if (!gu::is_graph_input_pin(pin) || pin.path().steps().size() != 1 || graph_ == nullptr || graph_->get_io() == nullptr
        || graph_->get_io()->get_library() == nullptr) {
      return std::nullopt;
    }
    const auto& step = pin.path().steps().back();
    if (!step.ordinal) {
      return std::nullopt;
    }
    const auto sub = graph_->get_io()->get_library()->get_node(step.subnode);
    if (sub.is_invalid() || !sub.is_loop_subnode()) {
      return std::nullopt;
    }
    for (const auto occurrence : sub.subnode_group().occurrences()) {
      if (occurrence.ordinal() != *step.ordinal) {
        continue;
      }
      for (const auto& binding : occurrence.input_bindings()) {
        if (binding.input_port() != pin.get_port_id() || binding.kind() != hhds::Input_binding_kind::carry_initial
            || binding.stored_edges().size() != 1) {
          continue;
        }
        return view_.lift(binding.stored_edges().front().driver);
      }
      break;
    }
    return std::nullopt;
  }

  std::optional<Fold_key> at_width(const hhds::Occurrence_pin& pin, int width) {
    if (pin.is_invalid() || width <= 0) {
      failed_ = true;
      return std::nullopt;
    }
    const Memo_key memo_key{pin.get_occurrence_index(), width};
    if (auto it = memo_.find(memo_key); it != memo_.end()) {
      return it->second;
    }
    if (!active_.insert(memo_key).second) {
      failed_ = true;  // v1 is combinational; any residual cycle declines
      return std::nullopt;
    }

    std::optional<Fold_key> result;
    if (gu::is_const_pin(pin)) {
      result = fold_atom("const", gu::hydrate_const(pin).serialize());
    } else if (gu::is_graph_input_pin(pin)) {
      if (auto index = loop_index_value(pin)) {
        result = fold_atom("const", Dlop::create_integer(*index)->serialize());
      } else if (auto initial = loop_initial_driver(pin)) {
        result = at_width(*initial, width);
      } else {
        auto key = fold_atom("input", pin.get_pin_name());
        key      = fold_join(key, static_cast<uint64_t>(width));
        key      = fold_join(key, gu::is_unsign(pin) ? 1 : 2);
        result   = key;
      }
    } else {
      const auto node = pin.get_master_node();
      visited_.insert(node.get_occurrence_index());
      const int native_width = occurrence_width(pin);

      // A zero-extension/truncation can be crossed only while it preserves the
      // modulus being compared. This deliberately does NOT make a narrowing
      // Get_mask globally transparent; outside an additive reduction it stays
      // a normal node with its own width and operand.
      if (occurrence_all_bits_mask(node)) {
        auto       input           = occurrence_value_input(node, "a");
        const bool input_preserves = input
                                     && ((gu::is_const_pin(*input) && !gu::hydrate_const(*input).has_unknowns()
                                          && !gu::hydrate_const(*input).is_negative())
                                         || occurrence_width(*input) >= width);
        if (input && native_width >= width && input_preserves) {
          result = at_width(*input, width);
        }
      }

      if (!result && gu::type_op_of(node) == Ntype_op::Sum && native_width >= width) {
        std::vector<Fold_key> terms;
        if (collect_sum(pin, width, terms)) {
          std::sort(terms.begin(), terms.end());
          Fold_key key = fold_join(fold_atom("sum-mod"), static_cast<uint64_t>(width));
          for (const auto& term : terms) {
            key = fold_join(key, term);
          }
          result = key;
        }
      }

      if (!result) {
        auto key = native(pin);
        if (key) {
          result = occurrence_width(pin) == width ? *key : fold_join(fold_join(fold_atom("fit"), width), *key);
        }
      }
    }

    active_.erase(memo_key);
    if (result) {
      memo_.emplace(memo_key, *result);
    }
    return result;
  }

  bool collect_sum(const hhds::Occurrence_pin& pin, int width, std::vector<Fold_key>& terms) {
    if (pin.is_invalid()) {
      return false;
    }
    if (!gu::is_const_pin(pin) && !gu::is_graph_input_pin(pin)) {
      const auto node = pin.get_master_node();
      visited_.insert(node.get_occurrence_index());
      if (occurrence_all_bits_mask(node)) {
        auto       input           = occurrence_value_input(node, "a");
        const bool input_preserves = input
                                     && ((gu::is_const_pin(*input) && !gu::hydrate_const(*input).has_unknowns()
                                          && !gu::hydrate_const(*input).is_negative())
                                         || occurrence_width(*input) >= width);
        if (input && occurrence_width(pin) >= width && input_preserves) {
          return collect_sum(*input, width, terms);
        }
      }
      if (gu::type_op_of(node) == Ntype_op::Sum) {
        // A full-precision unsigned sum can also be distributed into a wider
        // comparison modulus. This matters when cprop drops a zero seed: the
        // first binary lane sum may be narrower than the final n-ary sum, but
        // neither representation has discarded a carry. Signed or unknown
        // operands keep their native fit so extension semantics remain exact.
        bool exact_unsigned_sum = gu::is_unsign(pin);
        for (const auto& edge : node.inp_edges()) {
          if (edge.sink.get_port_id() != 0) {
            exact_unsigned_sum = false;
            break;
          }
          if (gu::is_const_pin(edge.driver)) {
            const auto value = gu::hydrate_const(edge.driver);
            if (value.has_unknowns() || value.is_negative()) {
              exact_unsigned_sum = false;
              break;
            }
          } else if (!gu::is_unsign(edge.driver)) {
            exact_unsigned_sum = false;
            break;
          }
        }
        if (occurrence_width(pin) < width && !exact_unsigned_sum) {
          auto term = at_width(pin, width);
          if (!term) {
            return false;
          }
          terms.push_back(*term);
          return true;
        }
        bool any = false;
        for (const auto& edge : node.inp_edges()) {
          // Sum port 0 is addition. Any subtract/other operand makes this a
          // non-associative shape and the fold declines.
          if (edge.sink.get_port_id() != 0 || !collect_sum(edge.driver, width, terms)) {
            return false;
          }
          any = true;
        }
        return any;
      }
    }
    if (gu::is_const_pin(pin)) {
      const auto c = gu::hydrate_const(pin);
      if (!c.has_unknowns() && c.is_known_zero()) {
        // +0 is the additive identity of the modulus being compared: cprop
        // drops a Sum's lone zero addend, so the unrolled side may lack the
        // seed constant the compact virtual expansion still carries.
        return true;
      }
    }
    auto term = at_width(pin, width);
    if (!term) {
      return false;
    }
    terms.push_back(*term);
    return true;
  }

  std::optional<Fold_key> native(const hhds::Occurrence_pin& pin) {
    if (pin.is_invalid() || gu::is_const_pin(pin) || gu::is_graph_input_pin(pin)) {
      return at_width(pin, std::max(1, occurrence_width(pin)));
    }
    const auto node = pin.get_master_node();
    const auto op   = gu::type_op_of(node);
    if (op == Ntype_op::Sub || is_state(op) || op == Ntype_op::Invalid) {
      failed_ = true;
      return std::nullopt;
    }
    Fold_key key = fold_join(fold_atom("node"), static_cast<uint64_t>(op));
    key          = fold_join(key, static_cast<uint64_t>(std::max(0, occurrence_width(pin))));
    key          = fold_join(key, static_cast<uint64_t>(static_cast<uint32_t>(pin.get_port_id())));

    absl::flat_hash_map<int, std::vector<Fold_key>> by_port;
    for (const auto& edge : node.inp_edges()) {
      const int child_width = std::max(1, occurrence_width(edge.driver));
      auto      child       = at_width(edge.driver, child_width);
      if (!child) {
        return std::nullopt;
      }
      by_port[edge.sink.get_port_id()].push_back(*child);
    }
    std::vector<int> ports;
    ports.reserve(by_port.size());
    for (const auto& [port, _] : by_port) {
      ports.push_back(port);
    }
    std::sort(ports.begin(), ports.end());
    for (int port : ports) {
      key          = fold_join(key, static_cast<uint64_t>(static_cast<uint32_t>(port)) | (1ULL << 40U));
      auto& values = by_port[port];
      std::sort(values.begin(), values.end());
      for (const auto& value : values) {
        key = fold_join(key, value);
      }
    }
    return key;
  }

  hhds::Graph*                                graph_ = nullptr;
  hhds::Occurrences_view                      view_;
  absl::flat_hash_map<Memo_key, Fold_key>     memo_;
  absl::flat_hash_set<Memo_key>               active_;
  absl::flat_hash_set<hhds::Occurrence_index> visited_;
  bool                                        failed_ = false;
};

bool same_io_contract(hhds::Graph* a, hhds::Graph* b) {
  if (a == nullptr || b == nullptr || a->get_io() == nullptr || b->get_io() == nullptr) {
    return false;
  }
  auto describe = [](const auto& decls, char kind) {
    std::vector<std::string> out;
    for (const auto& decl : decls) {
      out.push_back(std::format("{}:{}:{}:{}", kind, decl.name, decl.bits, decl.unsign));
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  return describe(a->get_io()->get_input_pin_decls(), 'i') == describe(b->get_io()->get_input_pin_decls(), 'i')
         && describe(a->get_io()->get_output_pin_decls(), 'o') == describe(b->get_io()->get_output_pin_decls(), 'o');
}

int graph_input_uses(const hhds::Pin_class& root, const hhds::Pin_class& wanted, absl::flat_hash_set<hhds::Class_index>& active) {
  if (root.is_invalid()) {
    return 0;
  }
  if (gu::is_graph_input_pin(root)) {
    return root.get_port_id() == wanted.get_port_id() ? 1 : 0;
  }
  if (gu::is_const_pin(root)) {
    return 0;
  }
  auto node = root.get_master_node();
  if (!active.insert(node.get_class_index()).second) {
    return -1;
  }
  int uses = 0;
  for (const auto& edge : node.inp_edges()) {
    const int n = graph_input_uses(edge.driver, wanted, active);
    if (n < 0) {
      return -1;
    }
    uses += n;
  }
  active.erase(node.get_class_index());
  return uses;
}

}  // namespace

bool folded_loop_identical(hhds::Graph* compact, hhds::Graph* unrolled) {
  if (!same_io_contract(compact, unrolled)) {
    return false;
  }

  hhds::Node_class loop;
  for (const auto& node : compact->body().nodes(hhds::Node_order::forward)) {
    if (gu::type_op_of(node) == Ntype_op::Sub) {
      if (!node.is_loop_subnode() || !loop.is_invalid()) {
        return false;
      }
      loop = node;
    } else if (is_state(gu::type_op_of(node))) {
      return false;
    }
  }
  if (loop.is_invalid()) {
    return false;
  }
  for (const auto& node : unrolled->body().nodes(hhds::Node_order::forward)) {
    if (gu::type_op_of(node) == Ntype_op::Sub || is_state(gu::type_op_of(node))) {
      return false;
    }
  }

  const auto descriptor = loop.subnode_loop();
  const auto body       = loop.get_subnode_graph();
  if (!descriptor || !body || descriptor->count < 2 || descriptor->count > (1u << 20) || !descriptor->index_input
      || descriptor->activation_input || descriptor->next_active_output) {
    return false;
  }
  const auto carries = loop.subnode_group().carries();
  if (carries.size() != 1) {
    return false;
  }
  for (const auto& node : body->body().nodes(hhds::Node_order::forward)) {
    if (gu::type_op_of(node) == Ntype_op::Sub || is_state(gu::type_op_of(node))) {
      return false;  // nested/stateful lanes decline in v1
    }
  }

  auto input_decl = [&](hhds::Port_id pid) -> const hhds::GraphIO::DeclaredIoPin* {
    for (const auto& decl : body->get_io()->get_input_pin_decls()) {
      if (decl.port_id == pid) {
        return &decl;
      }
    }
    return nullptr;
  };
  auto output_decl = [&](hhds::Port_id pid) -> const hhds::GraphIO::DeclaredIoPin* {
    for (const auto& decl : body->get_io()->get_output_pin_decls()) {
      if (decl.port_id == pid) {
        return &decl;
      }
    }
    return nullptr;
  };
  const auto* index_decl  = input_decl(*descriptor->index_input);
  const auto* carry_idecl = input_decl(carries.front().input_port());
  const auto* carry_odecl = output_decl(carries.front().output_port());
  if (index_decl == nullptr || carry_idecl == nullptr || carry_odecl == nullptr || carry_idecl->bits == 0
      || carry_idecl->bits != carry_odecl->bits || !carry_idecl->unsign || !carry_odecl->unsign) {
    return false;
  }
  const auto index_pin = body->get_input_pin(index_decl->name);
  const auto carry_pin = body->get_input_pin(carry_idecl->name);
  if (index_pin.is_invalid() || carry_pin.is_invalid()) {
    return false;
  }

  // One body output, driven by an additive node with exactly one carry operand
  // and one lane operand. The lane must abstract exactly one index use and no
  // carry use; the carry operand must contain exactly one carry and no index.
  hhds::Pin_class carry_driver;
  size_t          output_edges = 0;
  for (const auto& edge : body->get_output_node().inp_edges()) {
    ++output_edges;
    if (edge.sink.get_pin_name() == carry_odecl->name) {
      carry_driver = edge.driver;
    }
  }
  if (output_edges != 1 || carry_driver.is_invalid() || gu::is_const_pin(carry_driver) || gu::is_graph_input_pin(carry_driver)
      || gu::type_op_of(carry_driver.get_master_node()) != Ntype_op::Sum) {
    return false;
  }
  int carry_operands = 0;
  int lane_operands  = 0;
  for (const auto& edge : carry_driver.get_master_node().inp_edges()) {
    if (edge.sink.get_port_id() != 0) {
      return false;
    }
    absl::flat_hash_set<hhds::Class_index> active_index;
    absl::flat_hash_set<hhds::Class_index> active_carry;
    const int                              index_uses = graph_input_uses(edge.driver, index_pin, active_index);
    const int                              carry_uses = graph_input_uses(edge.driver, carry_pin, active_carry);
    if (index_uses < 0 || carry_uses < 0) {
      return false;
    }
    if (index_uses == 0 && carry_uses == 1) {
      ++carry_operands;
    } else if (index_uses == 1 && carry_uses == 0) {
      ++lane_operands;
    } else {
      return false;
    }
  }
  if (carry_operands != 1 || lane_operands != 1) {
    return false;
  }

  Virtual_expr compact_expr(compact);
  Virtual_expr unrolled_expr(unrolled);
  const auto   compact_outputs  = compact_expr.outputs();
  const auto   unrolled_outputs = unrolled_expr.outputs();
  const bool   compact_live     = compact_outputs && compact_expr.every_live_node_consumed();
  const bool   unrolled_live    = unrolled_outputs && unrolled_expr.every_live_node_consumed();
  if (!compact_outputs || !unrolled_outputs || *compact_outputs != *unrolled_outputs || !compact_live || !unrolled_live) {
    return false;
  }

  // The only mutation is an inspectable correspondence attribute after every
  // hard gate has discharged. No nodes or edges are added/removed.
  gu::set_match(loop, 1);
  for (const auto& node : body->body().nodes(hhds::Node_order::forward)) {
    gu::set_match(node, 1);
  }
  for (const auto& node : unrolled->body().nodes(hhds::Node_order::forward)) {
    gu::set_match(node, 1);
  }
  return true;
}

bool structural_equivalent_traversal(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts) {
  if (a == nullptr || b == nullptr) {
    return false;
  }

  // ---- Analyze both sides once: fsig pairs the acyclic majority robustly, so
  // the traversal only has to resolve the cycle cores. build_sides seeds cut
  // points (state + blackbox Subs) exactly as the signature path, so the two
  // agree on what a source is.
  Side         sa, sb;
  Match_result scratch;
  build_sides(a, b, opts, sa, sb, scratch);
  // A proven rewiring between compare points is a real difference -- never a
  // stalled-signature false miss -- so do not try to rescue it.
  if (scratch.cut_violated != 0) {
    return false;
  }

  Bimap ab, ba;  // a<->b node bijection
  auto  try_pair = [&](const hhds::Node_class& na, const hhds::Node_class& nb) -> bool {
    auto ci_a = na.get_class_index();
    auto ci_b = nb.get_class_index();
    auto ia   = ab.find(ci_a);
    auto ib   = ba.find(ci_b);
    if (ia != ab.end() || ib != ba.end()) {
      return ia != ab.end() && ia->second == ci_b && ib != ba.end() && ib->second == ci_a;  // consistent?
    }
    ab.emplace(ci_a, ci_b);
    ba.emplace(ci_b, ci_a);
    return true;
  };

  // ---- Pair the acyclic majority by forward signature (1:1 in fcommon). This
  // is only DISCOVERY -- a signature collision would mispair, but verify rejects
  // any inconsistent edge, so it can only cost a false miss, never a false hit.
  {
    absl::flat_hash_map<uint64_t, hhds::Node_class> fa, fb;
    absl::flat_hash_set<uint64_t>                   fa_dup, fb_dup;
    auto bucket = [](Side& s, absl::flat_hash_map<uint64_t, hhds::Node_class>& m, absl::flat_hash_set<uint64_t>& dup) {
      for (const auto& node : s.order) {
        auto it = s.fsig.find(node.get_class_index());
        if (it == s.fsig.end()) {
          continue;
        }
        if (!m.try_emplace(it->second, node).second) {
          dup.insert(it->second);
        }
      }
    };
    bucket(sa, fa, fa_dup);
    bucket(sb, fb, fb_dup);
    for (const auto& [v, na] : fa) {
      if (fa_dup.contains(v)) {
        continue;
      }
      auto it = fb.find(v);
      if (it == fb.end() || fb_dup.contains(v)) {
        continue;
      }
      try_pair(na, it->second);
    }
  }

  // ---- Forward traversal to reach the cycle cores the signature stranded.
  // Worklist of matched driver pins (a signal correspondence); a visited set on
  // the a-side pin keeps each processed once. mismatch=true aborts the rescue.
  std::vector<std::pair<hhds::Pin_class, hhds::Pin_class>> work;
  absl::flat_hash_set<std::pair<uint64_t, uint32_t>>       seen_pin;
  bool                                                     mismatch = false;

  auto enqueue = [&](const hhds::Pin_class& pa, const hhds::Pin_class& pb) {
    auto key = std::make_pair(static_cast<uint64_t>(pa.get_master_node().get_class_index().value),
                              static_cast<uint32_t>(pa.get_port_id()));
    if (seen_pin.insert(key).second) {
      work.emplace_back(pa, pb);
    }
  };

  // The distinct output driver pins of a node, keyed for cross-side matching
  // (Sub: by name; else by port_id).
  auto enqueue_node_outputs = [&](const hhds::Node_class& na, const hhds::Node_class& nb) {
    bool                                           sub = gu::type_op_of(na) == Ntype_op::Sub;
    absl::flat_hash_map<uint64_t, hhds::Pin_class> bmap;
    absl::flat_hash_set<uint64_t>                  bseen;
    for (const auto& e : nb.out_edges()) {
      uint64_t k = sub ? hstr(e.driver.get_pin_name()) : static_cast<uint64_t>(static_cast<uint32_t>(e.driver.get_port_id()));
      if (bseen.insert(k).second) {
        bmap.emplace(k, e.driver);
      }
    }
    absl::flat_hash_set<uint64_t> aseen;
    for (const auto& e : na.out_edges()) {
      uint64_t k = sub ? hstr(e.driver.get_pin_name()) : static_cast<uint64_t>(static_cast<uint32_t>(e.driver.get_port_id()));
      if (!aseen.insert(k).second) {
        continue;
      }
      auto it = bmap.find(k);
      if (it != bmap.end()) {
        enqueue(e.driver, it->second);
      }
    }
  };

  // Seed 1: graph inputs by name.
  {
    absl::flat_hash_map<std::string_view, hhds::Pin_class> bin;
    for (const auto& e : b->get_input_node().out_edges()) {
      bin.try_emplace(e.driver.get_pin_name(), e.driver);
    }
    absl::flat_hash_set<std::string_view> adone;
    for (const auto& e : a->get_input_node().out_edges()) {
      if (!adone.insert(e.driver.get_pin_name()).second) {
        continue;
      }
      auto it = bin.find(e.driver.get_pin_name());
      if (it != bin.end()) {
        enqueue(e.driver, it->second);
      }
    }
  }
  // Seed 2: outputs of every already-paired node (fsig pairs + cut points).
  for (const auto& [ci_a, ci_b] : ab) {
    enqueue_node_outputs(a->get_node(ci_a), b->get_node(ci_b));
  }

  // A cycle node's canonical bucket key: op + width + its resolved inputs (an
  // unresolved fanin folds a "?" placeholder), which tells mux(sel,a,self) from
  // mux(sel,b,self) even before the self edge is closed.
  auto canon = [&](const hhds::Node_class& node, bool a_side) -> uint64_t {
    uint64_t                                        h = node_kind_key(node);
    absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
    for (const auto& e : node.inp_edges()) {
      bool     resolved;
      uint64_t d = driver_desc(e.driver, a_side, ab, ba, resolved);
      by_port[e.sink.get_port_id()].push_back(resolved ? d : hstr("\x01?"));
    }
    return fold_operands(h, by_port);
  };

  while (!work.empty() && !mismatch) {
    auto [da, db] = work.back();
    work.pop_back();
    // Unpaired, non-cut, non-output consumers on each side.
    auto gather = [&](const hhds::Pin_class& d, bool a_side, std::vector<hhds::Node_class>& out) {
      absl::flat_hash_set<hhds::Class_index> dedup;
      for (const auto& e : d.out_edges()) {
        if (gu::is_graph_output_pin(e.sink)) {
          continue;
        }
        auto sn = e.sink.get_master_node();
        if (is_cut(sn, opts.blackbox_subs)) {
          continue;  // an anchor: paired by fsig-seed, its inputs are obligations
        }
        auto ci = sn.get_class_index();
        if ((a_side ? ab.contains(ci) : ba.contains(ci))) {
          continue;  // already paired
        }
        if (dedup.insert(ci).second) {
          out.push_back(sn);
        }
      }
    };
    std::vector<hhds::Node_class> alist, blist;
    gather(da, true, alist);
    gather(db, false, blist);
    if (alist.empty() && blist.empty()) {
      continue;
    }
    auto by_canon = [&](std::vector<hhds::Node_class>& v, bool a_side) {
      std::sort(v.begin(), v.end(), [&](const hhds::Node_class& x, const hhds::Node_class& y) {
        return canon(x, a_side) < canon(y, a_side);
      });
    };
    by_canon(alist, true);
    by_canon(blist, false);
    // Zip same-canon consumers; a canon present on only one side is left for
    // another driver / caught by verify -- discovery must not abort here.
    size_t i = 0, j = 0;
    while (i < alist.size() && j < blist.size()) {
      uint64_t ka = canon(alist[i], true);
      uint64_t kb = canon(blist[j], false);
      if (ka < kb) {
        ++i;
      } else if (ka > kb) {
        ++j;
      } else {
        if (!try_pair(alist[i], blist[j])) {
          mismatch = true;
          break;
        }
        enqueue_node_outputs(alist[i], blist[j]);
        ++i;
        ++j;
      }
    }
  }
  if (mismatch) {
    return false;
  }

  // ---- VERIFY: the exact, sole soundness gate. Every user node on both sides
  // must be paired (a total bijection), and every paired node must agree on op,
  // width, Sub interface, and -- the crux -- its full input-edge multiset under
  // the bijection. A wrong discovery pairing, a rewiring, or an unreached node
  // all surface here as an inequality, so a `true` is a genuine isomorphism.
  for (auto node : a->body().nodes(hhds::Node_order::forward)) {
    if (!ab.contains(node.get_class_index())) {
      return false;  // an a node the traversal never paired
    }
  }
  size_t b_nodes = 0;
  for (auto node : b->body().nodes(hhds::Node_order::forward)) {
    if (!ba.contains(node.get_class_index())) {
      return false;
    }
    ++b_nodes;
  }
  if (ab.size() != b_nodes) {
    return false;  // not a bijection (size mismatch)
  }
  for (auto na : a->body().nodes(hhds::Node_order::forward)) {
    auto nb = b->get_node(ab.at(na.get_class_index()));
    if (gu::type_op_of(na) != gu::type_op_of(nb)) {
      return false;
    }
    if (gu::type_op_of(na) == Ntype_op::Sub) {
      if (sub_iface_key(na) != sub_iface_key(nb)) {
        return false;  // blackbox interface changed
      }
    } else if (node_out_bits(na) != node_out_bits(nb)) {
      return false;
    }
    bool oka, okb;
    auto da = input_descs(na, true, ab, ba, oka);
    auto db = input_descs(nb, false, ab, ba, okb);
    if (!oka || !okb || da != db) {
      return false;  // an input edge does not map under the bijection
    }
  }
  // Graph outputs are compare points too (not forward_class nodes): the output
  // node's input-edge multiset must correspond, or a swapped/rewired output slips
  // through the node bijection.
  {
    bool oka, okb;
    auto da = input_descs(a->get_output_node(), true, ab, ba, oka);
    auto db = input_descs(b->get_output_node(), false, ab, ba, okb);
    if (!oka || !okb || da != db) {
      return false;
    }
  }
  return true;
}

namespace {

// One def's digest, recursing into resolvable Sub bodies (Merkle). `memo` and
// `visiting` are shared across the whole canonical_digest() call so shared
// children in the instance DAG are digested once and a cycle is caught.
Canonical_digest digest_one(hhds::Graph* g, const Digest_resolver& resolve, absl::flat_hash_map<hhds::Gid, Canonical_digest>& memo,
                            absl::flat_hash_set<hhds::Gid>& visiting, Sub_fold sub_fold) {
  Canonical_digest d;
  if (g == nullptr) {
    return d;
  }

  // Refuse anonymous state cells up front (see semdiff.hpp): their state_key
  // falls back to the per-run debug nid, which is neither stable across
  // processes nor safely replaceable by a constant.
  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    if (is_state(gu::type_op_of(node)) && state_key(g, node).starts_with("f:")) {
      return d;  // valid=false — not digestable, callers skip the cache
    }
  }

  Semdiff_options opts;
  opts.matching_names = true;  // state cells keyed by hierarchical name — lec's
                               // correspondence basis, so digest-equal transfers
  Side s              = analyze(g, opts);

  // Order-independent fold: one token per node (fsig = input-cone identity,
  // bsig = output-cone identity, kind = local shape), sorted so allocation /
  // iteration order can never leak into the digest. The canonical form follows
  // pass/submatch's convention: sort the constituent hashes, then mix.
  std::vector<uint64_t> toks;
  toks.reserve(s.order.size() + 16);
  for (const auto& node : s.order) {
    auto     ci = node.get_class_index();
    uint64_t f  = 0;
    uint64_t b  = 0;
    if (auto it = s.fsig.find(ci); it != s.fsig.end()) {
      f = it->second;
    }
    if (auto it = s.bsig.find(ci); it != s.bsig.end()) {
      b = it->second;
    }
    uint64_t tok = hcombine(hcombine(f, b), node_kind_key(node));
    // interface mode: a Sub folds ONLY its def identity (the gid in node_kind_key)
    // and its boundary connectivity (already in f/b) -- exactly a bodyless
    // blackbox. abc region reuse needs this: a child-body edit must not invalidate
    // the parent region's already-mapped netlist.
    if (sub_fold == Sub_fold::merkle) {
      if (auto gid = node.get_subnode_gid(); gid != hhds::Gid_invalid) {
        // Hierarchical (Merkle) fold: a Sub with a resolvable body takes the
        // CHILD'S digest as its identity — an edited child that the encoder
        // flattens into this def's proof must change this digest, or a cached
        // verdict would go stale. No body => true blackbox (UF in proofs): the
        // def-name identity already folded by node_kind_key stands.
        if (hhds::Graph* child = resolve ? resolve(gid) : nullptr) {
          Canonical_digest cd;
          if (auto it = memo.find(gid); it != memo.end()) {
            cd = it->second;
          } else {
            if (!visiting.insert(gid).second) {
              return {};  // instantiation cycle: not digestable
            }
            cd = digest_one(child, resolve, memo, visiting, sub_fold);
            visiting.erase(gid);
            memo.emplace(gid, cd);
          }
          if (!cd.valid) {
            return {};  // an undigestable child poisons every ancestor
          }
          tok = hcombine(hcombine(tok, cd.h0), cd.h1);
        }
      }
    }
    toks.push_back(tok);
  }
  // The module interface, explicitly: lec pairs IO by name, a dangling port
  // never shows up in any node signature above, and the name<->port_id binding
  // is part of the identity (a parent's edges carry port ids; permuting the
  // binding changes what those edges mean without touching this graph's nodes).
  // Width is read the way the lec encoder reads it (pin bits attr with decl
  // fallback — graph_util::real_width), so digest-equal implies encode-equal.
  auto gio = g->get_io();
  for (const auto& dio : gio->get_input_pin_decls()) {
    uint64_t t = hcombine(hstr("\x01idecl"), hstr(dio.name));
    t          = hcombine(t, static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(g->get_input_pin(dio.name), *gio, dio.name))));
    t          = hcombine(t, static_cast<uint64_t>(dio.port_id) | (static_cast<uint64_t>(dio.unsign) << 32U));
    toks.push_back(t);
  }
  for (const auto& dio : gio->get_output_pin_decls()) {
    uint64_t t = hcombine(hstr("\x01odecl"), hstr(dio.name));
    t = hcombine(t, static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(g->get_output_pin(dio.name), *gio, dio.name))));
    t = hcombine(t, static_cast<uint64_t>(dio.port_id) | (static_cast<uint64_t>(dio.unsign) << 32U));
    toks.push_back(t);
  }
  std::sort(toks.begin(), toks.end());

  uint64_t h0 = 0x1ec0ffee2f0c0deULL;
  uint64_t h1 = 0x5a17ed15c0ffee5ULL;
  for (uint64_t t : toks) {
    h0 = hcombine(h0, t);
    h1 = hcombine(h1, mix64(t ^ 0x9e3779b97f4a7c15ULL));
  }
  d.h0    = h0;
  d.h1    = h1;
  d.valid = true;
  return d;
}

}  // namespace

Canonical_digest canonical_digest(hhds::Graph* g, const Digest_resolver& resolve, Sub_fold sub_fold) {
  absl::flat_hash_map<hhds::Gid, Canonical_digest> memo;
  return canonical_digest(g, resolve, memo, sub_fold);
}

Canonical_digest canonical_digest(hhds::Graph* g, const Digest_resolver& resolve,
                                  absl::flat_hash_map<hhds::Gid, Canonical_digest>& memo, Sub_fold sub_fold) {
  if (g == nullptr) {
    return {};
  }
  if (auto it = memo.find(g->get_gid()); it != memo.end()) {
    return it->second;  // this def was already digested as some other root's child
  }
  absl::flat_hash_set<hhds::Gid> visiting;
  visiting.insert(g->get_gid());  // catch self-instantiation
  auto d = digest_one(g, resolve, memo, visiting, sub_fold);
  memo.emplace(g->get_gid(), d);
  return d;
}

}  // namespace livehd::semdiff
