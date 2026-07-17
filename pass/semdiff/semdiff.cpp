// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "semdiff.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
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
constexpr uint64_t hcombine(uint64_t h, uint64_t v) {
  return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U)));
}
uint64_t hstr(std::string_view s) {
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
bool is_cut(const hhds::Node_class& node) {
  auto op = gu::type_op_of(node);  // LINK-based: set_subnode re-stamps the raw type
  return is_state(op) || (op == Ntype_op::Sub && node.is_loop_break());
}

// Name-normalization mirror of pass/lec/encode.cpp's flop_state_key (kept local
// so pass/semdiff stays free of the cvc5-heavy pass/lec dep): strip the SSA /
// hierarchical decoration so the same RTL register matches across front-ends.
std::string normalize_reg_name(std::string_view raw) {
  std::string_view s = raw;
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
// differing widths never falsely match (unsigned bits = magnitude+1 trap).
int32_t node_out_bits(const hhds::Node_class& node) {
  for (const auto& e : node.out_edges()) {
    if (auto b = gu::bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
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
    case Ntype_op::Latch: return pid == 3 || pid == 4;                // din, enable
    case Ntype_op::Fflop: return pid == 0 || pid == 3 || pid == 5;    // valid, din, stop
    case Ntype_op::Memory:
      return pid == 0 || pid == 3 || pid == 4 || pid == 12 || pid == 13;  // addr, din, enable, update, update_enable
    default: return true;  // comb node: every input is data
  }
}

struct State_cell {
  hhds::Node_class      node;
  std::string           key;    // state_key (tier-1 identity; mangled under name_noise)
  std::string           truth;  // name_noise only: the original key (empty = key is original)
  bool                  is_mem = false;
  uint64_t              kind   = 0;  // op + bits + init fold (local identity)
  std::vector<uint32_t> preds;       // state cells feeding a data pin through comb
  std::vector<uint32_t> succs;       // state cells fed from a driver pin through comb
  std::vector<uint64_t> in_anchors;   // graph-input tokens feeding a data pin
  std::vector<uint64_t> out_anchors;  // graph-output tokens reached from a driver pin
  // Transitive closure over the state graph, with the min HOP DISTANCE (BFS
  // level). Distance-annotated resolved points are a deliberate refinement over
  // the paper's plain sets: a linear chain (in -> A -> B -> out) is permanently
  // ambiguous under {in}/{out} set equality, while (in,1)/(in,2) tells A from B.
  // Annotation only refines buckets, and lec re-verifies every pair anyway.
  std::vector<std::pair<uint32_t, uint32_t>> reach_b;  // (index, dist>=1)
  std::vector<std::pair<uint32_t, uint32_t>> reach_f;
  uint64_t              token = 0;    // nonzero = resolved (tier-1 seed or tier-2 pair)
  bool                  t1_pair = false, t1_group = false, t2_pair = false, ambiguous = false;
  bool                  kind_clash = false;  // unpaired: cross-side SRP/ERP match refused by the
                                             // kind fold (op/bits/init — the pair precondition)
};

struct State_side {
  std::vector<State_cell>                          cells;
  absl::flat_hash_map<hhds::Class_index, uint32_t> index;  // node -> cells[] slot
  absl::flat_hash_map<uint64_t, std::string>       label;  // explain only: RP token -> name
};

State_side collect_state(hhds::Graph* g, const Semdiff_options& opts) {
  const bool want_labels = opts.explain_noise > 0;
  State_side ss;
  for (auto node : g->forward_class()) {
    auto op = gu::type_op_of(node);
    if (!is_state(op)) {
      continue;
    }
    State_cell c;
    c.node   = node;
    c.key    = state_key(g, node);
    c.is_mem = op == Ntype_op::Memory;
    c.kind   = hcombine(hstr("\x01skind"), static_cast<uint64_t>(op));
    c.kind   = hcombine(c.kind, static_cast<uint64_t>(static_cast<uint32_t>(node_out_bits(node))));
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
  const auto& reach = backward ? c.reach_b : c.reach_f;
  const auto& own   = backward ? c.in_anchors : c.out_anchors;
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
void pair_state(hhds::Graph* ga, hhds::Graph* gb, State_side& sa, State_side& sb, const Semdiff_options& opts,
                Match_result& res) {
  State_stats& st = res.state;
  st.a_total = static_cast<uint32_t>(sa.cells.size());
  st.b_total = static_cast<uint32_t>(sb.cells.size());
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
        c.truth = c.key;
        c.key += "\x01!noise";
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
      bool     one = av.size() == 1 && it->second.size() == 1;
      for (uint32_t i : av) {
        sa.cells[i].token   = tok;
        sa.cells[i].t1_pair = one;
        sa.cells[i].t1_group = !one;
        st.a_name_grouped += one ? 0 : 1;
      }
      for (uint32_t i : it->second) {
        sb.cells[i].token   = tok;
        sb.cells[i].t1_pair = one;
        sb.cells[i].t1_group = !one;
        st.b_name_grouped += one ? 0 : 1;
      }
      st.name_pairs += one ? 1 : 0;
      st.name_pairs_mem += one && sa.cells[av.front()].is_mem ? 1 : 0;
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
    for (uint32_t round = 1; round <= maxiter; ++round) {
      absl::flat_hash_map<uint64_t, std::vector<uint32_t>> asig, bsig;
      auto sig_of = [&](const State_side& ss, const State_cell& c) {
        uint64_t h = hcombine(c.kind, rp_signature(ss, c, /*backward=*/true));
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
          uint64_t tok                    = hcombine(hstr("\x02pair"), sig);
          if (opts.explain_noise > 0) {
            sa.label.try_emplace(tok, "pair:" + sa.cells[av.front()].key);
            sb.label.try_emplace(tok, "pair:" + sa.cells[av.front()].key);
          }
          sa.cells[av.front()].token      = tok;
          sa.cells[av.front()].t2_pair    = true;
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
            auto ri = rp_items(sa, cr, backward);
            auto ii = rp_items(sb, ci, backward);
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
            std::print("semdiff[explain] def '{}': impl '{}' ({} bits {}) noised, UNRECOVERED — same-sig candidates ref/impl {}/{}\n",
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

  auto finish = [&](hhds::Graph* g, State_side& ss, uint32_t& unpaired, uint32_t& ambiguous, std::vector<std::string>& names,
                    std::vector<std::string>& mem_diverged, std::string_view side) {
    for (auto& c : ss.cells) {
      if (c.token == 0) {
        ++unpaired;
        ambiguous += c.ambiguous ? 1 : 0;
        // A memory with a GENUINE mismatch (kind/init clash or no counterpart —
        // not mere symmetric ambiguity, which is occurrence-safe) must not be
        // force-collapsed by lec: its correspondence diverges. See 2f-lec guard.
        if (c.is_mem && !c.ambiguous) {
          mem_diverged.push_back(std::string(c.node.get_hier_name()));
        }
        if (opts.state_pairing) {
          names.push_back(c.node.get_hier_name()
                          + (c.ambiguous ? " (ambiguous)" : c.kind_clash ? " (kind/init mismatch)" : " (no full match)"));
        }
      }
      if (opts.dump_state) {
        std::print("semdiff[state]: {} '{}' {}{} {}\n",
                   side,
                   g->get_name(),
                   c.truth.empty() ? c.key : c.truth,
                   c.truth.empty() ? "" : " (noised)",
                   c.t1_pair    ? "name"
                   : c.t1_group ? "name-group"
                   : c.t2_pair  ? "full"
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
  hhds::Graph*                                      g = nullptr;
  std::vector<hhds::Node_class>                     order;   // forward_class order
  absl::flat_hash_map<hhds::Class_index, uint64_t>  fsig;    // forward signature
  absl::flat_hash_map<hhds::Class_index, uint64_t>  bsig;    // backward signature
  absl::flat_hash_set<uint64_t>                     fvals;   // fsig value set
  absl::flat_hash_set<uint64_t>                     bvals;   // bsig value set
};

// The forward pass's operand rule, factored out so the compare-point obligation
// below folds operands EXACTLY the way the signatures did. Returns false when the
// driver has no forward signature yet (i.e. it is past a frontier).
//
// The const case is not optional: a pid-encoded const (e.g. a get_mask's mask)
// has no CONST_NODE in forward_class, so an obligation that only consulted fsig
// would miss a changed constant entirely.
bool resolve_driver(const Side& s, const hhds::Pin_class& drv, uint64_t& out) {
  if (gu::is_graph_input_pin(drv)) {
    out = hcombine(hstr("\x01in"), hstr(drv.get_pin_name()));
    return true;
  }
  if (gu::is_const_pin(drv)) {
    // A constant operand (incl. the pid-encoded const that drives e.g. a
    // get_mask's mask) is anchored by value — its CONST_NODE is not a
    // forward_class node, so resolve it here or forward would stall and the
    // node would fall back to a coarser backward (symmetric) match.
    out = hcombine(hstr("\x01const"), hstr(gu::hydrate_const(drv).serialize()));
    return true;
  }
  auto it = s.fsig.find(drv.get_master_node().get_class_index());
  if (it == s.fsig.end()) {
    return false;
  }
  out = hcombine(it->second, static_cast<uint64_t>(static_cast<uint32_t>(drv.get_port_id())));
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
    for (auto node : g->forward_class()) {
      if (is_state(gu::type_op_of(node))) {
        uint64_t seed             = hcombine(hstr("\x01state"), hstr(state_key(g, node)));
        s.fsig[node.get_class_index()] = seed;
        s.bsig[node.get_class_index()] = seed;
      }
    }
  }

  // ---- Forward pass: inputs/consts -> outputs (topological). A node is ready
  // when every fanin signal already has a forward signature.
  for (auto node : g->forward_class()) {
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

    bool                                       ready = true;
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

    bool                                       ready = true;
    bool                                       any   = false;
    absl::flat_hash_map<int, std::vector<uint64_t>> by_port;
    for (const auto& e : node.out_edges()) {
      any = true;
      const auto& snk = e.sink;
      uint64_t    usig;
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

}  // namespace

Match_result structural_match(hhds::Graph* a, hhds::Graph* b, const Semdiff_options& opts) {
  Match_result res;
  if (a == nullptr || b == nullptr) {
    return res;
  }

  Side sa, sb;
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
        for (auto node : g->forward_class()) {
          if (gu::type_op_of(node) != Ntype_op::Sub || !node.is_loop_break()) {
            continue;
          }
          seeds.emplace(node.get_class_index(), hcombine(hstr("\x01sub"), hstr(cut_point_key(g, node))));
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
    auto collect_cuts = [&](const Side& s, absl::flat_hash_map<std::string, uint64_t>& k,
                            absl::flat_hash_set<std::string>& u) {
      for (const auto& node : s.order) {
        if (!is_cut(node)) {
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

  // Signatures present on BOTH sides are matchable.
  absl::flat_hash_set<uint64_t> fcommon;
  for (uint64_t v : sa.fvals) {
    if (sb.fvals.contains(v)) {
      fcommon.insert(v);
    }
  }
  absl::flat_hash_set<uint64_t> bcommon;
  for (uint64_t v : sa.bvals) {
    if (sb.bvals.contains(v)) {
      bcommon.insert(v);
    }
  }

  // Assign ids in a's forward_class order (deterministic); b reuses the map, so
  // a class shared across the two graphs lands on the same id on both sides.
  absl::flat_hash_map<Classkey, uint32_t> class2id;
  uint32_t                                next_id = 1;
  auto assign = [&](Side& side, uint32_t& matched, uint32_t& unmatched) {
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
    for (auto node : a->forward_class()) {
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
      uint32_t r           = find(id);
      auto [it, inserted] = root2region.try_emplace(r, next_region);
      if (inserted) {
        ++next_region;
      }
      return it->second;
    };
    for (hhds::Graph* g : {a, b}) {
      for (auto node : g->forward_class()) {
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

namespace {

// One def's digest, recursing into resolvable Sub bodies (Merkle). `memo` and
// `visiting` are shared across the whole canonical_digest() call so shared
// children in the instance DAG are digested once and a cycle is caught.
Canonical_digest digest_one(hhds::Graph* g, const Digest_resolver& resolve,
                            absl::flat_hash_map<hhds::Gid, Canonical_digest>& memo,
                            absl::flat_hash_set<hhds::Gid>&                   visiting) {
  Canonical_digest d;
  if (g == nullptr) {
    return d;
  }

  // Refuse anonymous state cells up front (see semdiff.hpp): their state_key
  // falls back to the per-run debug nid, which is neither stable across
  // processes nor safely replaceable by a constant.
  for (auto node : g->forward_class()) {
    if (is_state(gu::type_op_of(node)) && state_key(g, node).starts_with("f:")) {
      return d;  // valid=false — not digestable, callers skip the cache
    }
  }

  Semdiff_options opts;
  opts.matching_names = true;  // state cells keyed by hierarchical name — lec's
                               // correspondence basis, so digest-equal transfers
  Side s = analyze(g, opts);

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
          cd = digest_one(child, resolve, memo, visiting);
          visiting.erase(gid);
          memo.emplace(gid, cd);
        }
        if (!cd.valid) {
          return {};  // an undigestable child poisons every ancestor
        }
        tok = hcombine(hcombine(tok, cd.h0), cd.h1);
      }
    }
    toks.push_back(tok);
  }
  // The module interface, explicitly: lec pairs IO by name, a dangling port
  // never shows up in any node signature above, and the name<->port_id binding
  // is part of the identity (a parent's edges carry port ids; permuting the
  // binding changes what those edges mean without touching this graph's nodes).
  // Width is read the way the lec encoder reads it (pin bits attr with decl
  // fallback — encode.cpp real_width_io), so digest-equal implies encode-equal.
  auto gio = g->get_io();
  for (const auto& dio : gio->get_input_pin_decls()) {
    uint64_t t = hcombine(hstr("\x01idecl"), hstr(dio.name));
    t          = hcombine(t, static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(g->get_input_pin(dio.name), *gio, dio.name))));
    t          = hcombine(t, static_cast<uint64_t>(dio.port_id) | (static_cast<uint64_t>(dio.unsign) << 32U));
    toks.push_back(t);
  }
  for (const auto& dio : gio->get_output_pin_decls()) {
    uint64_t t = hcombine(hstr("\x01odecl"), hstr(dio.name));
    t          = hcombine(t, static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(g->get_output_pin(dio.name), *gio, dio.name))));
    t          = hcombine(t, static_cast<uint64_t>(dio.port_id) | (static_cast<uint64_t>(dio.unsign) << 32U));
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

Canonical_digest canonical_digest(hhds::Graph* g, const Digest_resolver& resolve) {
  absl::flat_hash_map<hhds::Gid, Canonical_digest> memo;
  return canonical_digest(g, resolve, memo);
}

Canonical_digest canonical_digest(hhds::Graph* g, const Digest_resolver& resolve,
                                  absl::flat_hash_map<hhds::Gid, Canonical_digest>& memo) {
  if (g == nullptr) {
    return {};
  }
  if (auto it = memo.find(g->get_gid()); it != memo.end()) {
    return it->second;  // this def was already digested as some other root's child
  }
  absl::flat_hash_set<hhds::Gid> visiting;
  visiting.insert(g->get_gid());  // catch self-instantiation
  auto d = digest_one(g, resolve, memo, visiting);
  memo.emplace(g->get_gid(), d);
  return d;
}

}  // namespace livehd::semdiff
