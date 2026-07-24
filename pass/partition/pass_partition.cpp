// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_partition.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "color_common.hpp"
#include "diag.hpp"
#include "flatten.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "str_tools.hpp"

using namespace livehd::graph_util;  // type_op_of, node_color_of, is_const_pin, ...
using livehd::color::is_partitionable;
using livehd::color::NO_COLOR;

static Pass_plugin sample("pass_partition", Pass_partition::setup);

Pass_partition::Pass_partition(const Eprp_var& var) : Pass("pass.partition", var) {}

void Pass_partition::setup() {
  Eprp_method m("pass.partition", "Build a new graph_library from the active coloring (one module per region)", &Pass_partition::partition);
  // The top module is the shared kernel `--top` flag (lhd plumbs it into the
  // `top` label), not a per-pass --set option.
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  m.add_label_optional("debug_color", "diagnose same-color region interface mismatches", "false");
  m.add_label_optional("flatten",
                       "auto|true|false whole-design flatten: inline the instance hierarchy and partition the flat "
                       "design as one def (auto = flatten exactly when the active coloring is `pass.color flat`); a "
                       "single resulting region is emitted directly under the top name — one output module",
                       "auto");
  register_pass(m);
}

namespace {

namespace gu = livehd::graph_util;

using livehd::color::Union_find;  // region = connected component of same color

std::string sanitize(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
  }
  if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) {
    out.insert(out.begin(), '_');
  }
  return out;
}

// ---------------------------------------------------------------------------
// Nid-free content signature for STABLE boundary port names (task 2a-incr).
//
// A boundary port becomes a wire the freshly rebuilt wrapper stitches BY NAME,
// so a cached region body can only be reused if the port name is reproducible
// across recompiles. wire_name falls back to `<op>_<nid>`, which renumbers on
// any edit; instead hash the pin's producer cone down to STABLE anchors
// (declared graph inputs by name, user-named nodes/pins, constants by value),
// folding operands by sink-port so commutative inputs agree while
// non-commutative ones stay distinct. Memoized per name_ports() call so
// reconvergent fan-in is walked once. The exact hash does not affect
// correctness (a collision only costs a conservative miss), so a simple mix is
// fine.
// ---------------------------------------------------------------------------
uint64_t sig_mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
  return h;
}
uint64_t sig_str(uint64_t h, std::string_view s) {
  for (unsigned char c : s) {
    h = sig_mix(h, c);
  }
  return h;
}

uint64_t cone_sig(const hhds::Pin_class& pin, absl::flat_hash_map<hhds::Pin_class, uint64_t>& memo,
                  absl::flat_hash_set<hhds::Node_class>& on_path, int depth) {
  if (auto it = memo.find(pin); it != memo.end()) {
    return it->second;
  }
  constexpr uint64_t kSeed = 0xcbf29ce484222325ULL;
  if (gu::is_const_pin(pin)) {  // constant: its value is its identity
    uint64_t h = sig_str(sig_mix(kSeed, 1), gu::hydrate_const(pin).serialize());
    memo[pin]  = h;
    return h;
  }
  if (gu::is_graph_input_pin(pin)) {  // declared IO: its name is its identity
    uint64_t h = sig_str(sig_mix(kSeed, 2), gu::pin_name_of(pin));
    memo[pin]  = h;
    return h;
  }
  if (auto pn = gu::pin_name_of(pin); !pn.empty()) {  // named wire: stop here
    uint64_t h = sig_str(sig_mix(kSeed, 3), pn);
    memo[pin]  = h;
    return h;
  }
  auto master = pin.get_master_node();
  if (gu::has_name(master)) {  // named node (register/wire): a stable anchor
    uint64_t h = sig_mix(sig_str(sig_mix(kSeed, 4), gu::node_name_of(master)), static_cast<uint64_t>(pin.get_port_id()));
    memo[pin]  = h;
    return h;
  }
  // Anonymous internal node: op + width/sign + a commutative-safe fold of the
  // input-driver cones (each child mixed with its sink port_id, then sorted, so
  // operand order is irrelevant for commutative ops but preserved for the rest).
  uint64_t node = sig_mix(sig_mix(kSeed, 6), static_cast<uint64_t>(type_op_of(master)));
  node          = sig_mix(node, (static_cast<uint64_t>(static_cast<uint32_t>(gu::bits_of(pin))) << 1U)
                                    | static_cast<uint64_t>(gu::is_unsign(pin) ? 0U : 1U));
  // A cycle or depth cap terminates at this coarse op/shape anchor (not
  // memoized, since it is path-dependent); regions that hit it stay reuse-safe
  // via a conservative miss, never a wrong reuse.
  if (depth <= 0 || on_path.contains(master)) {
    return sig_mix(node, 0x9e37U);
  }
  on_path.insert(master);
  std::vector<uint64_t> ops;
  for (const auto& e : master.inp_edges()) {
    uint64_t child = cone_sig(e.driver, memo, on_path, depth - 1);
    ops.push_back(sig_mix(child, static_cast<uint64_t>(e.sink.get_port_id())));
  }
  std::sort(ops.begin(), ops.end());
  for (uint64_t o : ops) {
    node = sig_mix(node, o);
  }
  node = sig_mix(node, static_cast<uint64_t>(pin.get_port_id()));  // which output pin of the node
  on_path.erase(master);
  memo[pin] = node;
  return node;
}

// Forward (CONSUMER-side) analogue of cone_sig (Proposal 2, bidirectional
// canonical labeling): hashes how a driver's signal is USED downstream, so two
// boundary inputs with identical PRODUCER cones but different consumer roles --
// e.g. two replay-queue lanes that write DIFFERENT bit ranges of one packed
// conflict-matrix flop -- get distinct signatures. cone_sig alone (producer only)
// ties them, which is a FALSE tie: they are distinct external nets, so a by-name
// reuse that swaps them transposes the state (LEC-refuted on
// minion_dcache_replay_queue). Anchors on graph outputs (by name), named
// nodes/state (by name), and folds each consumer's op + CONSTANT operands (the
// Get_mask/Set_mask bit ranges that pick the lane) + sink port. Reproducible
// (nid-free) and symmetric to cone_sig, so old/new recompiles agree.
uint64_t fwd_cone_sig(const hhds::Pin_class& driver, absl::flat_hash_map<hhds::Pin_class, uint64_t>& memo,
                      absl::flat_hash_set<hhds::Node_class>& on_path, int depth) {
  if (auto it = memo.find(driver); it != memo.end()) {
    return it->second;
  }
  constexpr uint64_t kSeed = 0x84222325cbf29ce4ULL;
  if (depth <= 0) {
    return sig_mix(kSeed, 0x5a17U);  // depth cap: coarse, path-dependent, not memoized
  }
  std::vector<uint64_t> uses;
  for (const auto& e : driver.out_edges()) {
    const auto& snk = e.sink;
    uint64_t    u;
    if (gu::is_graph_output_pin(snk)) {  // declared IO consumer: name is its identity
      u = sig_str(sig_mix(kSeed, 1), gu::pin_name_of(snk));
    } else {
      auto cm = snk.get_master_node();
      if (gu::has_name(cm)) {  // named consumer (register/wire/state): a stable anchor
        u = sig_mix(sig_str(sig_mix(kSeed, 2), gu::node_name_of(cm)), static_cast<uint64_t>(snk.get_port_id()));
      } else if (on_path.contains(cm)) {  // cycle: coarse op anchor
        u = sig_mix(sig_mix(kSeed, 3), static_cast<uint64_t>(type_op_of(cm)));
      } else {
        on_path.insert(cm);
        // consumer op + which sink port we feed + its CONSTANT operands (the
        // masks that select the lane) + a fold of where its outputs go.
        uint64_t cnode = sig_mix(sig_mix(kSeed, 4), static_cast<uint64_t>(type_op_of(cm)));
        cnode          = sig_mix(cnode, static_cast<uint64_t>(snk.get_port_id()));
        std::vector<uint64_t> cin;
        for (const auto& ie : cm.inp_edges()) {
          if (gu::is_const_pin(ie.driver)) {
            cin.push_back(sig_mix(sig_str(sig_mix(kSeed, 5), gu::hydrate_const(ie.driver).serialize()),
                                  static_cast<uint64_t>(ie.sink.get_port_id())));
          }
        }
        std::sort(cin.begin(), cin.end());
        for (uint64_t c : cin) {
          cnode = sig_mix(cnode, c);
        }
        std::vector<uint64_t>    outs;
        absl::flat_hash_set<int> seen;
        for (const auto& oe : cm.out_edges()) {
          if (seen.insert(static_cast<int>(oe.driver.get_port_id())).second) {
            outs.push_back(fwd_cone_sig(oe.driver, memo, on_path, depth - 1));
          }
        }
        std::sort(outs.begin(), outs.end());
        for (uint64_t o : outs) {
          cnode = sig_mix(cnode, o);
        }
        on_path.erase(cm);
        u = cnode;
      }
    }
    uses.push_back(u);
  }
  std::sort(uses.begin(), uses.end());  // commutative over the fanout set
  uint64_t h = sig_mix(kSeed, uses.size());
  for (uint64_t u : uses) {
    h = sig_mix(h, u);
  }
  memo[driver] = h;
  return h;
}

