// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_reduce.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "color_common.hpp"
#include "diag.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"

namespace livehd::color {

namespace {

namespace gu = livehd::graph_util;

using Node = hhds::Node_class;
using Pin  = hhds::Pin_class;

// ---------------------------------------------------------------------------
// Hash toolkit -- the semdiff/abc_incr canonical-digest primitives. Kept
// file-private like theirs (each digest must be free to evolve with its own
// consumer's invalidation needs), and two-lane for the same reason abc_incr is:
// a splice on a colliding digest is a MISCOMPILE, so the verify walk below is
// the gate and the digest only proposes -- but a weak digest would still flood
// the walk with false buckets.
// ---------------------------------------------------------------------------
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

struct Sig {
  uint64_t a = 0, b = 0;
  friend bool operator==(const Sig& x, const Sig& y) = default;
  friend auto operator<=>(const Sig& x, const Sig& y) = default;
};
constexpr uint64_t kLaneB = 0x9ae16a3b2f90404fULL;

Sig sig_seed(uint64_t tag) { return {mix64(tag), mix64(tag ^ kLaneB)}; }
Sig sig_comb(Sig h, Sig v) { return {hcombine(h.a, v.a), hcombine(h.b, mix64(v.b ^ kLaneB))}; }
Sig sig_u64(Sig h, uint64_t v) { return sig_comb(h, {v, v}); }
Sig sig_str(Sig h, std::string_view s) { return sig_comb(h, {hstr(s), mix64(hstr(s) ^ kLaneB)}); }

// Fold a port-grouped operand list, commutative-normalizing WITHIN each
// sink-port class (the semdiff/abc_incr fold_operands rule: `a+b == b+a` on
// one port class, but operands never trade places across ports).
Sig fold_operands(Sig base, absl::flat_hash_map<int, std::vector<Sig>>& by_port) {
  std::vector<int> ports;
  ports.reserve(by_port.size());
  for (auto& [p, vs] : by_port) {
    (void)vs;
    ports.emplace_back(p);
  }
  std::sort(ports.begin(), ports.end());
  for (int p : ports) {
    auto& vs = by_port[p];
    std::sort(vs.begin(), vs.end());
    base = sig_u64(base, static_cast<uint64_t>(static_cast<uint32_t>(p)));
    for (const auto& v : vs) {
      base = sig_comb(base, v);
    }
  }
  return base;
}

// ---------------------------------------------------------------------------
// Cone model
// ---------------------------------------------------------------------------

// A driver pin's emission-relevant shape. `off` is the packed-slice pin_offset:
// it changes what cgen emits for the pin, so two cones differing only in it are
// NOT the same pattern.
struct Pin_shape {
  uint32_t pid  = 0;
  int32_t  bits = 0;
  bool     sign = false;
  int64_t  off  = 0;
  friend bool operator==(const Pin_shape& x, const Pin_shape& y) = default;
  friend auto operator<=>(const Pin_shape& x, const Pin_shape& y) = default;
};

Pin_shape shape_of(const Pin& p) {
  Pin_shape s;
  s.pid  = static_cast<uint32_t>(p.get_port_id());
  s.bits = gu::bits_of(p);
  s.sign = !gu::is_unsign(p);
  if (auto o = p.attr(livehd::attrs::pin_offset); o.has()) {
    s.off = static_cast<int64_t>(o.get());
  }
  return s;
}

Sig fold_shape(Sig h, const Pin_shape& s) {
  h = sig_u64(h, (static_cast<uint64_t>(s.pid) << 33U) | (static_cast<uint64_t>(static_cast<uint32_t>(s.bits)) << 1U)
                     | static_cast<uint64_t>(s.sign));
  return sig_u64(h, static_cast<uint64_t>(s.off));
}

struct Cone {
  hhds::Graph* g = nullptr;
  Node         root;
  // Members in forward_class (topological) order; the root is the last member.
  std::vector<Node> members;
  // Distinct non-const external driver pins, in deterministic first-use order.
  std::vector<Pin> leaves;
  std::vector<Sig> leaf_tok;  // final WL token per leaf (parallel to `leaves`)
  // Final per-member signatures (the verify walk's pairing key).
  absl::flat_hash_map<Node, Sig> sig;
  // The root's driven pins (deduped by pid, ascending) -- the pattern outputs.
  std::vector<Pin_shape> out_ports;
  Sig                    digest{};
  bool                   valid = false;
};

// A node may join a shared body only if it is a pure combinational PRIMITIVE
// and not woven into the per-site assert/assume machinery. Sub is excluded
// EXPLICITLY, not via is_loop_break: hhds re-stamps an instance of a fully
// combinational module as non-loop-break, and a Sub member would be a
// miscompile three ways -- the digest and the verify walk ignore the target
// module (two different comb modules with identical port shapes would bucket
// and pair), and the body clone would drop the subnode link entirely.
bool is_eligible(const Node& n) {
  if (!is_partitionable(n) || n.is_loop_break() || livehd::graph_util::type_op_of(n) == Ntype_op::Sub) {
    return false;
  }
  return !n.attr(livehd::attrs::runtime_check).has();
}

// Our own output defs. A rerun must never mine them: a pattern body re-digests
// to exactly the digest that names it, so with new sites elsewhere the bucket
// would splice the body into an INSTANCE OF ITSELF (and any splice inside a
// shared body would rewrite every site at once).
bool is_pattern_def_name(std::string_view name) {
  if (name.size() != 4 + 32 || !name.starts_with("pat_")) {
    return false;
  }
  for (char c : name.substr(4)) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

// The stable part of a member's identity: op, LUT function (when the node IS a
// LUT, the attr is the function), and the shape of every pin it drives.
Sig node_anchor(const Node& n) {
  Sig h = sig_seed(0xa2c40);
  h     = sig_u64(h, static_cast<uint64_t>(gu::type_op_of(n)));
  if (auto a = n.attr(livehd::attrs::lut); a.has()) {
    h = sig_str(h, std::string_view{a.get()});
  }
  std::vector<Pin_shape> pins;
  for (const auto& e : n.out_edges()) {
    const auto pid = static_cast<uint32_t>(e.driver.get_port_id());
    bool       dup = false;
    for (const auto& s : pins) {
      if (s.pid == pid) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      pins.push_back(shape_of(e.driver));
    }
  }
  std::sort(pins.begin(), pins.end());
  for (const auto& s : pins) {
    h = fold_shape(h, s);
  }
  return h;
}

// Const operand token: VALUE-BLIND on purpose. Loop unrolling produces cone
// after cone identical except for an index constant (`== 10'sh15c`, `15d`,
// ...); folding the value would split every iteration into its own bucket and
// hide exactly the repetition reduce exists to find. The value's storage SHAPE
// (bits, sign, unknowns) stays in the token: a slot whose values differ across
// occurrences is promoted to an input port, and a uniform shape is what makes
// that port's width/sign well-defined. Which values a slot actually held is
// folded into the pattern's NAME later (pattern_identity), so content
// addressing still holds.
Sig const_token(const Pin& d) {
  const auto v = gu::hydrate_const(d);
  Sig        h = sig_seed(0xc0157e2ULL);
  h            = sig_u64(h, static_cast<uint64_t>(v.get_bits()));
  h            = sig_u64(h, (v.is_negative() ? 2ULL : 0ULL) | (v.has_unknowns() ? 1ULL : 0ULL));
  return h;
}

// Leaf init token: width and sign only -- deliberately alpha-blind (no names,
// no nids), so identical shapes bucket across defs and across renames.
Sig leaf_init(const Pin& p) {
  return sig_u64(sig_seed(0x1eaf0),
                 (static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(p))) << 1U) | (gu::is_unsign(p) ? 0U : 1U));
}

// Signatures + digest for one cone. `cone_of`/`cid` supply the membership test.
// kRounds of member recompute with kRounds-1 leaf refinements in between: the
// Weisfeiler-Lehman refinement that separates leaves the cone uses differently.
void compute_signatures(Cone& k, const absl::flat_hash_map<Node, int32_t>& cone_of, int32_t cid) {
  constexpr int kRounds = 3;

  absl::flat_hash_map<Pin, size_t> leaf_ix;
  leaf_ix.reserve(k.leaves.size());
  for (size_t i = 0; i < k.leaves.size(); ++i) {
    leaf_ix.emplace(k.leaves[i], i);
  }
  std::vector<Sig> tok(k.leaves.size());
  for (size_t i = 0; i < k.leaves.size(); ++i) {
    tok[i] = leaf_init(k.leaves[i]);
  }

  auto is_member = [&](const Node& n) {
    auto it = cone_of.find(n);
    return it != cone_of.end() && it->second == cid;
  };

  absl::flat_hash_map<Node, Sig> sig;
  sig.reserve(k.members.size());
  for (int round = 0; round < kRounds; ++round) {
    sig.clear();
    for (const auto& n : k.members) {
      Sig                                        h = node_anchor(n);
      absl::flat_hash_map<int, std::vector<Sig>> by_port;
      for (const auto& e : n.inp_edges()) {
        const auto& d = e.driver;
        if (d.is_invalid()) {
          continue;
        }
        Sig t;
        if (gu::is_const_pin(d)) {
          t = const_token(d);
        } else if (auto m = d.get_master_node(); is_member(m)) {
          auto ms = sig.find(m);
          if (ms == sig.end()) {
            // Members are walked in forward_class order, which is topological
            // for combinational logic -- an unsigned in-cone driver means the
            // order is broken. Refuse rather than hash garbage.
            k.valid = false;
            return;
          }
          t = sig_u64(ms->second, static_cast<uint64_t>(static_cast<uint32_t>(d.get_port_id())));
        } else {
          t = tok[leaf_ix.at(d)];
        }
        by_port[static_cast<int>(e.sink.get_port_id())].push_back(t);
      }
      sig[n] = fold_operands(h, by_port);
    }
    if (round + 1 == kRounds) {
      break;
    }
    // Refine leaves from their consumers' fresh signatures.
    std::vector<std::vector<std::pair<Sig, uint32_t>>> uses(k.leaves.size());
    for (const auto& n : k.members) {
      for (const auto& e : n.inp_edges()) {
        if (auto it = leaf_ix.find(e.driver); it != leaf_ix.end()) {
          uses[it->second].emplace_back(sig.at(n), static_cast<uint32_t>(e.sink.get_port_id()));
        }
      }
    }
    for (size_t i = 0; i < tok.size(); ++i) {
      std::sort(uses[i].begin(), uses[i].end());
      Sig t = sig_comb(sig_seed(0x1eaf2), tok[i]);
      for (const auto& [s, p] : uses[i]) {
        t = sig_u64(sig_comb(t, s), p);
      }
      tok[i] = t;
    }
  }

  // Final fold: counts + sorted multisets. Nid allocation and walk order
  // cannot leak into the digest.
  std::vector<Sig> ms;
  ms.reserve(k.members.size());
  for (const auto& n : k.members) {
    ms.push_back(sig.at(n));
  }
  std::sort(ms.begin(), ms.end());
  std::vector<Sig> ls = tok;
  std::sort(ls.begin(), ls.end());

  Sig d = sig_seed(0x9edcce);
  d     = sig_u64(d, k.members.size());
  d     = sig_u64(d, k.leaves.size());
  d     = sig_u64(d, k.out_ports.size());
  for (const auto& s : ms) {
    d = sig_comb(d, s);
  }
  for (const auto& s : ls) {
    d = sig_comb(d, s);
  }
  for (const auto& s : k.out_ports) {
    d = fold_shape(d, s);
  }

  k.leaf_tok = std::move(tok);
  k.sig      = std::move(sig);
  k.digest   = d;
  k.valid    = true;
}

// ---------------------------------------------------------------------------
// Mining: disjoint fanout-free-cone decomposition of one def
// ---------------------------------------------------------------------------
// Rough cgen line cost of a cone's members: each node is one always_comb
// statement plus one wire decl; a Mux lowers to an if/else block (~5 lines)
// plus its decl. The instance that replaces the cone costs ports+2 lines. The
// extraction guard trades in THESE units -- the goal is smaller Verilog, and a
// node-count guard happily "wins" nodes while growing the text.
uint64_t est_verilog_lines(const Cone& k) {
  uint64_t l = 0;
  for (const auto& n : k.members) {
    l += gu::type_op_of(n) == Ntype_op::Mux ? 6 : 2;
  }
  return l;
}

// True when the cone touches a parallel duplicate edge (same driver pin AND
// same sink pin twice, e.g. `r+r` as two edges into one variadic port). The
// splice and the body build recreate such wiring edge-by-edge, and hhds's
// edge-slot overflow promotion DEDUPES parallel edges -- one of the two adds
// can silently vanish (`r+r` -> `r`). Refuse-not-guess: skip the cone.
bool has_dup_edge(const Cone& k) {
  absl::flat_hash_set<std::pair<Pin, Pin>> seen;
  for (const auto& n : k.members) {
    for (const auto& e : n.inp_edges()) {
      if (!e.driver.is_invalid() && !seen.insert({e.driver, e.sink}).second) {
        return true;
      }
    }
  }
  seen.clear();
  for (const auto& e : k.root.out_edges()) {
    if (!seen.insert({e.driver, e.sink}).second) {
      return true;
    }
  }
  return false;
}

void mine_def(hhds::Graph* g, const Reduce_opts& opts, std::vector<Cone>& out, Reduce_stats& st) {
  // forward_class position for every eligible node (also the membership set).
  absl::flat_hash_map<Node, uint32_t> pos;
  std::vector<Node>                   order;
  for (auto n : g->forward_class()) {
    if (is_eligible(n)) {
      pos.emplace(n, static_cast<uint32_t>(order.size()));
      order.push_back(n);
    }
  }
  if (order.empty()) {
    return;
  }

  // Sinks before drivers: a node whose fanout all lands on ONE eligible sink
  // joins that sink's cone; everything else (multi-sink fanout, or any reader
  // that is a flop/Sub/output/const-fed structure) roots its own cone. Every
  // eligible live node lands in exactly one cone => cones are disjoint.
  absl::flat_hash_map<Node, int32_t> cone_of;
  std::vector<std::vector<Node>>     members;
  std::vector<Node>                  roots;
  cone_of.reserve(order.size());
  for (size_t i = order.size(); i-- > 0;) {
    auto n    = order[i];
    bool root = false;
    bool any  = false;
    Node single{};
    for (const auto& e : n.out_edges()) {
      any     = true;
      auto sn = e.sink.get_master_node();
      if (!pos.contains(sn)) {
        root = true;
        break;
      }
      if (single.is_invalid()) {
        single = sn;
      } else if (!(single == sn)) {
        root = true;
        break;
      }
    }
    if (!any) {
      continue;  // dead node: nothing to extract, other passes clean it up
    }
    int32_t cid;
    if (root) {
      cid = static_cast<int32_t>(roots.size());
      roots.push_back(n);
      members.emplace_back();
    } else {
      auto it = cone_of.find(single);
      if (it == cone_of.end()) {
        continue;  // its only reader was dead -- so is this chain
      }
      cid = it->second;
    }
    cone_of.emplace(n, cid);
    members[cid].push_back(n);
  }

  for (size_t c = 0; c < roots.size(); ++c) {
    if (members[c].size() < opts.min_nodes) {
      continue;
    }
    Cone k;
    k.g       = g;
    k.root    = roots[c];
    k.members = std::move(members[c]);
    std::sort(k.members.begin(), k.members.end(), [&](const Node& x, const Node& y) { return pos.at(x) < pos.at(y); });

    // Leaves: distinct non-const external driver pins, first-use order.
    absl::flat_hash_set<Pin> seen;
    for (const auto& n : k.members) {
      for (const auto& e : n.inp_edges()) {
        const auto& d = e.driver;
        if (d.is_invalid() || gu::is_const_pin(d)) {
          continue;
        }
        if (auto it = cone_of.find(d.get_master_node()); it != cone_of.end() && it->second == static_cast<int32_t>(c)) {
          continue;
        }
        if (seen.insert(d).second) {
          k.leaves.push_back(d);
        }
      }
    }

    // Outputs: the root's driven pins, deduped by pid.
    for (const auto& e : k.root.out_edges()) {
      const auto pid = static_cast<uint32_t>(e.driver.get_port_id());
      bool       dup = false;
      for (const auto& s : k.out_ports) {
        if (s.pid == pid) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        k.out_ports.push_back(shape_of(e.driver));
      }
    }
    std::sort(k.out_ports.begin(), k.out_ports.end());
    if (k.out_ports.empty()) {
      continue;
    }
    if (has_dup_edge(k)) {
      ++st.dup_edge_skipped;
      continue;
    }

    compute_signatures(k, cone_of, static_cast<int32_t>(c));
    if (k.valid) {
      out.push_back(std::move(k));
    }
  }
}

// ---------------------------------------------------------------------------
// Verify: exact isomorphism walk, deriving the leaf correspondence
// ---------------------------------------------------------------------------

// Operand of one member, classified for pairing.
struct Opnd {
  int         kind = 0;  // 0 = const, 1 = leaf, 2 = member
  Sig         key{};     // pairing class: const shape token / leaf token / member sig
  std::string cval;      // kind 0: serialized value (exact compare + tie order)
  bool        cunk = false;  // kind 0: value carries unknown (x) bits
  Pin         pin;       // any kind: the driver pin
  Node        node;      // kind 2: the member
  Pin_shape   shape{};   // kinds 1,2: driver-pin shape (bits/sign/offset; pid for 2)
  uint64_t    tie = 0;   // deterministic tie-break inside equal keys (nid-based)
};

bool opnd_less(const Opnd& x, const Opnd& y) {
  return std::tie(x.kind, x.key, x.cval, x.shape, x.tie) < std::tie(y.kind, y.key, y.cval, y.shape, y.tie);
}

// One const OPERAND EDGE of the representative, addressed content-canonically:
// (member, sink pid, index in the group's sorted const prefix). The occurrence
// walks record their counterpart pin per slot by these coordinates, so the walk
// order never matters. After all occurrences verify, a slot whose values all
// agree stays an internal constant; a slot whose values differ becomes an
// input port fed per site.
struct Const_slot {
  Node        member;
  int         port = 0;
  int         j    = 0;  // index within the sorted const prefix of the group
  Pin         rep_pin;
  std::string rep_val;
};
// member -> (pid<<20 | j) -> global slot index. j and pid are tiny.
using Slot_index = absl::flat_hash_map<Node, absl::flat_hash_map<uint64_t, size_t>>;

uint64_t slot_key(int port, int j) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(port)) << 20U) | static_cast<uint64_t>(static_cast<uint32_t>(j));
}