struct SinkRef {
  hhds::Node_class node;
  hhds::Port_id    pid = 0;
};
struct InputPort {
  std::string          name;
  hhds::Pin_class      driver;        // external driver pin (its net feeds this port)
  bool                 from_primary = false;
  std::string          primary_name;  // io name when from_primary
  std::vector<SinkRef> sinks;         // sinks inside the region fed by this port
};
struct OutputPort {
  std::string     name;
  hhds::Pin_class driver;  // internal driver pin exported by this port
};
struct IntEdge {
  hhds::Pin_class  driver;
  hhds::Node_class snode;
  hhds::Port_id    spid = 0;
};
struct ConstEdge {
  hhds::Pin_class  cdriver;
  hhds::Node_class snode;
  hhds::Port_id    spid = 0;
};
// How a new-top primary output is driven.
struct OutWire {
  std::string     oname;
  enum Kind { Region, Primary, Const } kind = Region;
  hhds::Pin_class driver;        // Region: the internal driver pin (look up out port)
  std::string     primary_name;  // Primary: source io name
  hhds::Pin_class cdriver;       // Const: the constant pin
};

class Partitioner {
public:
  Partitioner(hhds::Graph* g, hhds::GraphLibrary* outlib, std::string top, bool debug_color,
              livehd::partition::Body_builder hook = {}, bool flatten = false, bool fuse_colors = false,
              bool want_pre_bodies = false)
      : g_(g),
        outlib_(outlib),
        top_(std::move(top)),
        debug_color_(debug_color),
        hook_(std::move(hook)),
        flatten_(flatten),
        fuse_colors_(fuse_colors),
        build_pre_(want_pre_bodies) {}

  bool run();
  void report_stats();

private:
  hhds::Graph*                    g_;
  hhds::GraphLibrary*             outlib_;
  std::string                     top_;
  bool                            debug_color_;
  bool                            saw_uncolored_ = false;  // any color-0 node seen in collect()
  livehd::partition::Body_builder hook_;
  // Whole-design flatten mode: same-color regions merge even when structurally
  // disconnected (one region per color), and a single-region result is emitted
  // directly under `top_` (no wrapper) — see build_module_as_top.
  bool flatten_ = false;
  // The active coloring advertises multi-component color ids ("packed":true --
  // the size window's misc bins of isolated leftovers). One region per COLOR
  // for colored nodes, so the component split cannot silently shred the bins;
  // color 0 keeps the per-component behavior (an uncolored design is not the
  // window's output).
  bool fuse_colors_ = false;
  // Incremental synth: build a per-def region's original-logic pre-body (see
  // build_decomposition want_pre_bodies). Gates the collect() edge-table build
  // on the hook path and the pre-body build in build_module; never for flatten.
  bool build_pre_ = false;

  // collect()-only state: the union-find over node handles and the rep -> dense
  // region index map (ONE entry per region). Both are freed at the end of
  // collect() -- everything after works on dense indices.
  Union_find                                      uf_;
  absl::flat_hash_map<hhds::Node_class, uint32_t> rep2idx_;

  // Per-region tables, indexed by the dense region index minted in collect()'s
  // membership pass (forward_class first-encounter order, so deterministic).
  // Plain vectors on purpose: the previous rep-keyed hash-of-vectors put a
  // 56-byte Node_class key plus hash-slot overhead in front of every region AND
  // a hash probe inside the per-edge hot loop; a whole-graph replica behind a
  // hash map is exactly the shape that explodes on a multi-million-node def.
  std::vector<std::vector<hhds::Node_class>> region_nodes_;
  std::vector<int>                           region_color_;
  // Per-region reuse eligibility for incremental synth: cleared when two
  // crossing inputs share a producer-cone signature (a genuine automorphism
  // whose by-name stitch is not reproducible across recompiles). Sized in
  // name_ports(); surfaced to the hook via Region_body::reuse_eligible.
  std::vector<char>                          region_reuse_ok_;

  std::vector<std::vector<InputPort>>                        module_inputs_;
  std::vector<absl::flat_hash_map<hhds::Pin_class, size_t>>  in_index_;
  std::vector<std::vector<OutputPort>>                       module_outputs_;
  absl::flat_hash_map<hhds::Pin_class, std::pair<uint32_t, size_t>> out_index_;
  std::vector<std::vector<IntEdge>>                          internal_edges_;
  std::vector<std::vector<ConstEdge>>                        const_edges_;
  std::vector<OutWire>                                       top_outputs_;

  std::vector<std::shared_ptr<hhds::GraphIO>> module_gio_;

  // Dense index of the region holding `n`. collect()-time only (uf_/rep2idx_
  // die with it); every partitionable node was indexed by the membership pass.
  [[nodiscard]] uint32_t region_idx(const hhds::Node_class& n) { return rep2idx_.find(uf_.find(n))->second; }

  void ensure_input_port(uint32_t r, const hhds::Pin_class& driver, const SinkRef& sink, bool from_primary,
                         std::string_view pname) {
    auto& idx = in_index_[r];
    auto  it  = idx.find(driver);
    size_t i;
    if (it == idx.end()) {
      i = module_inputs_[r].size();
      module_inputs_[r].push_back(InputPort{std::string{}, driver, from_primary, std::string{pname}, {}});
      idx[driver] = i;
    } else {
      i = it->second;
    }
    module_inputs_[r][i].sinks.push_back(sink);
  }

  void ensure_output_port(const hhds::Pin_class& driver) {
    if (out_index_.contains(driver)) {
      return;
    }
    auto   rd = region_idx(driver.get_master_node());
    size_t i  = module_outputs_[rd].size();
    module_outputs_[rd].push_back(OutputPort{std::string{}, driver});
    out_index_[driver] = {rd, i};
  }

  bool collect();
  void name_ports();
  void diagnose_colors();
  void build_module(uint32_t r);
  void build_module_as_top(uint32_t r);
  void build_top(const std::vector<uint32_t>& regs);
  // Region-module construction shared by build_module's classic (no-hook) body
  // and the incremental pre-body (same edge tables => byte-stable output).
  std::shared_ptr<hhds::GraphIO> declare_region_io(uint32_t r, hhds::GraphLibrary* dst_lib, const std::string& name);
  void                           stamp_region_input_pins(uint32_t r, hhds::Graph* body);
  void emit_region_body(uint32_t r, hhds::Graph* body, hhds::GraphLibrary* dst_lib,
                        const std::vector<hhds::Node_class>& rnodes, const std::vector<IntEdge>& redges,
                        const std::vector<ConstEdge>& rconsts, bool decl_only_subs);
  // Rebuild region r's original logic into `dst_lib` under `name` (decl-only
  // Subs); returns the committed body. The abc cache's stable compare artifact.
  hhds::Graph* build_pre_body_into(uint32_t r, hhds::GraphLibrary& dst_lib, const std::string& name,
                                   const std::vector<hhds::Node_class>& rnodes);
  // As above but for the single-region "as top" shape (primary IO names,
  // top_outputs_ instead of region ports) -- shared by build_module_as_top's
  // classic body and its incremental pre-body.
  void emit_region_body_as_top(uint32_t r, hhds::Graph* body, hhds::GraphLibrary* dst_lib,
                               const std::vector<hhds::Node_class>& rnodes, const std::vector<IntEdge>& redges,
                               const std::vector<ConstEdge>& rconsts, bool decl_only_subs);
  void         emit_top_passthrough_outputs(hhds::Graph* body);  // primary/const-driven outputs (no region)
  hhds::Graph* build_pre_body_as_top(uint32_t r, hhds::GraphLibrary& dst_lib, const std::string& name,
                                     const std::vector<hhds::Node_class>& rnodes);
  // Regions in a reproducible order: by color, then by the smallest member node
  // id (invariant to which member the union-find picked as representative).
  // Region indices are already deterministic; this order is what fixes the
  // module/Sub/pin CREATION order (serialized top lg, downstream cgen/LEC).
  [[nodiscard]] std::vector<uint32_t> ordered_regions();
  void carry_node_attrs(const hhds::Node_class& orig, const hhds::Node_class& neo, hhds::GraphLibrary* dst_lib);
  void carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo);
};

void Partitioner::carry_node_attrs(const hhds::Node_class& orig, const hhds::Node_class& neo, hhds::GraphLibrary* dst_lib) {
  if (gu::has_name(orig)) {
    neo.attr(hhds::attrs::name).set(std::string{gu::node_name_of(orig)});
  }
  if (auto a = orig.attr(livehd::attrs::lut); a.has()) {
    neo.attr(livehd::attrs::lut).set(std::string{a.get()});
  }
  if (auto a = orig.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
    // Import into the output LIBRARY's shared srcmap, never the body's own
    // locator: every body chains to the library base (hhds bind_library), so the
    // id resolves from any region module, while a per-body import re-copies the
    // per-FILE metadata (line-offset tables) into every one of the tens of
    // thousands of region bodies -- measured at ~9 KB/node retained on XSCore,
    // the std::bad_alloc. Ids are content-hashed, so the shared import dedups
    // across bodies and defs for free.
    auto newid = dst_lib->source_map().import_from(g_->source_locator(), a.get());
    neo.attr(hhds::attrs::srcid).set(newid);
  }
}

void Partitioner::carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo) {
  if (auto b = gu::bits_of(orig); b != 0) {
    gu::set_bits(neo, b);
  }
  if (!gu::is_unsign(orig)) {
    gu::set_sign(neo);
  }
  auto pn = gu::pin_name_of(orig);
  if (!pn.empty()) {
    gu::set_pin_name(neo, pn);
  }
  if (auto o = orig.attr(livehd::attrs::pin_offset); o.has()) {
    neo.attr(livehd::attrs::pin_offset).set(o.get());
  }
}

bool Partitioner::collect() {
  auto gio = g_->get_io();

  // Regions = connected components of same-color partitionable nodes. Color 0
  // (NO_COLOR) means the node was never colored — either no `pass.color` ran, or
  // it ran and left this node out. Color 0 is treated as just another color:
  // uncolored nodes merge into their own color-0 region(s) exactly like any
  // colored set, so the decomposition (and `pass.abc`) runs on an uncolored
  // design without a prior color pass. We only warn once (below) so the implicit
  // coloring is visible without being fatal.
  // Whole-design flatten: one region per color, even for structurally
  // disconnected same-color cones (two cones linked only through a primary
  // input or a constant never union-merge below). The flat def must come out
  // as ONE module, and a same-color multi-module split would defeat it.
  absl::flat_hash_map<int, hhds::Node_class> color_anchor;

  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto c = node_color_of(n);
    if (c == NO_COLOR) {
      saw_uncolored_ = true;
    }
    uf_.find(n);
    // fuse_colors_: the coloring advertises multi-component ids (size-window
    // misc bins); one region per color keeps the bins whole. Color 0 is
    // exempt -- uncolored nodes are not the window's output and keep the
    // per-component split. Note this also fuses SEEDED (block-attr) ids that
    // span disconnected clouds: deliberate -- a user block is one region by
    // declaration (its region_opts are keyed per color), not one per cloud.
    if (flatten_ || (fuse_colors_ && c != NO_COLOR)) {
      auto [it, inserted] = color_anchor.try_emplace(c, n);
      if (!inserted) {
        uf_.merge(n, it->second);
      }
    }
    for (const auto& e : n.out_edges()) {
      auto sn = e.sink.get_master_node();
      if (is_partitionable(sn) && node_color_of(sn) == c) {
        uf_.merge(n, sn);
      }
    }
  }
  if (saw_uncolored_) {
    // One warning per def (not per node): the dedup sink collapses repeats.
    livehd::diag::warn("pass.partition", "uncolored-node", "io")
        .msg("top '{}' has uncolored nodes (color 0); treating them as a single color-0 region — run pass.color "
             "first for an explicit partitioning",
             top_)
        .emit();
  }

  // Region membership + color; dense region indices are minted here, in
  // forward_class first-encounter order (deterministic).
  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto [it, inserted] = rep2idx_.try_emplace(uf_.find(n), static_cast<uint32_t>(region_nodes_.size()));
    if (inserted) {
      region_nodes_.emplace_back();
      region_color_.push_back(node_color_of(n));  // every member shares the region's color
    }
    region_nodes_[it->second].push_back(n);
  }
  const size_t nregions = region_nodes_.size();
  module_inputs_.resize(nregions);
  in_index_.resize(nregions);
  module_outputs_.resize(nregions);
  internal_edges_.resize(nregions);
  const_edges_.resize(nregions);
  module_gio_.resize(nregions);

  // Classify every edge feeding a region node (via inp_edges).
  for (auto n : g_->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto r = region_idx(n);
    for (const auto& e : n.inp_edges()) {
      auto dn  = e.driver.get_master_node();
      auto spid = e.sink.get_port_id();
      if (gu::is_const_pin(e.driver)) {
        // internal_edges_/const_edges_ recreate connectivity for the classic
        // (no-hook) rebuild AND the incremental pre-body (build_pre_): both feed
        // the SAME construction, so a comment-only recompile yields a byte-stable
        // pre-body. On the plain hook path they are dead (the hook fills the body
        // itself) -- skip an O(flat-edges) table that peaks before the first
        // region and is dead weight on the exact OOM (flatten) path.
        if (!hook_ || build_pre_) {
          const_edges_[r].push_back(ConstEdge{e.driver, n, spid});
        }
      } else if (gu::is_graph_input_pin(e.driver)) {
        // pin_name_of resolves the graph input's declared port name directly.
        ensure_input_port(r, e.driver, SinkRef{n, spid}, /*from_primary=*/true, std::string{gu::pin_name_of(e.driver)});
      } else if (is_partitionable(dn)) {
        auto rd = region_idx(dn);
        if (rd == r) {
          if (!hook_ || build_pre_) {  // dead on the plain hook path (see const_edges_ above)
            internal_edges_[r].push_back(IntEdge{e.driver, n, spid});
          }
        } else {
          ensure_output_port(e.driver);
          ensure_input_port(r, e.driver, SinkRef{n, spid}, /*from_primary=*/false, std::string{});
        }
      }
      // else: unexpected builtin driver — skip.
    }
  }

  // Primary outputs.
  if (gio) {
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto opin = g_->get_output_pin(decl.name);
      if (opin.is_invalid()) {
        continue;
      }
      for (const auto& e : opin.inp_edges()) {
        auto d  = e.driver;
        auto dn = d.get_master_node();
        if (gu::is_const_pin(d)) {
          top_outputs_.push_back(OutWire{decl.name, OutWire::Const, {}, {}, d});
        } else if (gu::is_graph_input_pin(d)) {
          top_outputs_.push_back(OutWire{decl.name, OutWire::Primary, {}, std::string{gu::pin_name_of(d)}, {}});
        } else if (is_partitionable(dn)) {
          ensure_output_port(d);
          top_outputs_.push_back(OutWire{decl.name, OutWire::Region, d, {}, {}});
        }
      }
    }
  }

  // The union-find and the rep map are dead from here on: everything below
  // (naming, ordering, module builds, the wrapper) works on dense indices and
  // out_index_. Free the def's largest side tables before output bodies grow.
  uf_      = Union_find{};
  rep2idx_ = {};
  return true;
}