struct Match {
  absl::flat_hash_map<Pin, Pin> leaf_a2b;       // representative leaf -> occurrence leaf
  std::vector<Pin>              occ_slot_pins;  // occurrence const pin per rep slot
  bool                          ok = false;
};

// Build the sorted, port-grouped operand lists of one member. Returns false on
// an operand the miner never classified (bail out). `ports` is the group keys
// in ASCENDING order -- pid order, never hash-map order, so every walk and the
// slot enumeration see one deterministic sequence.
bool operands_of(const Cone& K, const absl::flat_hash_map<Pin, Sig>& tok, const Node& n,
                 absl::flat_hash_map<int, std::vector<Opnd>>& by_port, std::vector<int>& ports) {
  for (const auto& e : n.inp_edges()) {
    const auto& d = e.driver;
    if (d.is_invalid()) {
      continue;
    }
    Opnd o;
    if (gu::is_const_pin(d)) {
      const auto v = gu::hydrate_const(d);
      o.kind       = 0;
      o.cval       = v.serialize();
      o.cunk       = v.has_unknowns();
      o.key        = const_token(d);
      o.pin        = d;
    } else if (auto mn = d.get_master_node(); K.sig.contains(mn)) {
      o.kind  = 2;
      o.key   = K.sig.at(mn);
      o.node  = mn;
      o.pin   = d;
      o.shape = shape_of(d);
      o.tie   = static_cast<uint64_t>(mn.get_debug_nid());
    } else {
      auto it = tok.find(d);
      if (it == tok.end()) {
        return false;  // an operand the miner never saw: bail out
      }
      o.kind      = 1;
      o.key       = it->second;
      o.pin       = d;
      o.shape     = shape_of(d);
      o.shape.pid = 0;  // a leaf's source pid is not part of the pattern
      o.tie       = (static_cast<uint64_t>(d.get_master_node().get_debug_nid()) << 16U)
            ^ static_cast<uint64_t>(static_cast<uint32_t>(d.get_port_id()));
    }
    by_port[static_cast<int>(e.sink.get_port_id())].push_back(std::move(o));
  }
  ports.clear();
  ports.reserve(by_port.size());
  for (auto& [p, vs] : by_port) {
    std::sort(vs.begin(), vs.end(), opnd_less);
    ports.push_back(p);
  }
  std::sort(ports.begin(), ports.end());
  return true;
}