void Partitioner::name_ports() {
  // in_index_ is collect()'s dedup table; the port sorts below invalidate its
  // indices and nothing reads it after collect -- drop it, don't rebuild it.
  in_index_.clear();
  out_index_.clear();  // rebuilt below after the sort settles indices
  region_reuse_ok_.assign(region_nodes_.size(), 1);

  // A boundary port becomes a wire in the region module; it must not collide
  // with a recreated internal node's name. The classic failure is a flop whose
  // q is a region output: the port would otherwise take the flop's own
  // wire_name and cgen would emit two same-named declarations (the output reg
  // and the internal flop reg) at different widths.
  //
  // One region at a time, with ONE `used` set covering that region's internal
  // names AND its already-assigned ports: (a) the reserved-name set peaks at
  // the largest region instead of holding every node name of the def at once
  // (hundreds of MB on a multi-million-node def), and (b) an output port can
  // no longer take an input port's name -- the previous per-loop pristine
  // copies let `input x` and `output x` coexist in one module.
  for (uint32_t r = 0; r < static_cast<uint32_t>(region_nodes_.size()); ++r) {
    const auto&                      nodes = region_nodes_[r];
    absl::flat_hash_set<std::string> used;
    used.reserve(nodes.size());
    for (const auto& n : nodes) {
      // Reserve the name cgen will ACTUALLY emit for this node, which is its
      // user name OR — when unnamed — the synthetic `<type>_<nid>`
      // (default_instance_name). Reserving only user-named nodes let an unnamed
      // node's synthetic name (e.g. `mux_56`) collide with a same-named output
      // port; cgen then declares the wire twice at mismatched widths and emits
      // invalid Verilog. This only bit when a node and the port it drives land
      // in the same region — rare under a per-node coloring, but the norm for an
      // uncolored design that folds the whole graph into one color-0 region.
      used.insert(sanitize(gu::default_instance_name(n)));
    }

    // Stable, nid-free content signatures for this region's boundary drivers:
    // the cached body is stitched into a freshly rebuilt wrapper BY PORT NAME,
    // so the name must be reproducible across recompiles (not `<op>_<nid>`). The
    // memo is shared across inputs+outputs so a shared cone is walked once.
    absl::flat_hash_map<hhds::Pin_class, uint64_t> sig_memo;
    auto sig_of = [&](const hhds::Pin_class& drv) {
      absl::flat_hash_set<hhds::Node_class> on_path;
      return cone_sig(drv, sig_memo, on_path, 512);
    };
    // Proposal 2: a boundary INPUT is identified by BOTH its producer cone
    // (sig_of, backward) AND its consumer cone (fwd_sig_of, forward). Producer
    // alone is a coarse tie -- two lanes fed by identical logic but used
    // differently downstream (different packed-state bit ranges) collide; adding
    // the consumer side separates them, so a distinguishable lane gets a distinct,
    // reproducible name instead of an arbitrary-tiebreak _k suffix.
    absl::flat_hash_map<hhds::Pin_class, uint64_t> fwd_memo;
    auto fwd_sig_of = [&](const hhds::Pin_class& drv) {
      absl::flat_hash_set<hhds::Node_class> on_path;
      return fwd_cone_sig(drv, fwd_memo, on_path, 256);
    };

    {
      auto&                                          ports = module_inputs_[r];
      absl::flat_hash_map<hhds::Pin_class, uint64_t> psig;
      psig.reserve(ports.size());
      for (auto& p : ports) {
        psig[p.driver] = sig_mix(sig_of(p.driver), fwd_sig_of(p.driver));
      }
      // Sort by the content signature (nid-free, reproducible) so the port_id
      // numbering and the `_k` dedup are stable across recompiles; the port_id
      // tiebreak only orders the this-run-arbitrary tie handled just below.
      std::sort(ports.begin(), ports.end(), [&](const InputPort& a, const InputPort& b) {
        auto sa = psig[a.driver];
        auto sb = psig[b.driver];
        if (sa != sb) {
          return sa < sb;
        }
        return a.driver.get_port_id() < b.driver.get_port_id();
      });
      // After the bidirectional signature (producer AND consumer cone), two
      // crossing inputs that STILL share a signature are a genuine automorphism:
      // structurally indistinguishable in BOTH directions, so only an arbitrary
      // this-run tiebreak tells their names apart -- not reproducible, so the
      // region cannot be soundly reused by name. Flag it reuse-ineligible (the
      // load-bearing refusal: for a true automorphism the inputs and outputs swap
      // together, so the name-anchored compare/traversal PASS on a swapped binding
      // and the reuse would wire the wrong ports -- proven by an LEC refutation).
      // The consumer cone above dissolves the FALSE ties (lanes distinguishable
      // downstream, e.g. minion_dcache_replay_queue) so they become eligible and
      // soundly reusable; only the genuinely-symmetric residue is refused. Tied
      // OUTPUTS stay eligible (interchangeable: whichever net reads them gets the
      // same value).
      for (size_t i = 1; i < ports.size(); ++i) {
        if (psig[ports[i].driver] == psig[ports[i - 1].driver]) {
          region_reuse_ok_[r] = 0;
          break;
        }
      }
      for (auto& p : ports) {
        std::string base;
        if (p.from_primary && !p.primary_name.empty()) {
          base = p.primary_name;  // declared IO name: already stable
        } else if (auto pn = gu::pin_name_of(p.driver); !pn.empty()) {
          base = std::string{pn};  // named wire: already stable
        } else {
          base = std::format("i_{:016x}", psig[p.driver]);  // anonymous crossing: content hash
        }
        base           = sanitize(base.empty() ? std::string{"in"} : base);
        std::string nm = base;
        int         k  = 1;
        while (used.contains(nm)) {
          nm = base + "_" + std::to_string(k++);
        }
        used.insert(nm);
        p.name = nm;
      }
    }

    {
      auto&                                          ports = module_outputs_[r];
      absl::flat_hash_map<hhds::Pin_class, uint64_t> psig;
      psig.reserve(ports.size());
      for (auto& p : ports) {
        psig[p.driver] = sig_of(p.driver);
      }
      std::sort(ports.begin(), ports.end(), [&](const OutputPort& a, const OutputPort& b) {
        auto sa = psig[a.driver];
        auto sb = psig[b.driver];
        if (sa != sb) {
          return sa < sb;
        }
        return a.driver.get_port_id() < b.driver.get_port_id();
      });
      for (size_t i = 0; i < ports.size(); ++i) {
        std::string base;
        if (auto pn = gu::pin_name_of(ports[i].driver); !pn.empty()) {
          base = std::string{pn};  // named wire/register: already stable
        } else {
          base = std::format("o_{:016x}", psig[ports[i].driver]);  // anonymous: content hash
        }
        base = sanitize(base.empty() ? std::string{"out"} : base);
        // A region output driven by a stateful node (flop/memory) shares that
        // node's emitted reg wire name. cgen declares the output reg (from the IO
        // decl) AND the node's own reg under that one name, at mismatched
        // width/sign -> broken Verilog (it also skips the port=node assign when
        // the names match). Give the port a distinct name so cgen emits a clean
        // `port = node` assign, exactly like a normal module's flop-driven output.
        // is_type_register, not an explicit Flop||Memory list (2f-latch M2): a
        // LATCH-driven region output hit the exact broken-Verilog mode the
        // comment above describes, because a latch also gets an emitted reg
        // wire under its own name. Fflop is covered for the same reason.
        if (gu::is_type_register(ports[i].driver.get_master_node())) {
          base += "_o";
        }
        std::string nm = base;
        int         k  = 1;
        while (used.contains(nm)) {
          nm = base + "_" + std::to_string(k++);
        }
        used.insert(nm);
        ports[i].name               = nm;
        out_index_[ports[i].driver] = {r, i};
      }
    }
  }
}

std::vector<uint32_t> Partitioner::ordered_regions() {
  // min-nid is precomputed in one pass over the membership vectors: computing it
  // inside the sort comparator re-walked whole regions O(R log R) times, which on
  // a multi-million-node def is minutes of pure comparator time.
  std::vector<uint32_t> regs(region_nodes_.size());
  std::vector<uint64_t> min_nid(region_nodes_.size(), std::numeric_limits<uint64_t>::max());
  for (uint32_t r = 0; r < static_cast<uint32_t>(region_nodes_.size()); ++r) {
    regs[r] = r;
    for (const auto& n : region_nodes_[r]) {
      min_nid[r] = std::min<uint64_t>(min_nid[r], n.get_debug_nid());
    }
  }
  std::sort(regs.begin(), regs.end(), [&](uint32_t a, uint32_t b) {
    if (region_color_[a] != region_color_[b]) {
      return region_color_[a] < region_color_[b];
    }
    return min_nid[a] < min_nid[b];
  });
  return regs;
}

// A body-less decl clone of a Sub's child def into `dst_lib`, for the pre-body:
// its throwaway library has no partitioned children to resolve against and needs
// none (the structural compare treats a Sub as a blackbox keyed by def name +
// port shape). is_loop_break cannot be derived from the absent body, so carry it
// from the INSTANCE onto the first output decl pin -- semdiff seeds a Sub as a
// cut point only when is_loop_break (a sequential submodule that breaks a
// combinational cycle); without the stamp the cyclic cone stays unsigned.
static std::shared_ptr<hhds::GraphIO> clone_subnode_decl(hhds::GraphLibrary* dst_lib, const hhds::Node_class& inst) {
  auto orig = inst.get_subnode_io();
  if (!orig) {
    return nullptr;
  }
  if (auto existing = dst_lib->find_io(orig->get_name())) {
    return existing;
  }
  auto io = dst_lib->create_io(orig->get_name());
  for (const auto& p : orig->get_input_pin_decls()) {
    io->add_input(p.name, p.port_id, p.loop_break);
    if (p.bits != 0) {
      io->set_bits(p.name, p.bits);
    }
    io->set_unsign(p.name, p.unsign);
  }
  const auto& odecls     = orig->get_output_pin_decls();
  const bool  inst_break = inst.is_loop_break();
  for (size_t i = 0; i < odecls.size(); ++i) {
    const auto& p = odecls[i];
    io->add_output(p.name, p.port_id, p.loop_break || (inst_break && i == 0));
    if (p.bits != 0) {
      io->set_bits(p.name, p.bits);
    }
    io->set_unsign(p.name, p.unsign);
  }
  return io;
}

// Declare + size the region-module IO (input then output ports) in `dst_lib`.
// Shared by the mapped-module shell on outlib_ and the pre-body's scratch lib,
// so both carry byte-identical port names/widths/signs.
std::shared_ptr<hhds::GraphIO> Partitioner::declare_region_io(uint32_t r, hhds::GraphLibrary* dst_lib,
                                                              const std::string& name) {
  auto          gio = dst_lib->create_io(name);
  hhds::Port_id pid = 1;
  for (auto& p : module_inputs_[r]) {
    gio->add_input(p.name, pid++);
    if (auto b = gu::bits_of(p.driver); b != 0) {
      gio->set_bits(p.name, static_cast<uint32_t>(b));
    }
    gio->set_unsign(p.name, gu::is_unsign(p.driver));
  }
  for (auto& p : module_outputs_[r]) {
    gio->add_output(p.name, pid++);
    if (auto b = gu::bits_of(p.driver); b != 0) {
      gio->set_bits(p.name, static_cast<uint32_t>(b));
    }
    gio->set_unsign(p.name, gu::is_unsign(p.driver));
  }
  return gio;
}

// Stamp per-pin bits/sign on the materialized INPUT pins. The GraphIO declared
// sign is NOT auto-propagated to the pin objects, and readers (pass.lec,
// pass.abc's bit-blast) consult the per-pin attr, not the declaration -- so a
// signed/8-bit region boundary would otherwise read as unsigned/1-bit. Mirrors
// tolg. Inputs only: the output port is a SINK (its sign lives on the GraphIO).
void Partitioner::stamp_region_input_pins(uint32_t r, hhds::Graph* body) {
  for (auto& p : module_inputs_[r]) {
    auto ip = body->get_input_pin(p.name);
    if (auto b = gu::bits_of(p.driver); b != 0) {
      gu::set_bits(ip, b);
    }
    gu::is_unsign(p.driver) ? gu::set_unsign(ip) : gu::set_sign(ip);
  }
}

// Recreate region r's original logic into `body` (in `dst_lib`) from the edge
// tables: typed-node clone + Sub relink, internal/const/boundary edges, and the
// Sub declared-output completion. THE single construction shared by the classic
// no-hook rebuild (bodied Subs in outlib_) and the incremental pre-body
// (decl_only_subs => body-less Sub decls in a throwaway lib) -- so the two are
// byte-identical and a re-derived pre-body cannot drift from the emitted def.
void Partitioner::emit_region_body(uint32_t r, hhds::Graph* body, hhds::GraphLibrary* dst_lib,
                                   const std::vector<hhds::Node_class>& rnodes, const std::vector<IntEdge>& redges,
                                   const std::vector<ConstEdge>& rconsts, bool decl_only_subs) {
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;
  for (const auto& n : rnodes) {
    auto op  = gu::type_op_of(n);
    auto neo = gu::create_typed_node(*body, op);
    if (op == Ntype_op::Sub && n.get_subnode_io()) {
      // Hierarchical input: re-link the instance to its child def. The classic
      // path resolves the already-partitioned child (children-first ordering) or
      // clones a black-box decl in outlib_; the pre-body clones a body-less decl
      // into its own throwaway lib (no children there, and the compare needs
      // none). Without the link the Sub's body wires float and the def is lost.
      std::shared_ptr<hhds::GraphIO> out_child
          = decl_only_subs ? clone_subnode_decl(dst_lib, n) : livehd::partition::resolve_or_clone_subdef(dst_lib, n);
      if (out_child) {
        neo.set_subnode(out_child);
      } else {
        livehd::diag::err("pass.partition", "missing-subdef", "unsupported")
            .msg("sub-instance '{}' in '{}' references child def '{}' missing from the output library",
                 gu::debug_name(n),
                 top_,
                 std::string{n.get_subnode_io()->get_name()})
            .fatal();
      }
    }
    node_map[n] = neo;
    carry_node_attrs(n, neo, dst_lib);
  }

  // Track which Sub output ports get a real (edge-carrying) pin, so the
  // declared-outputs completion below does not clobber their carried attrs.
  absl::flat_hash_map<hhds::Node_class, absl::flat_hash_set<uint32_t>> sub_outs_made;

  auto driver_pin = [&](const hhds::Pin_class& orig) {
    auto master = orig.get_master_node();
    auto neo    = node_map[master].create_driver_pin(orig.get_port_id());
    carry_driver_attrs(orig, neo);
    if (gu::type_op_of(master) == Ntype_op::Sub) {
      sub_outs_made[master].insert(static_cast<uint32_t>(orig.get_port_id()));
    }
    return neo;
  };

  // Internal edges.
  for (const auto& e : redges) {
    auto dp = driver_pin(e.driver);
    auto sp = node_map[e.snode].create_sink_pin(e.spid);
    dp.connect_sink(sp);
  }
  // Constant edges, one recreated const node per SOURCE pin: the input had one
  // const feeding K sinks, so the module gets one too -- per-edge recreation
  // minted K duplicate Nconst nodes (plus pins and value attrs) into the output
  // library for every shared constant.
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> const_map;
  for (const auto& e : rconsts) {
    auto it = const_map.find(e.cdriver);
    if (it == const_map.end()) {
      it = const_map.emplace(e.cdriver, gu::create_const(*body, gu::hydrate_const(e.cdriver))).first;
    }
    auto sp = node_map[e.snode].create_sink_pin(e.spid);
    it->second.connect_sink(sp);
  }
  // Boundary inputs. The sink lists are dead after this loop (build_top reads
  // only name/driver/from_primary); free them region by region.
  for (auto& p : module_inputs_[r]) {
    auto ipin = body->get_input_pin(p.name);
    for (const auto& s : p.sinks) {
      auto sp = node_map[s.node].create_sink_pin(s.pid);
      ipin.connect_sink(sp);
    }
    p.sinks.clear();
    p.sinks.shrink_to_fit();
  }
  // Boundary outputs.
  for (const auto& p : module_outputs_[r]) {
    auto dp = driver_pin(p.driver);
    dp.connect_sink(body->get_output_pin(p.name));
  }

  // Sub instances: materialize the declared outputs that carried no edge
  // (edge-less driver pin), mirroring tolg. Readers probe every declared output
  // (cgen create_subs, LEC pairing) and hhds find_pin asserts on a pin that was
  // never created. Width/sign come from the child decl; ports wired above keep
  // their carried attrs.
  for (const auto& n : rnodes) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto neo = node_map[n];
    auto sio = neo.get_subnode_io();
    if (!sio) {
      continue;
    }
    auto& made = sub_outs_made[n];
    for (const auto& d : sio->get_output_pin_decls()) {
      if (made.contains(static_cast<uint32_t>(d.port_id))) {
        continue;
      }
      auto np = neo.create_driver_pin(d.port_id);
      if (d.bits != 0) {
        gu::set_bits(np, static_cast<int>(d.bits));
      }
      // No sign stamp: decl.unsign==false also means "unspecified", and an
      // edge-less pin has no reader — leave the attr absent (unsigned default).
    }
  }
}

// Rebuild region r's ORIGINAL logic into a throwaway library `dst_lib` under
// `name`, via the same emit_region_body the classic path uses (decl-only Subs).
// The abc cache's stable structural-compare artifact. Frees the per-region edge
// tables after (dead: the hook fills the mapped body; build_top needs only the
// port lists).
hhds::Graph* Partitioner::build_pre_body_into(uint32_t r, hhds::GraphLibrary& dst_lib, const std::string& name,
                                              const std::vector<hhds::Node_class>& rnodes) {
  auto gio  = declare_region_io(r, &dst_lib, name);
  auto body = gio->create_graph();
  stamp_region_input_pins(r, body.get());
  emit_region_body(r, body.get(), &dst_lib, rnodes, internal_edges_[r], const_edges_[r], /*decl_only_subs=*/true);
  body->commit();
  internal_edges_[r].clear();
  internal_edges_[r].shrink_to_fit();
  const_edges_[r].clear();
  const_edges_[r].shrink_to_fit();
  return body.get();
}

void Partitioner::build_module(uint32_t r) {
  int         color = region_color_[r];
  std::string name  = std::format("{}__c{}", top_, color);
  // Disambiguate if this color has multiple regions.
  if (outlib_->find_io(name)) {
    int         suffix = 1;
    std::string base   = name;
    while (outlib_->find_io(name)) {
      name = std::format("{}_r{}", base, suffix++);
    }
  }

  auto gio       = declare_region_io(r, outlib_, name);
  module_gio_[r] = gio;
  auto body      = gio->create_graph();
  stamp_region_input_pins(r, body.get());

  // Body-builder hook (task 2a-abc): hand the region interface + contents to the
  // caller, which fills the body (e.g. an ABC-mapped netlist) instead of the
  // original logic. The IO pins are already materialized on `body`.
  if (hook_) {
    livehd::partition::Region_body rb;
    rb.body        = body.get();
    rb.src         = g_;
    rb.color       = color;
    rb.module_name = name;
    rb.reuse_eligible = (r < region_reuse_ok_.size()) ? (region_reuse_ok_[r] != 0) : true;
    for (auto& p : module_inputs_[r]) {
      rb.inputs.push_back({p.name, p.driver, gu::bits_of(p.driver), !gu::is_unsign(p.driver)});
    }
    for (auto& p : module_outputs_[r]) {
      rb.outputs.push_back({p.name, p.driver, gu::bits_of(p.driver), !gu::is_unsign(p.driver)});
    }
    // rb.nodes is a non-owning span: the storage must outlive the synchronous
    // hook call, so move it into a local (freeing the map slot) and view THAT.
    auto rnodes = std::move(region_nodes_[r]);
    rb.nodes    = rnodes;
    // Incremental synth: rebuild the region's ORIGINAL logic into a throwaway
    // library via the SAME construction (emit_region_body), so the abc cache's
    // structural compare sees a byte-stable pre-body across recompiles. `pre_lib`
    // must outlive the synchronous hook_. Skipped when flattening: the as-top
    // path owns the single whole-design region and the edge tables would peak at
    // hundreds of MB with nothing to reuse incrementally.
    hhds::GraphLibrary pre_lib;
    if (build_pre_ && !flatten_ && rb.reuse_eligible) {
      rb.pre_name = "p_" + name;
      rb.pre_body = build_pre_body_into(r, pre_lib, rb.pre_name, rnodes);
      rb.pre_lib  = &pre_lib;
    }
    hook_(rb);
    body->commit();
    return;
  }

  // Each region is this function's single visitor: take the membership and edge
  // tables by move and drop the map slots, so the def's Partitioner state SHRINKS
  // as the output bodies grow instead of stacking under them (these tables are
  // the largest per-def transients -- hundreds of MB on a multi-million-node
  // def). ordered_regions() already ran; nothing reads the entries again.
  auto rnodes  = std::move(region_nodes_[r]);
  auto redges  = std::move(internal_edges_[r]);
  auto rconsts = std::move(const_edges_[r]);
  emit_region_body(r, body.get(), outlib_, rnodes, redges, rconsts, /*decl_only_subs=*/false);
  body->commit();
}