absl::flat_hash_map<Pin, Sig> leaf_tok_map(const Cone& K) {
  absl::flat_hash_map<Pin, Sig> tok;
  tok.reserve(K.leaves.size());
  for (size_t i = 0; i < K.leaves.size(); ++i) {
    tok.emplace(K.leaves[i], K.leaf_tok[i]);
  }
  return tok;
}

// Canonical const-slot list of the representative: members in topo order, pids
// ascending, j over the sorted const prefix of each group. One slot per const
// EDGE (duplicate parallel edges were refused at mining).
std::vector<Const_slot> enumerate_const_slots(const Cone& K, Slot_index& ix) {
  std::vector<Const_slot> slots;
  const auto              tok = leaf_tok_map(K);
  for (const auto& n : K.members) {
    absl::flat_hash_map<int, std::vector<Opnd>> by_port;
    std::vector<int>                            ports;
    if (!operands_of(K, tok, n, by_port, ports)) {
      continue;  // unreachable for a mined cone; keep enumeration total
    }
    for (int p : ports) {
      const auto& vs = by_port[p];
      for (size_t i = 0; i < vs.size() && vs[i].kind == 0; ++i) {
        ix[n][slot_key(p, static_cast<int>(i))] = slots.size();
        slots.push_back({n, p, static_cast<int>(i), vs[i].pin, vs[i].cval});
      }
    }
  }
  return slots;
}