// Whole-design flatten, single region: the region IS the design. Emit it
// directly under the top's own name with the top's own port list (no wrapper,
// no __c suffix) — `pass.color flat` + `pass.abc` then yields exactly one
// output module. Ports keep the primary IO names (decls cloned verbatim), so
// the result is a drop-in replacement for the original def; name_ports()'s
// collision renaming is bypassed on purpose (internal child nodes are
// instance-path-prefixed by the flattener and cannot collide with port names).
void Partitioner::build_module_as_top(uint32_t r) {
  auto src_gio = g_->get_io();
  auto gio     = outlib_->create_io(top_);
  module_gio_[r] = gio;
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    gio->add_input(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      gio->set_bits(decl.name, decl.bits);
    }
    gio->set_unsign(decl.name, decl.unsign);
  }
  for (const auto& decl : src_gio->get_output_pin_decls()) {
    gio->add_output(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      gio->set_bits(decl.name, decl.bits);
    }
    gio->set_unsign(decl.name, decl.unsign);
  }
  auto body = gio->create_graph();

  // Stamp bits/sign on the materialized input pins (same invariant as
  // build_module/build_top: the decl is not auto-propagated to pin attrs and
  // every reader sizes from the pin).
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    auto ip = body->get_input_pin(decl.name);
    if (decl.bits != 0) {
      gu::set_bits(ip, static_cast<int>(decl.bits));
    }
    decl.unsign ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  // With one region there is no other region to drive a boundary port, so
  // every input port must come from a primary input.
  for (const auto& p : module_inputs_[r]) {
    if (!p.from_primary) {
      livehd::diag::err("pass.partition", "flatten-input", "internal")
          .msg("flatten: single-region input port unexpectedly driven by a non-primary pin in '{}'", top_)
          .fatal();
      return;
    }
  }

  if (hook_) {
    livehd::partition::Region_body rb;
    rb.body           = body.get();
    rb.src            = g_;
    rb.color          = region_color_[r];
    rb.module_name    = top_;
    rb.reuse_eligible = (r < region_reuse_ok_.size()) ? (region_reuse_ok_[r] != 0) : true;
    for (const auto& p : module_inputs_[r]) {
      rb.inputs.push_back({p.primary_name, p.driver, gu::bits_of(p.driver), !gu::is_unsign(p.driver)});
    }
    for (const auto& ow : top_outputs_) {
      if (ow.kind == OutWire::Region) {
        rb.outputs.push_back({ow.oname, ow.driver, gu::bits_of(ow.driver), !gu::is_unsign(ow.driver)});
      }
    }
    // Same span-lifetime rule as build_module's hook branch.
    auto rnodes = std::move(region_nodes_[r]);
    rb.nodes    = rnodes;
    // Incremental pre-body (see build_module). Skipped when flattening: the single
    // whole-design region has nothing to reuse and its edge tables are the largest
    // transient. `pre_lib` must outlive the synchronous hook_.
    hhds::GraphLibrary pre_lib;
    if (build_pre_ && !flatten_ && rb.reuse_eligible) {
      rb.pre_name = "p_" + top_;
      rb.pre_body = build_pre_body_as_top(r, pre_lib, rb.pre_name, rnodes);
      rb.pre_lib  = &pre_lib;
    }
    hook_(rb);
  } else {
    // Same consume-and-free as build_module: with one region these tables ARE
    // the whole def (the whole DESIGN on the flatten path), so releasing them as
    // the twin is built is the difference between 1x and 2x peak.
    auto rnodes  = std::move(region_nodes_[r]);
    auto redges  = std::move(internal_edges_[r]);
    auto rconsts = std::move(const_edges_[r]);

    emit_region_body_as_top(r, body.get(), outlib_, rnodes, redges, rconsts, /*decl_only_subs=*/false);
  }

  // Primary-driven and constant-driven outputs bypass the region in both the
  // hook and twin shapes (the wrapper used to wire these; there is no wrapper).
  emit_top_passthrough_outputs(body.get());

  body->commit();
}

// The single-region "as top" construction (primary IO + top_outputs_), shared by
// build_module_as_top's classic body and its incremental pre-body -- so a
// re-derived pre-body cannot drift from the emitted def. Mirrors emit_region_body
// but wires boundary inputs by primary_name and outputs from top_outputs_.
void Partitioner::emit_region_body_as_top(uint32_t r, hhds::Graph* body, hhds::GraphLibrary* dst_lib,
                                          const std::vector<hhds::Node_class>& rnodes, const std::vector<IntEdge>& redges,
                                          const std::vector<ConstEdge>& rconsts, bool decl_only_subs) {
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map;
  for (const auto& n : rnodes) {
    auto op  = gu::type_op_of(n);
    auto neo = gu::create_typed_node(*body, op);
    if (op == Ntype_op::Sub && n.get_subnode_io()) {
      std::shared_ptr<hhds::GraphIO> out_child
          = decl_only_subs ? clone_subnode_decl(dst_lib, n) : livehd::partition::resolve_or_clone_subdef(dst_lib, n);
      if (out_child) {
        neo.set_subnode(out_child);
      } else {
        livehd::diag::err("pass.partition", "missing-subdef", "unsupported")
            .msg("sub-instance '{}' in '{}' references child def '{}' missing from the output library",
                 gu::debug_name(n),
                 top_,
                 std::string{n.get_subnode_io()->get_name()})
            .fatal();
      }
    }
    node_map[n] = neo;
    carry_node_attrs(n, neo, dst_lib);
  }

  auto driver_pin = [&](const hhds::Pin_class& orig) {
    auto neo = node_map[orig.get_master_node()].create_driver_pin(orig.get_port_id());
    carry_driver_attrs(orig, neo);
    return neo;
  };

  for (const auto& e : redges) {
    driver_pin(e.driver).connect_sink(node_map[e.snode].create_sink_pin(e.spid));
  }
  // One recreated const per source pin (see build_module).
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class> const_map;
  for (const auto& e : rconsts) {
    auto cit = const_map.find(e.cdriver);
    if (cit == const_map.end()) {
      cit = const_map.emplace(e.cdriver, gu::create_const(*body, gu::hydrate_const(e.cdriver))).first;
    }
    cit->second.connect_sink(node_map[e.snode].create_sink_pin(e.spid));
  }
  for (auto& p : module_inputs_[r]) {
    auto ipin = body->get_input_pin(p.primary_name);
    for (const auto& s : p.sinks) {
      ipin.connect_sink(node_map[s.node].create_sink_pin(s.pid));
    }
    p.sinks.clear();
    p.sinks.shrink_to_fit();
  }
  for (const auto& ow : top_outputs_) {
    if (ow.kind == OutWire::Region) {
      driver_pin(ow.driver).connect_sink(body->get_output_pin(ow.oname));
    }
  }

  // Materialize declared-but-unwired Sub outputs (same reader invariant as
  // build_module; "has an edge" is exact here because every created driver pin
  // above is immediately connected).
  for (const auto& n : rnodes) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto neo = node_map[n];
    auto sio = neo.get_subnode_io();
    if (!sio) {
      continue;
    }
    absl::flat_hash_set<uint32_t> made;
    for (const auto& e : neo.out_edges()) {
      made.insert(static_cast<uint32_t>(e.driver.get_port_id()));
    }
    for (const auto& d : sio->get_output_pin_decls()) {
      if (made.contains(static_cast<uint32_t>(d.port_id))) {
        continue;
      }
      auto np = neo.create_driver_pin(d.port_id);
      if (d.bits != 0) {
        gu::set_bits(np, static_cast<int>(d.bits));
      }
    }
  }
}

// Outputs driven directly by a primary input or a constant (they bypass the
// region entirely). Applied to both the mapped body and the pre-body.
void Partitioner::emit_top_passthrough_outputs(hhds::Graph* body) {
  for (const auto& ow : top_outputs_) {
    if (ow.kind == OutWire::Const) {
      gu::create_const(*body, gu::hydrate_const(ow.cdriver)).connect_sink(body->get_output_pin(ow.oname));
    } else if (ow.kind == OutWire::Primary) {
      body->get_input_pin(ow.primary_name).connect_sink(body->get_output_pin(ow.oname));
    }
  }
}

// The as-top pre-body: rebuild region r's original logic into `dst_lib` under
// `name` with the top's primary IO, decl-only Subs. The abc cache's stable
// compare artifact for a single-region def.
hhds::Graph* Partitioner::build_pre_body_as_top(uint32_t r, hhds::GraphLibrary& dst_lib, const std::string& name,
                                                const std::vector<hhds::Node_class>& rnodes) {
  auto src_gio = g_->get_io();
  auto gio     = dst_lib.create_io(name);
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    gio->add_input(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      gio->set_bits(decl.name, decl.bits);
    }
    gio->set_unsign(decl.name, decl.unsign);
  }
  for (const auto& decl : src_gio->get_output_pin_decls()) {
    gio->add_output(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      gio->set_bits(decl.name, decl.bits);
    }
    gio->set_unsign(decl.name, decl.unsign);
  }
  auto body = gio->create_graph();
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    auto ip = body->get_input_pin(decl.name);
    if (decl.bits != 0) {
      gu::set_bits(ip, static_cast<int>(decl.bits));
    }
    decl.unsign ? gu::set_unsign(ip) : gu::set_sign(ip);
  }
  emit_region_body_as_top(r, body.get(), &dst_lib, rnodes, internal_edges_[r], const_edges_[r], /*decl_only_subs=*/true);
  emit_top_passthrough_outputs(body.get());
  body->commit();
  internal_edges_[r].clear();
  internal_edges_[r].shrink_to_fit();
  const_edges_[r].clear();
  const_edges_[r].shrink_to_fit();
  return body.get();
}

void Partitioner::build_top(const std::vector<uint32_t>& regs) {
  auto src_gio = g_->get_io();
  auto tgio    = outlib_->create_io(top_);
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    tgio->add_input(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      tgio->set_bits(decl.name, decl.bits);
    }
    tgio->set_unsign(decl.name, decl.unsign);
  }
  for (const auto& decl : src_gio->get_output_pin_decls()) {
    tgio->add_output(decl.name, decl.port_id, decl.loop_break);
    if (decl.bits != 0) {
      tgio->set_bits(decl.name, decl.bits);
    }
    tgio->set_unsign(decl.name, decl.unsign);
  }
  auto t = tgio->create_graph();

  // Stamp bits/sign on the materialized top input pins, mirroring tolg: the
  // GraphIO decl is not auto-propagated to the pin attrs, and readers that
  // re-consume this wrapper (pass.abc re-mapping a netlist, cgen) size a port
  // from the PIN attr — without it an 8-bit input reads as 1 bit.
  for (const auto& decl : src_gio->get_input_pin_decls()) {
    auto ip = t->get_input_pin(decl.name);
    if (decl.bits != 0) {
      gu::set_bits(ip, static_cast<int>(decl.bits));
    }
    decl.unsign ? gu::set_unsign(ip) : gu::set_sign(ip);
  }

  // One Sub instance per region.
  std::vector<hhds::Node_class>                                  sub_of(module_gio_.size());
  std::vector<absl::flat_hash_map<std::string, hhds::Pin_class>> sub_out_pin(module_gio_.size());
  for (const auto& r : regs) {
    auto gio = module_gio_[r];
    auto sub = gu::create_typed_node(*t, Ntype_op::Sub);
    sub.set_subnode(gio);
    // Region wrappers are left ANONYMOUS (no `name` attr) on purpose: a named
    // wrapper would insert a `u_<module>` level into every internal node's hier
    // name (top.u_top__c3.foo instead of top.foo), breaking name-based lec
    // pairing / opentimer / VCD between the source and the re-partitioned design.
    // build_hier_name treats an unnamed instance as transparent, so the leaf hier
    // names are preserved; cgen synthesizes the stable Verilog name `u_<module>`
    // at emit (Cgen_verilog::sub_instance_name).
    sub_of[r] = sub;
  }

  // Sub output pin for the net carried by original driver `d` (idempotent per
  // instance+port). Stamp bits/sign so cgen declares the top-level wire at the
  // right width (a Sub output that fans straight into other Subs otherwise
  // defaults to 1 bit).
  //
  // The region comes from out_index_ (collect() stored {region, port} for every
  // exported driver), NOT from region_of(): out_index_ already knows it, and not
  // consulting the union-find here is what lets run() free uf_ -- the def's
  // second-largest table -- before any output body is built.
  auto get_sub_out = [&](const hhds::Pin_class& d) {
    auto oit = out_index_.find(d);
    assert(oit != out_index_.end() && "build_top: driver was never exported as a region output");
    auto        rd    = oit->second.first;
    const auto& pname = module_outputs_[rd][oit->second.second].name;
    auto& m     = sub_out_pin[rd];
    auto  it    = m.find(pname);
    if (it != m.end()) {
      return it->second;
    }
    auto p = sub_of[rd].create_driver_pin(pname);
    if (auto b = gu::bits_of(d); b != 0) {
      gu::set_bits(p, b);
    }
    if (!gu::is_unsign(d)) {
      gu::set_sign(p);
    }
    m[pname] = p;
    return p;
  };

  // Wire each module's input ports.
  for (const auto& r : regs) {
    for (const auto& p : module_inputs_[r]) {
      // The sub's GraphIO input port already carries the width+sign (create_io
      // above); cgen declares the port from there and sizes the connecting wire
      // from the driver. `bits`/`signed` are driver-pin properties, so do NOT
      // stamp this boundary SINK (node_util.hpp set_bits asserts driver-only).
      auto spin = sub_of[r].create_sink_pin(p.name);
      if (p.from_primary) {
        t->get_input_pin(p.primary_name).connect_sink(spin);
      } else {
        get_sub_out(p.driver).connect_sink(spin);
      }
    }
  }

  // Wire the new top's primary outputs.
  for (const auto& ow : top_outputs_) {
    auto out_sink = t->get_output_pin(ow.oname);
    if (ow.kind == OutWire::Region) {
      get_sub_out(ow.driver).connect_sink(out_sink);
    } else if (ow.kind == OutWire::Primary) {
      t->get_input_pin(ow.primary_name).connect_sink(out_sink);
    } else {  // Const
      gu::create_const(*t, gu::hydrate_const(ow.cdriver)).connect_sink(out_sink);
    }
  }

  t->commit();
}

// Group regions by color and report the per-region interface signature so a
// same-color interface divergence (almost always an upstream color-pass bug)
// can be pinpointed. Module-per-region keeps the rebuild correct regardless,
// but a mismatch means the color pass violated "same id => identical region".
void Partitioner::diagnose_colors() {
  absl::flat_hash_map<int, std::vector<uint32_t>> by_color;
  for (uint32_t r = 0; r < static_cast<uint32_t>(region_nodes_.size()); ++r) {
    by_color[region_color_[r]].push_back(r);
  }
  auto sig = [&](uint32_t r) {
    std::vector<int> in_w;
    std::vector<int> out_w;
    for (auto& p : module_inputs_[r]) {
      in_w.push_back(gu::bits_of(p.driver));
    }
    for (auto& p : module_outputs_[r]) {
      out_w.push_back(gu::bits_of(p.driver));
    }
    std::sort(in_w.begin(), in_w.end());
    std::sort(out_w.begin(), out_w.end());
    std::string s = "in[";
    for (int w : in_w) {
      s += std::to_string(w) + ",";
    }
    s += "] out[";
    for (int w : out_w) {
      s += std::to_string(w) + ",";
    }
    s += "]";
    return s;
  };
  for (auto& [color, regions] : by_color) {
    if (regions.size() < 2) {
      continue;
    }
    std::string first = sig(regions.front());
    for (size_t i = 1; i < regions.size(); ++i) {
      std::string s = sig(regions[i]);
      if (s != first) {
        std::print("[partition.debug_color] color {} region interface mismatch: '{}' vs '{}' "
                   "(node {} vs {}) -- the color pass produced non-identical same-id regions\n",
                   color,
                   first,
                   s,
                   gu::debug_name(region_nodes_[regions.front()].front()),
                   gu::debug_name(region_nodes_[regions[i]].front()));
      }
    }
  }
}

bool Partitioner::run() {
  if (!collect()) {
    return false;
  }
  auto regs = ordered_regions();
  if (debug_color_) {
    diagnose_colors();
  }
  // A def that collapses to a SINGLE region is emitted directly under its own
  // name and port list -- no wrapper. The region IS the def, so a `<def>` wrapper
  // whose only content is one `<def>__c<id>` instance plus the wires between them
  // is pure overhead: for a leaf like ALU or StageReg it adds a whole module and
  // an instance for zero logic. This covers the whole-design flatten case
  // (pass.color flat) AND the common per-def case where a def's entire body is
  // one color. Emitting under the def's own IO also keeps the original,
  // collision-free port names (no build_module `_o` renaming) -- which is why
  // name_ports() is skipped outright here. Multi-region defs keep the
  // wrapper+regions shape (the wrapper wires the several `__c<id>` regions
  // together).
  if (regs.size() == 1) {
    build_module_as_top(regs.front());
    return true;
  }
  name_ports();
  for (const auto& r : regs) {
    build_module(r);
  }
  build_top(regs);
  return true;
}

void Partitioner::report_stats() {
  if (!collect()) {
    return;
  }
  name_ports();
  size_t total_ports = 0;
  std::print("pass.partition stats for top '{}':\n", top_);
  std::print("  regions: {}\n", region_nodes_.size());
  for (uint32_t r = 0; r < static_cast<uint32_t>(region_nodes_.size()); ++r) {
    size_t in  = module_inputs_[r].size();
    size_t out = module_outputs_[r].size();
    total_ports += in + out;
    std::print("  region color={} nodes={} in_ports={} out_ports={}\n", region_color_[r], region_nodes_[r].size(), in, out);
  }
  std::print("  total boundary ports: {}\n", total_ports);
}