// Exact structural-identity check of cone B against representative A. Any
// accepted pairing is a witnessed isomorphism: symmetric ties are paired
// arbitrarily (sorted order) and a wrong guess simply fails the walk, so the
// occurrence is dropped rather than mis-stitched (refuse-not-guess). Const
// operands may DIFFER in value (never in shape, and never with unknown bits on
// either side): the divergence is recorded per slot for the parameterization
// decision, not rejected.
Match match_cones(const Cone& A, const Cone& B, const Slot_index& slot_ix, size_t n_slots) {
  Match m;
  if (A.members.size() != B.members.size() || A.leaves.size() != B.leaves.size() || !(A.out_ports == B.out_ports)) {
    return m;
  }

  const auto tokA = leaf_tok_map(A);
  const auto tokB = leaf_tok_map(B);

  absl::flat_hash_map<Node, Node> a2b, b2a;
  absl::flat_hash_map<Pin, Pin>   la2b, lb2a;

  auto pair_nodes = [&](const Node& na, const Node& nb) -> int {
    // Returns: 1 = freshly paired (walk it), 0 = already consistently paired,
    // -1 = contradiction.
    auto ia = a2b.find(na);
    auto ib = b2a.find(nb);
    if (ia != a2b.end() || ib != b2a.end()) {
      return (ia != a2b.end() && ia->second == nb && ib != b2a.end() && ib->second == na) ? 0 : -1;
    }
    a2b.emplace(na, nb);
    b2a.emplace(nb, na);
    return 1;
  };

  auto pair_leaves = [&](const Pin& pa, const Pin& pb) -> bool {
    auto ia = la2b.find(pa);
    auto ib = lb2a.find(pb);
    if (ia != la2b.end() || ib != lb2a.end()) {
      return ia != la2b.end() && ia->second == pb && ib != lb2a.end() && ib->second == pa;
    }
    la2b.emplace(pa, pb);
    lb2a.emplace(pb, pa);
    return true;
  };

  std::vector<Pin> occ_slots(n_slots);
  size_t           slots_seen = 0;

  std::vector<std::pair<Node, Node>> work;
  if (pair_nodes(A.root, B.root) != 1) {
    return m;
  }
  work.emplace_back(A.root, B.root);

  while (!work.empty()) {
    auto [na, nb] = work.back();
    work.pop_back();

    if (gu::type_op_of(na) != gu::type_op_of(nb)) {
      return m;
    }
    {
      auto  la = na.attr(livehd::attrs::lut);
      auto  lb = nb.attr(livehd::attrs::lut);
      if (la.has() != lb.has() || (la.has() && std::string_view{la.get()} != std::string_view{lb.get()})) {
        return m;
      }
    }

    absl::flat_hash_map<int, std::vector<Opnd>> pa, pb;
    std::vector<int>                            ports_a, ports_b;
    if (!operands_of(A, tokA, na, pa, ports_a) || !operands_of(B, tokB, nb, pb, ports_b)) {
      return m;
    }
    if (ports_a != ports_b) {
      return m;
    }
    for (int port : ports_a) {
      auto& va = pa[port];
      auto& vb = pb[port];
      if (va.size() != vb.size()) {
        return m;
      }
      for (size_t i = 0; i < va.size(); ++i) {
        const auto& oa = va[i];
        const auto& ob = vb[i];
        if (oa.kind != ob.kind) {
          return m;
        }
        switch (oa.kind) {
          case 0: {
            if (!(oa.key == ob.key)) {
              return m;  // shape mismatch (bits/sign/unknowns)
            }
            if (oa.cval != ob.cval && (oa.cunk || ob.cunk)) {
              return m;  // x-carrying values only match verbatim
            }
            auto sit = slot_ix.find(na);
            if (sit == slot_ix.end()) {
              return m;
            }
            auto kit = sit->second.find(slot_key(port, static_cast<int>(i)));
            if (kit == sit->second.end()) {
              return m;
            }
            if (!occ_slots[kit->second].is_invalid()) {
              return m;  // a slot visited twice: walk/enumeration desync
            }
            occ_slots[kit->second] = ob.pin;
            ++slots_seen;
            break;
          }
          case 1:
            if (!(oa.shape == ob.shape) || !pair_leaves(oa.pin, ob.pin)) {
              return m;
            }
            break;
          default:
            if (!(oa.shape == ob.shape)) {
              return m;
            }
            if (int r = pair_nodes(oa.node, ob.node); r < 0) {
              return m;
            } else if (r == 1) {
              work.emplace_back(oa.node, ob.node);
            }
            break;
        }
      }
    }
  }

  // Coverage: the walk from the root must have claimed every member, every
  // leaf, and every const slot of both cones -- a digest collision that
  // survives to here still fails.
  if (a2b.size() != A.members.size() || la2b.size() != A.leaves.size() || slots_seen != n_slots) {
    return m;
  }

  m.leaf_a2b      = std::move(la2b);
  m.occ_slot_pins = std::move(occ_slots);
  m.ok            = true;
  return m;
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

struct Port_plan {
  // Representative leaf indices in canonical (token, first-use) rank order,
  // then PROMOTED const slots in canonical slot order. GraphIO port ids:
  // leaf rank r <-> r+1; const port c <-> L+1+c; output j <-> L+C+1+j.
  std::vector<size_t> leaf_rank;
  std::vector<size_t> const_ports;  // promoted slot indices
};

Port_plan plan_ports(const Cone& rep) {
  Port_plan plan;
  plan.leaf_rank.resize(rep.leaves.size());
  for (size_t i = 0; i < rep.leaves.size(); ++i) {
    plan.leaf_rank[i] = i;
  }
  std::sort(plan.leaf_rank.begin(), plan.leaf_rank.end(), [&](size_t x, size_t y) {
    if (!(rep.leaf_tok[x] == rep.leaf_tok[y])) {
      return rep.leaf_tok[x] < rep.leaf_tok[y];
    }
    return x < y;  // first-use order breaks token ties deterministically
  });
  return plan;
}

void carry_driver_attrs(const Pin& orig, const Pin& neo) {
  if (auto b = gu::bits_of(orig); b != 0) {
    gu::set_bits(neo, b);
  }
  if (!gu::is_unsign(orig)) {
    gu::set_sign(neo);
  }
  if (auto pn = gu::pin_name_of(orig); !pn.empty()) {
    gu::set_pin_name(neo, pn);
  }
  if (auto o = orig.attr(livehd::attrs::pin_offset); o.has()) {
    neo.attr(livehd::attrs::pin_offset).set(o.get());
  }
}

// Build the shared pattern def from the representative cone. Returns the
// GraphIO, or nullptr after a fatal diag.
std::shared_ptr<hhds::GraphIO> build_pattern_def(hhds::GraphLibrary* lib, const std::string& name, const Cone& rep,
                                                 const Port_plan& plan, const std::vector<Const_slot>& slots,
                                                 Reduce_stats& st) {
  auto gio = lib->create_io(name);

  hhds::Port_id pid = 1;
  for (size_t r = 0; r < plan.leaf_rank.size(); ++r) {
    const auto& leaf = rep.leaves[plan.leaf_rank[r]];
    auto        nm   = std::format("i{}", r);
    gio->add_input(nm, pid++);
    if (auto b = gu::bits_of(leaf); b != 0) {
      gio->set_bits(nm, static_cast<uint32_t>(b));
    }
    gio->set_unsign(nm, gu::is_unsign(leaf));
  }
  // Promoted const slots: the port shape comes from the VALUE class (all sites
  // share it -- the shape is folded into the const token). +1 bit is headroom
  // that can never truncate; sign follows the value.
  std::vector<std::pair<uint32_t, bool>> cshape(plan.const_ports.size());
  for (size_t c = 0; c < plan.const_ports.size(); ++c) {
    const auto v = gu::hydrate_const(slots[plan.const_ports[c]].rep_pin);
    cshape[c]    = {static_cast<uint32_t>(v.get_bits()) + 1U, !v.is_negative()};
    auto nm      = std::format("c{}", c);
    gio->add_input(nm, pid++);
    gio->set_bits(nm, cshape[c].first);
    gio->set_unsign(nm, cshape[c].second);
  }
  for (size_t j = 0; j < rep.out_ports.size(); ++j) {
    auto nm = std::format("o{}", j);
    gio->add_output(nm, pid++);
    if (rep.out_ports[j].bits != 0) {
      gio->set_bits(nm, static_cast<uint32_t>(rep.out_ports[j].bits));
    }
    gio->set_unsign(nm, !rep.out_ports[j].sign);
  }

  auto body = gio->create_graph();

  // Stamp bits/sign on the materialized body input pins: the GraphIO decl is
  // not auto-propagated to the pin attrs, and readers size ports from the PIN
  // attr (the pass.partition / tolg invariant).
  for (size_t r = 0; r < plan.leaf_rank.size(); ++r) {
    const auto& leaf = rep.leaves[plan.leaf_rank[r]];
    auto        ip   = body->get_input_pin(std::format("i{}", r));
    if (auto b = gu::bits_of(leaf); b != 0) {
      gu::set_bits(ip, b);
    }
    gu::is_unsign(leaf) ? gu::set_unsign(ip) : gu::set_sign(ip);
  }
  for (size_t c = 0; c < plan.const_ports.size(); ++c) {
    auto ip = body->get_input_pin(std::format("c{}", c));
    gu::set_bits(ip, static_cast<int32_t>(cshape[c].first));
    cshape[c].second ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  absl::flat_hash_map<Pin, std::string> leaf_port;
  for (size_t r = 0; r < plan.leaf_rank.size(); ++r) {
    leaf_port.emplace(rep.leaves[plan.leaf_rank[r]], std::format("i{}", r));
  }

  // Clone the members (comb only -- no Sub/state handling by construction).
  absl::flat_hash_map<Node, Node> node_map;
  node_map.reserve(rep.members.size());
  for (const auto& n : rep.members) {
    auto neo = gu::create_typed_node(*body, gu::type_op_of(n));
    if (gu::has_name(n)) {
      neo.attr(hhds::attrs::name).set(std::string{gu::node_name_of(n)});
    }
    if (auto a = n.attr(livehd::attrs::lut); a.has()) {
      neo.attr(livehd::attrs::lut).set(std::string{a.get()});
    }
    if (auto a = n.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
      neo.attr(hhds::attrs::srcid).set(body->source_locator().import_from(rep.g->source_locator(), a.get()));
    }
    node_map.emplace(n, neo);
    ++st.nodes_created;
  }

  // Wire the non-const operands: internal edges by clone, leaves from the
  // canonical input ports. Const operands are wired from the SLOT list below
  // (one edge per slot), so the promoted-vs-internal decision has a single
  // authority.
  for (const auto& n : rep.members) {
    auto neo = node_map.at(n);
    for (const auto& e : n.inp_edges()) {
      const auto& d = e.driver;
      if (d.is_invalid() || gu::is_const_pin(d)) {
        continue;
      }
      auto sp = neo.create_sink_pin(e.sink.get_port_id());
      if (auto mit = node_map.find(d.get_master_node()); mit != node_map.end()) {
        auto dp = mit->second.create_driver_pin(d.get_port_id());
        carry_driver_attrs(d, dp);
        dp.connect_sink(sp);
      } else {
        body->get_input_pin(leaf_port.at(d)).connect_sink(sp);
      }
    }
  }
  // Const slots: internal ones are recreated (once per source pin -- not
  // counted in nodes_created, create_const materializes a PIN on the builtin
  // const node); promoted ones read their c-port.
  {
    absl::flat_hash_map<size_t, size_t> slot2cport;
    for (size_t c = 0; c < plan.const_ports.size(); ++c) {
      slot2cport.emplace(plan.const_ports[c], c);
    }
    absl::flat_hash_map<Pin, Pin> const_map;
    for (size_t s = 0; s < slots.size(); ++s) {
      auto sp = node_map.at(slots[s].member).create_sink_pin(static_cast<hhds::Port_id>(slots[s].port));
      if (auto it = slot2cport.find(s); it != slot2cport.end()) {
        body->get_input_pin(std::format("c{}", it->second)).connect_sink(sp);
      } else {
        auto cit = const_map.find(slots[s].rep_pin);
        if (cit == const_map.end()) {
          cit = const_map.emplace(slots[s].rep_pin, gu::create_const(*body, gu::hydrate_const(slots[s].rep_pin))).first;
        }
        cit->second.connect_sink(sp);
      }
    }
  }
  for (size_t j = 0; j < rep.out_ports.size(); ++j) {
    auto dp = node_map.at(rep.root).create_driver_pin(static_cast<hhds::Port_id>(rep.out_ports[j].pid));
    // Shape from the plan; the wire NAME stays per-site (each occurrence keeps
    // its own on the instance pin), so the body pin gets no pin_name.
    if (rep.out_ports[j].bits != 0) {
      gu::set_bits(dp, rep.out_ports[j].bits);
    }
    if (rep.out_ports[j].sign) {
      gu::set_sign(dp);
    }
    if (rep.out_ports[j].off != 0) {
      dp.attr(livehd::attrs::pin_offset).set(rep.out_ports[j].off);
    }
    dp.connect_sink(body->get_output_pin(std::format("o{}", j)));
  }

  body->commit();
  return gio;
}

// Per-def forwarding of spliced-away root pins: when cone C1's root fed cone
// C2 and C1 is spliced first, C2's recorded leaf pin is gone -- its live
// replacement is C1's instance output.
using Fwd = absl::flat_hash_map<Pin, Pin>;

Pin follow(const Fwd& fwd, Pin p) {
  for (auto it = fwd.find(p); it != fwd.end(); it = fwd.find(p)) {
    p = it->second;
  }
  return p;
}

// Replace one occurrence with an instance of the pattern def. `match` is
// nullptr for the representative (identity correspondence).
void splice(const Cone& rep, const Cone& occ, const Match* match, const Port_plan& plan,
            const std::vector<Const_slot>& slots, const std::shared_ptr<hhds::GraphIO>& gio, Fwd& fwd,
            Reduce_stats& st) {
  auto* g = occ.g;

  // Snapshot the root's readers (and its driver pins) before any mutation:
  // out_edges() is a lazy view and the loop below deletes the node under it.
  struct Reader {
    uint32_t pid;
    Pin      driver;
    Pin      sink;
  };
  std::vector<Reader> readers;
  for (const auto& e : occ.root.out_edges()) {
    readers.push_back({static_cast<uint32_t>(e.driver.get_port_id()), e.driver, e.sink});
  }

  // Const nodes the cone consumes: candidates for the dangling sweep below.
  absl::flat_hash_set<Node> const_masters;
  for (const auto& n : occ.members) {
    for (const auto& e : n.inp_edges()) {
      if (gu::is_const_pin(e.driver)) {
        if (auto cm = e.driver.get_master_node(); gu::type_op_of(cm) == Ntype_op::Nconst && !gu::is_builtin_node(cm)) {
          const_masters.insert(cm);
        }
      }
    }
  }

  // The instance. Anonymous on purpose (partition's hier-name transparency
  // convention -- cgen synthesizes a stable u_<module> at emit), and NOT
  // colored: color ids are per-def region ids that pass.partition consumes,
  // and a fresh 1..P stamp would collide with whatever coloring the graphs
  // already carry. Pattern identity is the instance's target def name (pat_*).
  auto sub = gu::create_typed_node(*g, Ntype_op::Sub);
  sub.set_subnode(gio);
  ++st.nodes_created;
  ++st.occurrences;

  // Inputs: canonical rank r binds this occurrence's counterpart of the
  // representative's rank-r leaf.
  for (size_t r = 0; r < plan.leaf_rank.size(); ++r) {
    const auto& rep_leaf = rep.leaves[plan.leaf_rank[r]];
    Pin         src      = match == nullptr ? rep_leaf : match->leaf_a2b.at(rep_leaf);
    src                  = follow(fwd, src);
    src.connect_sink(sub.create_sink_pin(static_cast<hhds::Port_id>(r + 1)));
  }
  // Promoted const slots: each site feeds its OWN value (the site's const pin
  // still exists -- the dangling sweep below sees its new edge and keeps it).
  for (size_t c = 0; c < plan.const_ports.size(); ++c) {
    const auto s   = plan.const_ports[c];
    Pin        src = match == nullptr ? slots[s].rep_pin : match->occ_slot_pins[s];
    src.connect_sink(sub.create_sink_pin(static_cast<hhds::Port_id>(plan.leaf_rank.size() + 1 + c)));
  }

  // Outputs: rewire every reader of the old root to the instance, carrying the
  // occurrence's own wire name/shape so downstream naming does not shift.
  for (size_t j = 0; j < rep.out_ports.size(); ++j) {
    const auto pid = rep.out_ports[j].pid;
    auto       dp =
        sub.create_driver_pin(static_cast<hhds::Port_id>(plan.leaf_rank.size() + plan.const_ports.size() + 1 + j));
    bool       first = true;
    for (const auto& rd : readers) {
      if (rd.pid != pid) {
        continue;
      }
      if (first) {
        carry_driver_attrs(rd.driver, dp);
        fwd[rd.driver] = dp;
        first          = false;
      }
      dp.connect_sink(rd.sink);
    }
  }

  for (const auto& n : occ.members) {
    n.del_node();
    ++st.nodes_deleted;
  }
  // Consts whose only readers were the cone are dangling now.
  for (const auto& cm : const_masters) {
    bool used = false;
    for (const auto& e : cm.out_edges()) {
      (void)e;
      used = true;
      break;
    }
    if (!used) {
      cm.del_node();
      ++st.nodes_deleted;
    }
  }
}

}  // namespace

bool color_reduce(std::span<hhds::Graph* const> defs, const Reduce_opts& opts, Reduce_stats* stp) {
  Reduce_stats  local;
  Reduce_stats& st = stp == nullptr ? local : *stp;
  if (defs.empty()) {
    return true;
  }
  auto* lib = defs.front()->get_io() ? defs.front()->get_io()->get_library() : nullptr;
  for (auto* g : defs) {
    if ((g->get_io() ? g->get_io()->get_library() : nullptr) != lib) {
      livehd::diag::err("pass.color", "reduce-multi-library", "internal")
          .msg("color reduce: defs span multiple graph libraries (gids are library-scoped)")
          .fatal();
      return false;
    }
  }

  // -------- mine --------
  std::vector<Cone> cones;
  for (auto* g : defs) {
    if (is_pattern_def_name(g->get_name())) {
      continue;  // our own outputs are terminal (see is_pattern_def_name)
    }
    if (has_seeded_coloring(g)) {
      // Block-attr seeded colors are the user's explicit partition hints;
      // carving shared bodies out of them would rewrite what the source pinned.
      ++st.defs_skipped_seeded;
      continue;
    }
    ++st.defs_scanned;
    mine_def(g, opts, cones, st);
  }
  st.cones = cones.size();

  // -------- bucket --------
  absl::flat_hash_map<std::pair<uint64_t, uint64_t>, std::vector<Cone>> buckets;
  for (auto& k : cones) {
    buckets[{k.digest.a, k.digest.b}].push_back(std::move(k));
  }
  cones.clear();

  std::vector<std::pair<uint64_t, uint64_t>> keys;
  for (auto& [key, occs] : buckets) {
    if (occs.size() >= opts.min_count) {
      keys.push_back(key);
    } else {
      occs.clear();  // free the signature maps early: most cones are unique
      occs.shrink_to_fit();
    }
  }
  std::sort(keys.begin(), keys.end());

  // -------- verify + extract --------
  struct Job {
    std::vector<Cone>       occs;     // [0] = representative
    std::vector<Match>      matches;  // per non-rep occurrence
    std::vector<Const_slot> slots;    // representative const slots, canonical order
    std::vector<bool>       promoted; // per slot: values diverge -> input port
    Sig                     identity{};  // structural digest + slot decisions/values
  };
  std::vector<Job> jobs;
  for (const auto& key : keys) {
    auto& occs = buckets[key];
    std::sort(occs.begin(), occs.end(), [](const Cone& x, const Cone& y) {
      auto xn = x.g->get_name();
      auto yn = y.g->get_name();
      if (xn != yn) {
        return xn < yn;
      }
      return x.root.get_debug_nid() < y.root.get_debug_nid();
    });

    // Text-profit guard, conservative half: the instance costs at least
    // leaves+outs+2 lines (promoted consts only add). Not enough per-site win
    // => not worth a shared module. Re-checked after the const decision below.
    if (opts.min_win != 0
        && est_verilog_lines(occs.front())
               < occs.front().leaves.size() + occs.front().out_ports.size() + 2 + opts.min_win) {
      ++st.port_heavy_skipped;
      continue;
    }

    Job job;
    job.occs.reserve(occs.size());  // `rep` stays valid across the push_backs
    job.occs.push_back(std::move(occs.front()));
    const auto& rep = job.occs.front();
    Slot_index  slot_ix;
    job.slots = enumerate_const_slots(rep, slot_ix);
    for (size_t i = 1; i < occs.size(); ++i) {
      auto m = match_cones(rep, occs[i], slot_ix, job.slots.size());
      if (!m.ok) {
        ++st.verify_dropped;
        continue;
      }
      job.occs.push_back(std::move(occs[i]));
      job.matches.push_back(std::move(m));
    }
    occs.clear();
    if (job.occs.size() < opts.min_count) {
      continue;
    }

    // Const decision: a slot whose values agree across every site stays an
    // internal constant; a divergent slot becomes an input port.
    job.promoted.assign(job.slots.size(), false);
    for (size_t s = 0; s < job.slots.size(); ++s) {
      for (const auto& m : job.matches) {
        if (gu::hydrate_const(m.occ_slot_pins[s]).serialize() != job.slots[s].rep_val) {
          job.promoted[s] = true;
          break;
        }
      }
    }
    size_t n_promoted = 0;
    // The pattern's identity refines the value-blind digest with the slot
    // decisions and the internal values, so the content-addressed name still
    // uniquely determines body AND interface.
    Sig id = sig_comb(sig_seed(0x9a77e51d), rep.digest);
    id     = sig_u64(id, job.slots.size());
    for (size_t s = 0; s < job.slots.size(); ++s) {
      if (job.promoted[s]) {
        id = sig_u64(id, 1);
        ++n_promoted;
      } else {
        id = sig_str(sig_u64(id, 0), job.slots[s].rep_val);
      }
    }
    job.identity = id;

    if (opts.min_win != 0
        && est_verilog_lines(rep) < rep.leaves.size() + n_promoted + rep.out_ports.size() + 2 + opts.min_win) {
      ++st.port_heavy_skipped;
      continue;
    }
    st.promoted_consts += n_promoted;
    jobs.push_back(std::move(job));
  }

  // Pattern bodies are built FIRST, all of them, from the still-pristine
  // graphs: build_pattern_def re-walks the representative's live edges, and a
  // splice of pattern P can rewire a leaf of pattern Q's representative (Q's
  // recorded leaf pin was P's extracted root). Splicing only reads RECORDED
  // pins (plus the forwarding map for exactly that rewire), so once every body
  // exists the splice order is free.
  struct Ready {
    Job*                           job;
    Port_plan                      plan;
    std::shared_ptr<hhds::GraphIO> gio;
  };
  std::vector<Ready> ready;
  ready.reserve(jobs.size());
  for (auto& job : jobs) {
    const auto& rep  = job.occs.front();
    auto        plan = plan_ports(rep);
    for (size_t s = 0; s < job.slots.size(); ++s) {
      if (job.promoted[s]) {
        plan.const_ports.push_back(s);
      }
    }
    const auto name = std::format("pat_{:016x}{:016x}", job.identity.a, job.identity.b);

    auto gio = lib->find_io(name);
    if (gio) {
      // Iterative rerun: the def is content-addressed, so an existing body IS
      // this pattern -- but check the interface before trusting a 128-bit name.
      if (gio->get_input_pin_decls().size() != plan.leaf_rank.size() + plan.const_ports.size()
          || gio->get_output_pin_decls().size() != rep.out_ports.size()) {
        livehd::diag::err("pass.color", "reduce-pattern-collision", "internal")
            .msg("existing def '{}' does not match the pattern identity that names it ({} in / {} out expected)",
                 name,
                 plan.leaf_rank.size() + plan.const_ports.size(),
                 rep.out_ports.size())
            .fatal();
        return false;
      }
      // Reuse binds this run's canonical ranks to the existing def's ports,
      // which is content-forced ONLY when every binding is: tied leaf tokens
      // could bind roles differently than the run that built the body, and a
      // const-port ORDER depends on the builder's representative. The walk
      // witnessed this run's rep against its occurrences, never against the
      // existing body -- refuse-not-guess.
      bool fragile = !plan.const_ports.empty();
      for (size_t r = 1; !fragile && r < plan.leaf_rank.size(); ++r) {
        fragile = rep.leaf_tok[plan.leaf_rank[r - 1]] == rep.leaf_tok[plan.leaf_rank[r]];
      }
      if (fragile) {
        ++st.reuse_refused;
        continue;
      }
    } else {
      gio = build_pattern_def(lib, name, rep, plan, job.slots, st);
      if (!gio) {
        return false;
      }
    }

    ++st.patterns;
    if (opts.verbose) {
      std::print(stderr,
                 "[color.reduce] pattern {} ({} nodes, {} in / {} const / {} out) x {} sites\n",
                 name,
                 rep.members.size(),
                 plan.leaf_rank.size(),
                 plan.const_ports.size(),
                 rep.out_ports.size(),
                 job.occs.size());
    }
    ready.push_back({&job, std::move(plan), std::move(gio)});
  }

  // Parent-def mutation starts here.
  absl::flat_hash_map<hhds::Graph*, Fwd> fwd;
  for (auto& r : ready) {
    const auto& rep = r.job->occs.front();
    for (size_t i = 0; i < r.job->occs.size(); ++i) {
      const auto& occ = r.job->occs[i];
      splice(rep, occ, i == 0 ? nullptr : &r.job->matches[i - 1], r.plan, r.job->slots, r.gio, fwd[occ.g], st);
    }
  }

  return true;
}

}  // namespace livehd::color