// Resolve the top graph from `graphs` and compute the reachable defs in
// children-before-parents (post-order) order, so each parent's Sub instances can
// re-link to an already-partitioned child def in the output library. A flat
// single-module top yields just itself. Returns the top graph (nullptr if not
// found); `top` is filled in when it was empty.
hhds::Graph* resolve_order(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string& top,
                           std::vector<hhds::Graph*>& order) {
  hhds::Graph*                                 g = nullptr;
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> gid2graph;
  for (const auto& sp : graphs) {
    if (!sp) {
      continue;
    }
    gid2graph[sp->get_gid()] = sp.get();
    if (g == nullptr && (top.empty() || sp->get_name() == top)) {
      g = sp.get();
      if (top.empty()) {
        top = std::string{sp->get_name()};
      }
    }
  }
  if (g == nullptr) {
    return nullptr;
  }
  absl::flat_hash_set<hhds::Gid>    seen;
  std::function<void(hhds::Graph*)> dfs = [&](hhds::Graph* gg) {
    if (gg == nullptr || !seen.insert(gg->get_gid()).second) {
      return;
    }
    for (auto n : gg->forward_class()) {
      if (gu::is_type_sub(n)) {
        auto it = gid2graph.find(n.get_subnode_gid());
        if (it != gid2graph.end()) {
          dfs(it->second);
        }
      }
    }
    order.push_back(gg);
  };
  dfs(g);
  return g;
}

// Does g's active coloring advertise multi-component color ids ("packed":true,
// written by pass.color synth when a min floor is active)? The size window
// bin-packs isolated under-min leftovers into misc bins; without the matching
// anchor union in collect() the component split would silently shred them back
// into per-cloud modules. Same substring probe as flatten_resolved below — the
// blob is machine-written by build_coloring_info_json. Read off the TOP graph:
// one pass.color run colors the whole hierarchy, so the flag is global.
bool coloring_packed(hhds::Graph* g) {
  if (auto a = g->get_input_node().attr(livehd::attrs::coloring_info); a.has()) {
    return std::string_view{a.get()}.find("\"packed\":true") != std::string_view::npos;
  }
  return false;
}

// Resolve Flatten_mode::automatic against g's active coloring: a
// `pass.color flat` coloring means whole-design synthesis. Same substring
// probe as color_common's has_seeded_coloring — the blob is machine-written
// by build_coloring_info_json.
bool flatten_resolved(hhds::Graph* g, livehd::partition::Flatten_mode mode) {
  if (mode == livehd::partition::Flatten_mode::on) {
    return true;
  }
  if (mode == livehd::partition::Flatten_mode::off) {
    return false;
  }
  if (auto a = g->get_input_node().attr(livehd::attrs::coloring_info); a.has()) {
    return std::string_view{a.get()}.find("\"algorithm\":\"flat\"") != std::string_view::npos;
  }
  return false;
}

}  // namespace

namespace livehd::partition {

bool flatten_is_whole_design(hhds::Graph* g, Flatten_mode mode) { return flatten_resolved(g, mode); }

std::shared_ptr<hhds::GraphIO> resolve_or_clone_subdef(hhds::GraphLibrary* outlib, const hhds::Node_class& inst) {
  auto child = inst.get_subnode_io();
  if (!child) {
    return nullptr;
  }
  if (auto out_child = outlib->find_io(child->get_name())) {
    return out_child;
  }
  if (inst.get_subnode_graph() != nullptr) {
    return nullptr;  // has a body: children-first ordering should have partitioned it already
  }
  // Body-less black-box def: clone the IO decl so the instance stays opaque.
  auto io = outlib->create_io(std::string{child->get_name()});
  for (const auto& d : child->get_input_pin_decls()) {
    io->add_input(d.name, d.port_id, d.loop_break);
    if (d.bits != 0) {
      io->set_bits(d.name, d.bits);
    }
    io->set_unsign(d.name, d.unsign);
  }
  for (const auto& d : child->get_output_pin_decls()) {
    io->add_output(d.name, d.port_id, d.loop_break);
    if (d.bits != 0) {
      io->set_bits(d.name, d.bits);
    }
    io->set_unsign(d.name, d.unsign);
  }
  return io;
}

}  // namespace livehd::partition

bool Pass_partition::build_decomposition(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, hhds::GraphLibrary* outlib,
                                         std::string_view top_in, bool debug_color,
                                         const livehd::partition::Body_builder& hook,
                                         livehd::partition::Flatten_mode flatten, bool want_pre_bodies) {
  std::string               top{top_in};
  std::vector<hhds::Graph*> order;
  auto*                     g = resolve_order(graphs, top, order);
  if (g == nullptr) {
    livehd::diag::err("pass.partition", "no-top", "unsupported")
        .msg("partition: top module '{}' not found in the input library", top)
        .fatal();
    return false;
  }

  if (flatten_resolved(g, flatten)) {
    // Inline the hierarchy into a scratch def in the output library, run ONE
    // Partitioner on it (top's own name), then drop the scratch def — it must
    // never persist or be emitted. A hierarchy-less top skips the clone.
    hhds::Graph*                 flat_src = g;
    std::shared_ptr<hhds::Graph> flat_holder;
    std::string                  flat_name;
    if (order.size() > 1) {
      flat_name   = top + "__flatten_tmp";
      flat_holder = livehd::partition::flatten_hierarchy(g, outlib, flat_name);
      if (!flat_holder) {
        return false;  // diag already emitted
      }
      flat_src = flat_holder.get();
    }
    Partitioner p(flat_src, outlib, top, debug_color, hook, /*flatten=*/true, /*fuse_colors=*/false, want_pre_bodies);
    bool        ok = p.run();
    if (flat_holder) {
      outlib->delete_graph(flat_holder);
      outlib->delete_graphio(flat_name);
    }
    return ok;
  }

  const bool fuse_colors = coloring_packed(g);
  for (auto* def : order) {
    Partitioner p(def, outlib, std::string{def->get_name()}, debug_color, hook, /*flatten=*/false, fuse_colors,
                  want_pre_bodies);
    if (!p.run()) {
      return false;
    }
  }
  return true;
}

livehd::partition::Flatten_mode livehd::partition::parse_flatten_mode(std::string_view v, std::string_view pass) {
  if (v == "auto" || v.empty()) {
    return Flatten_mode::automatic;
  }
  if (v == "true" || v == "1") {
    return Flatten_mode::on;
  }
  if (v == "false" || v == "0") {
    return Flatten_mode::off;
  }
  livehd::diag::err(pass, "bad-flatten", "io").msg("{}: flatten must be auto|true|false, got '{}'", pass, v).fatal();
  return Flatten_mode::off;
}

void Pass_partition::partition(Eprp_var& var) {
  auto top = std::string{var.get("top", "")};
  auto out = std::string{var.get("out", "")};
  bool dbg = var.get("debug_color", "false") != "false" && var.get("debug_color", "false") != "0";
  auto flatten = livehd::partition::parse_flatten_mode(var.get("flatten", "auto"), "pass.partition");

  if (out.empty()) {
    // Stats-only mode: count colors/regions/ports per reachable def without
    // building an output library. Flatten applies here too — the reported
    // decomposition must match what an emit run would build.
    std::string               t = top;
    std::vector<hhds::Graph*> order;
    auto*                     g = resolve_order(var.graphs, t, order);
    if (g == nullptr) {
      livehd::diag::err("pass.partition", "no-top", "unsupported")
          .msg("partition: top module '{}' not found in the input library", t)
          .fatal();
      return;
    }
    if (flatten_resolved(g, flatten)) {
      auto* lib = g->get_io() ? g->get_io()->get_library() : nullptr;
      hhds::Graph*                 flat_src = g;
      std::shared_ptr<hhds::Graph> flat_holder;
      std::string                  flat_name;
      if (order.size() > 1 && lib != nullptr) {
        // Scratch def in the graph's OWN library: stats mode never saves it,
        // and it is deleted right after (same lifecycle as the emit path).
        flat_name   = t + "__flatten_tmp";
        flat_holder = livehd::partition::flatten_hierarchy(g, lib, flat_name);
        if (!flat_holder) {
          return;  // diag already emitted
        }
        flat_src = flat_holder.get();
      }
      Partitioner p(flat_src, nullptr, t, dbg, {}, /*flatten=*/true);
      p.report_stats();
      if (flat_holder) {
        lib->delete_graph(flat_holder);
        flat_holder = nullptr;
        lib->delete_graphio(flat_name);
      }
      return;
    }
    const bool fuse_colors = coloring_packed(g);
    for (auto* def : order) {
      Partitioner p(def, nullptr, std::string{def->get_name()}, dbg, {}, /*flatten=*/false, fuse_colors);
      p.report_stats();
    }
    return;
  }

  auto& outlib = livehd::Hhds_graph_library::instance(out);
  build_decomposition(var.graphs, &outlib, top, dbg, {}, flatten);
}
