// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "proof_prep.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "attrs.hpp"
#include "cell.hpp"
#include "gtest/gtest.h"
#include "inline_sub.hpp"
#include "inline_sub_reference.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "sim_program.hpp"
#include "split_selfref.hpp"
#include "str_tools.hpp"

namespace {

namespace gu = livehd::graph_util;
namespace se = livehd::single_edge;

// The port declaration order of `split`, the definition both sides KEEP that
// every `wrap` instantiates. Its clone stays a Sub in the host and gets a pin
// per declared port up front, in declaration order when that is cheap for
// hhds (graph/inline_sub.cpp mints_in_linear_steps).
enum class Split_order {
  // pass.synth's input splitter (`__livehd_abc_input_bits_<W>`): input `a` at
  // port 1, outputs b<W-1>..b0 declared at ports W+1 DOWN to 2. Every pin
  // lands next to the head of the clone's pin list: declaration order.
  splitter,
  // 24 inputs at ascending ports, then 24 outputs above them declared in
  // DESCENDING port order: in declaration order each output would walk past
  // every input, so the clone is minted in one ascending pass instead.
  skewed,
};

// One side of a comparison whose hierarchy the other side does not have
// (a synthesis partition's region modules against the RTL): everything but
// `top`, `kept` and `split` is absorbed, so lec splices it into those hosts.
//
//   leaf(a:8, c) -> (y:8, z:8)   y = a + 3; z = flop q(y) clocked by c;
//                                carries a simulation_init program
//   split(...)                   kept by both sides (see Split_order)
//   wrap(x:8, c) -> (o:8, p:4, s:1)
//                                named `l0` and an UNNAMED leaf in a chain;
//                                p is narrower than its 8-bit driver (a fit);
//                                s = two outputs of a named `sp` split, Or-ed
//   wide_leaf(i<k>) -> (o<k>)    o<k> = ~i<k>, `ports` lanes
//   wide(i<k>) -> (o<k>)         a named `core` wide_leaf, every port wired
//   kept(x:8, c) -> (o:8)        two chained wraps; the other side keeps it
//   top: `wraps` chained wraps (even ones named), their `p`s Or-ed into `pr`
//        and `s`es into `s`; `wides` wides on input `w`, Or-ed into `r`; a
//        kept with a constant clock
//
// Each wrap brings two absorbed leaf instances in with its splice, so the
// clones land after everything already in the host -- the shape that made a
// re-scan of the host per splice quadratic -- and `wide` is the wide nested
// boundary that made each pin lookup on a clone a list walk. The builder
// itself stays linear: pins in ascending port order, 2-input Or chains.
struct Design {
  std::unique_ptr<hhds::GraphLibrary>       lib = std::make_unique<hhds::GraphLibrary>();
  std::shared_ptr<hhds::Graph>              top;
  std::vector<std::shared_ptr<hhds::Graph>> graphs;
};

std::shared_ptr<hhds::GraphIO> make_io(hhds::GraphLibrary& lib, std::string_view name,
                                       const std::vector<std::pair<std::string, int>>& ins,
                                       const std::vector<std::pair<std::string, int>>& outs) {
  auto          io   = lib.create_io(std::string{name});
  hhds::Port_id port = 0;
  for (const auto& [n, bits] : ins) {
    io->add_input(n, port++);
    io->set_bits(n, bits);
    io->set_unsign(n, true);
  }
  for (const auto& [n, bits] : outs) {
    io->add_output(n, port++);
    io->set_bits(n, bits);
    io->set_unsign(n, true);
  }
  return io;
}

// `split` with explicit ports, in declaration order; returns its two read
// outputs and the input names.
struct Split_def {
  std::shared_ptr<hhds::GraphIO> io;
  std::shared_ptr<hhds::Graph>   graph;
  std::vector<std::string>       inputs;
  std::string                    out_a, out_b;
};

Split_def make_split(hhds::GraphLibrary& lib, Split_order order) {
  Split_def s;
  s.io        = lib.create_io("split");
  auto add_in = [&](const std::string& n, hhds::Port_id port, int bits) {
    s.io->add_input(n, port);
    s.io->set_bits(n, bits);
    s.io->set_unsign(n, true);
    s.inputs.push_back(n);
  };
  auto add_out = [&](const std::string& n, hhds::Port_id port) {
    s.io->add_output(n, port);
    s.io->set_bits(n, 1);
    s.io->set_unsign(n, true);
  };
  if (order == Split_order::splitter) {
    constexpr int width = 8;
    add_in("a", 1, width);
    for (int b = width - 1; b >= 0; --b) {
      add_out(std::format("b{}", b), static_cast<hhds::Port_id>(b + 2));
    }
    s.out_a = "b0";
    s.out_b = "b5";
  } else {
    constexpr int lanes = 24;
    for (int k = 0; k < lanes; ++k) {
      add_in(std::format("i{}", k), static_cast<hhds::Port_id>(1 + k), 1);
    }
    for (int k = lanes - 1; k >= 0; --k) {
      add_out(std::format("o{}", k), static_cast<hhds::Port_id>(1 + lanes + k));
    }
    s.out_a = "o0";
    s.out_b = "o7";
  }
  s.graph = s.io->create_graph();  // bodiless here: the other side keeps it, so it is never spliced
  return s;
}

hhds::Node_class make_inst(hhds::Graph& g, const std::shared_ptr<hhds::GraphIO>& io, std::string_view name) {
  auto inst = gu::create_typed_node(g, Ntype_op::Sub);
  inst.set_subnode(io);
  if (!name.empty()) {
    inst.attr(hhds::attrs::name).set(std::string{name});
  }
  return inst;
}

hhds::Pin_class out_of(const hhds::Node_class& inst, std::string_view port) {
  auto pin = inst.create_driver_pin(inst.get_subnode_io()->get_output_port_id(std::string{port}));
  gu::set_ubits(pin, static_cast<int>(inst.get_subnode_io()->get_bits(std::string{port})));
  return pin;
}

hhds::Pin_class in_of(const hhds::Node_class& inst, std::string_view port) {
  return inst.create_sink_pin(inst.get_subnode_io()->get_input_port_id(std::string{port}));
}

// `acc | v` as a new 2-input Or (`v` alone for the first link).
hhds::Pin_class or_chain(hhds::Graph& g, const hhds::Pin_class& acc, const hhds::Pin_class& v, int bits) {
  if (acc.is_invalid()) {
    return v;
  }
  auto node = gu::create_typed_node(g, Ntype_op::Or);
  acc.connect_sink(gu::setup_sink_pid(node, 0));
  v.connect_sink(gu::setup_sink_pid(node, 0));
  auto o = node.create_driver_pin(0);
  gu::set_ubits(o, bits);
  return o;
}

Design build(int wraps, int ports, int wides = 1, Split_order split_order = Split_order::splitter) {
  Design d;
  auto&  lib = *d.lib;

  auto lio  = make_io(lib, "leaf", {{"a", 8}, {"c", 1}}, {{"y", 8}, {"z", 8}});
  auto leaf = lio->create_graph();
  {
    auto sum = gu::create_typed_node(*leaf, Ntype_op::Sum);
    leaf->get_input_pin("a").connect_sink(gu::setup_sink_by_name(sum, "A"));
    gu::create_const(*leaf, *Dlop::create_integer(3)).connect_sink(gu::setup_sink_by_name(sum, "A"));
    auto y = sum.create_driver_pin(0);
    gu::set_ubits(y, 8);
    y.connect_sink(leaf->get_output_pin("y"));
    auto q = gu::create_typed_node(*leaf, Ntype_op::Flop);
    q.attr(hhds::attrs::name).set("q");
    y.connect_sink(gu::setup_sink_by_name(q, "din"));
    leaf->get_input_pin("c").connect_sink(gu::setup_sink_by_name(q, "clock_pin"));
    auto qo = q.create_driver_pin(0);
    gu::set_ubits(qo, 8);
    qo.connect_sink(leaf->get_output_pin("z"));
    leaf->get_input_node()
        .attr(livehd::attrs::simulation_init)
        .set(livehd::sim_ir::encode(livehd::sim_ir::Node{"seq", "leaf", 0, false, {}}));
  }

  const auto split = make_split(lib, split_order);

  auto wio  = make_io(lib, "wrap", {{"x", 8}, {"c", 1}}, {{"o", 8}, {"p", 4}, {"s", 1}});
  auto wrap = wio->create_graph();
  {
    auto l0 = make_inst(*wrap, lio, "l0");
    auto l1 = make_inst(*wrap, lio, "");  // transparent
    wrap->get_input_pin("x").connect_sink(in_of(l0, "a"));
    wrap->get_input_pin("c").connect_sink(in_of(l0, "c"));
    out_of(l0, "y").connect_sink(in_of(l1, "a"));
    wrap->get_input_pin("c").connect_sink(in_of(l1, "c"));
    out_of(l1, "z").connect_sink(wrap->get_output_pin("o"));
    out_of(l0, "z").connect_sink(wrap->get_output_pin("p"));  // 8 bits into a 4-bit port
    auto sp = make_inst(*wrap, split.io, "sp");
    for (const auto& in : split.inputs) {
      wrap->get_input_pin("x").connect_sink(in_of(sp, in));
    }
    auto sa = out_of(sp, split.out_a);
    auto sb = out_of(sp, split.out_b);
    or_chain(*wrap, sa, sb, 1).connect_sink(wrap->get_output_pin("s"));
  }

  std::vector<std::pair<std::string, int>> lanes_in, lanes_out;
  for (int k = 0; k < ports; ++k) {
    lanes_in.emplace_back(std::format("i{}", k), 1);
    lanes_out.emplace_back(std::format("o{}", k), 1);
  }
  auto wlio      = make_io(lib, "wide_leaf", lanes_in, lanes_out);
  auto wide_leaf = wlio->create_graph();
  auto wdio      = make_io(lib, "wide", lanes_in, lanes_out);
  auto wide      = wdio->create_graph();
  {
    auto core = make_inst(*wide, wlio, "core");
    for (int k = 0; k < ports; ++k) {
      auto inv = gu::create_typed_node(*wide_leaf, Ntype_op::Not);
      wide_leaf->get_input_pin(std::format("i{}", k)).connect_sink(gu::setup_sink_pid(inv, 0));
      auto o = inv.create_driver_pin(0);
      gu::set_ubits(o, 1);
      o.connect_sink(wide_leaf->get_output_pin(std::format("o{}", k)));
      wide->get_input_pin(std::format("i{}", k)).connect_sink(in_of(core, std::format("i{}", k)));
    }
    for (int k = 0; k < ports; ++k) {
      out_of(core, std::format("o{}", k)).connect_sink(wide->get_output_pin(std::format("o{}", k)));
    }
  }

  auto kio  = make_io(lib, "kept", {{"x", 8}, {"c", 1}}, {{"o", 8}});
  auto kept = kio->create_graph();
  {
    auto k0 = make_inst(*kept, wio, "k0");
    auto k1 = make_inst(*kept, wio, "k1");
    kept->get_input_pin("x").connect_sink(in_of(k0, "x"));
    kept->get_input_pin("c").connect_sink(in_of(k0, "c"));
    out_of(k0, "o").connect_sink(in_of(k1, "x"));
    kept->get_input_pin("c").connect_sink(in_of(k1, "c"));
    out_of(k1, "o").connect_sink(kept->get_output_pin("o"));
  }

  auto tio = make_io(lib, "top", {{"x", 8}, {"c", 1}, {"w", 1}}, {{"o", 8}, {"pr", 4}, {"r", 1}, {"k", 8}, {"s", 1}});
  d.top    = tio->create_graph();
  auto&           top   = *d.top;
  hhds::Pin_class chain = top.get_input_pin("x");
  hhds::Pin_class pr, s;
  for (int i = 0; i < wraps; ++i) {
    auto w = make_inst(top, wio, i % 2 == 0 ? std::format("w{}", i) : std::string{});
    chain.connect_sink(in_of(w, "x"));
    top.get_input_pin("c").connect_sink(in_of(w, "c"));
    chain = out_of(w, "o");
    auto p = out_of(w, "p");
    auto q = out_of(w, "s");
    pr     = or_chain(top, pr, p, 4);
    s      = or_chain(top, s, q, 1);
  }
  chain.connect_sink(top.get_output_pin("o"));
  pr.connect_sink(top.get_output_pin("pr"));
  s.connect_sink(top.get_output_pin("s"));
  hhds::Pin_class r;
  for (int j = 0; j < wides; ++j) {
    auto wd = make_inst(top, wdio, std::format("wd{}", j));
    for (int k = 0; k < ports; ++k) {
      top.get_input_pin("w").connect_sink(in_of(wd, std::format("i{}", k)));
    }
    std::vector<hhds::Pin_class> outs;  // every pin first: interleaving creates on another node defeats hhds's append cursor
    for (int k = 0; k < ports; ++k) {
      outs.push_back(out_of(wd, std::format("o{}", k)));
    }
    for (const auto& o : outs) {
      r = or_chain(top, r, o, 1);
    }
  }
  r.connect_sink(top.get_output_pin("r"));
  auto kp = make_inst(top, kio, "kp");
  top.get_input_pin("x").connect_sink(in_of(kp, "x"));
  gu::create_const(top, *Dlop::create_integer(0)).connect_sink(in_of(kp, "c"));
  out_of(kp, "o").connect_sink(top.get_output_pin("k"));

  d.graphs = {d.top, kept, wrap, leaf, wide, wide_leaf, split.graph};
  return d;
}

// The other side: it keeps `top`, `kept` and `split` only.
std::vector<std::shared_ptr<hhds::Graph>> other_side(hhds::GraphLibrary& lib) {
  return {lib.create_io("top")->create_graph(), lib.create_io("kept")->create_graph(), lib.create_io("split")->create_graph()};
}

// inline_instances_missing_from_other_side as it stood before its linear-cost
// rework, driving the inliner as it stood then (tests/inline_sub_reference):
// splice the FIRST candidate of a fresh scan of the host, then scan again.
// The reference the worklist and the live inliner must reproduce exactly.
size_t rescan_reference(const std::vector<std::shared_ptr<hhds::Graph>>& side_graphs,
                        const std::vector<std::shared_ptr<hhds::Graph>>& other_graphs, hhds::Graph* side_g) {
  absl::flat_hash_set<std::string> other_defs;
  for (const auto& sp : other_graphs) {
    other_defs.insert(std::string{sp->get_name()});
    other_defs.insert(str_tools::canonical_entity_name(sp->get_name()));
  }
  auto other_has_def = [&](std::string_view n) {
    return other_defs.contains(std::string{n}) || other_defs.contains(str_tools::canonical_entity_name(n));
  };
  std::vector<hhds::Graph*> hosts{side_g};
  for (const auto& sp : side_graphs) {
    if (sp.get() != side_g && other_has_def(sp->get_name())) {
      hosts.push_back(sp.get());
    }
  }
  size_t done = 0;
  for (auto* host : hosts) {
    const size_t before = done;
    for (bool spliced = true; spliced;) {
      spliced = false;
      for (auto n : host->body().nodes()) {
        if (gu::type_op_of(n) != Ntype_op::Sub) {
          continue;
        }
        auto sio = n.get_subnode_io();
        auto def = n.get_subnode_graph();
        if (sio == nullptr || other_has_def(sio->get_name()) || n.is_loop_subnode() || !def
            || livehd::latch_contract::match_icg_def(def.get()).has_value()) {
          continue;
        }
        if (livehd::graph_util_reference::inline_sub_instance(host, n, "pass.lec")) {
          spliced = true;
          ++done;
          break;
        }
      }
    }
    if (done != before) {
      (void)gu::split_packed_cycle_slices(host);
    }
  }
  return done;
}

// Everything that makes two graphs the same graph: node ids, ops, names,
// widths, signs, wire names, every edge, and the input node's simulation_init
// program. `raw_pins` prints each pin by its hhds id; otherwise as
// node:port:direction, which is what it denotes (an id only says when hhds
// minted it).
std::string dump(hhds::Graph& g, bool raw_pins = true) {
  auto pin_id = [raw_pins](const hhds::Pin_class& p) {
    if (raw_pins) {
      return std::to_string(p.get_debug_pid());
    }
    return std::format("{}:{}:{}", p.get_master_node().get_debug_nid(), p.get_port_id(), p.is_driver() ? 'd' : 's');
  };
  std::string out = std::format("graph {}\n", g.get_name());
  if (auto a = g.get_input_node().attr(livehd::attrs::simulation_init); a.has()) {
    out += std::format("sim_init {}\n", a.get());
  }
  auto pin_line = [&](const hhds::Pin_class& p) {
    std::string nm;
    if (auto a = p.attr(livehd::attrs::pin_name); a.has()) {
      nm = std::string{a.get()};
    }
    out += std::format("  pin {} port {} bits {} unsign {} name '{}'\n",
                       pin_id(p),
                       p.get_port_id(),
                       gu::bits_of(p),
                       gu::is_unsign(p),
                       nm);
  };
  for (auto n : g.body().nodes()) {
    out += std::format("node {} op {} name '{}'",
                       n.get_debug_nid(),
                       static_cast<int>(gu::type_op_of(n)),
                       gu::has_name(n) ? std::string{gu::node_name_of(n)} : std::string{});
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      out += std::format(" sub {}", n.get_subnode_io()->get_name());
    }
    out += "\n";
    for (auto p : n.inp_sorted_pins()) {
      pin_line(p);
      for (auto drv : p.get_driver_pins()) {
        out += std::format("    from {}\n", pin_id(drv));
      }
    }
    for (auto p : n.out_sorted_pins()) {
      pin_line(p);
    }
  }
  for (const auto& d : g.get_io()->get_output_pin_decls()) {
    for (auto drv : g.get_output_pin(d.name).get_driver_pins()) {
      out += std::format("out {} from {}\n", d.name, pin_id(drv));
    }
  }
  return out;
}

int count_op(hhds::Graph& g, Ntype_op op) {
  int n = 0;
  for (auto node : g.body().nodes()) {
    n += gu::type_op_of(node) == op ? 1 : 0;
  }
  return n;
}

// Splices one design with the live worklist + inliner and its twin with the
// reference, checks what came out, and returns {live, reference} dumps of the
// two hosts (top, kept) printed with `raw_pins`.
std::vector<std::pair<std::string, std::string>> splice_both(Split_order order, bool raw_pins) {
  constexpr int wraps = 9;
  constexpr int ports = 37;
  constexpr int wides = 2;
  auto          a     = build(wraps, ports, wides, order);
  auto          b     = build(wraps, ports, wides, order);
  EXPECT_EQ(dump(*a.top), dump(*b.top));

  hhds::GraphLibrary               other_lib;
  const auto                       other = other_side(other_lib);
  absl::flat_hash_set<std::string> absorbed;
  const size_t                     got = se::inline_instances_missing_from_other_side({}, a.graphs, other, a.top.get(), &absorbed);
  const size_t                     ref = rescan_reference(b.graphs, other, b.top.get());
  // top: the wraps, their two leaves each, the wides and their cores; kept: 2 wraps + 4 leaves.
  EXPECT_EQ(got, static_cast<size_t>(3 * wraps + 2 * wides + 6));
  EXPECT_EQ(got, ref);
  EXPECT_EQ(absorbed, (absl::flat_hash_set<std::string>{"wrap", "leaf", "wide", "wide_leaf"}));
  // top keeps its `kept` instance and one `split` per wrap; kept, one per wrap.
  EXPECT_EQ(count_op(*a.top, Ntype_op::Sub), 1 + wraps);
  EXPECT_EQ(count_op(*a.graphs[1], Ntype_op::Sub), 2);
  EXPECT_EQ(count_op(*a.top, Ntype_op::Flop), 2 * wraps);
  EXPECT_EQ(count_op(*a.top, Ntype_op::Not), wides * ports);
  // One merged program per host, one child per spliced leaf.
  const auto program = livehd::sim_ir::decode(a.top->get_input_node().attr(livehd::attrs::simulation_init).get());
  EXPECT_EQ(program.kids.size(), static_cast<size_t>(2 * wraps));

  std::vector<std::pair<std::string, std::string>> dumps;
  for (size_t i : {size_t{0}, size_t{1}}) {  // top, kept
    dumps.emplace_back(dump(*a.graphs[i], raw_pins), dump(*b.graphs[i], raw_pins));
  }
  return dumps;
}

// Wall time of one-sided inlining on a fresh build(wraps, ports, 8), best of `reps`.
double inline_secs(int wraps, int ports, int reps) {
  double best = 0;
  for (int rep = 0; rep < reps; ++rep) {
    auto               d = build(wraps, ports, 8);
    hhds::GraphLibrary other_lib;
    const auto         other = other_side(other_lib);
    const auto         start = std::chrono::steady_clock::now();
    const size_t       got   = se::inline_instances_missing_from_other_side({}, d.graphs, other, d.top.get());
    const double       secs  = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(got, static_cast<size_t>(3 * wraps + 2 * 8 + 6));
    EXPECT_EQ(count_op(*d.top, Ntype_op::Sub), 1 + wraps);  // the kept instance and the splits
    EXPECT_EQ(count_op(*d.top, Ntype_op::Flop), 2 * wraps);
    EXPECT_EQ(count_op(*d.top, Ntype_op::Not), 8 * ports);
    best = rep == 0 ? secs : std::min(best, secs);
  }
  return best;
}

}  // namespace

// The worklist over the live inliner splices the same instances in the same
// order as re-scanning the host after every splice did over the inliner as it
// stood then, so the hosts come out as the SAME graph -- same node and pin ids,
// edges, names, fits and merged simulation_init program. The kept `split`
// clones have pass.synth's splitter port order (outputs declared at descending
// ports), which the live inliner still mints in declaration order.
TEST(ProofPrep, OneSidedInlineMatchesTheRescanOrder) {
  for (const auto& [live, ref] : splice_both(Split_order::splitter, true)) {
    EXPECT_EQ(live, ref);
  }
}

// A kept boundary whose declaration order would make hhds walk the clone's pin
// list per pin is minted in ascending port order instead: the same pins on the
// same nodes with the same edges, only minted (numbered) in another order.
TEST(ProofPrep, OneSidedInlineMatchesTheReferenceOnASkewedKeptBoundary) {
  for (const auto& [live, ref] : splice_both(Split_order::skewed, false)) {
    EXPECT_EQ(live, ref);
  }
  // ...and the ascending pass is what ran: the raw ids do differ.
  bool any_differs = false;
  for (const auto& [live, ref] : splice_both(Split_order::skewed, true)) {
    any_differs = any_differs || live != ref;
  }
  EXPECT_TRUE(any_differs);
}

// The cva6 netlist LEC sat >9 min splicing one-sided instances: a re-scan of
// the host per splice, a walk over the instance's sinks per resolved input,
// and a pin-list walk per pin of every wide clone -- each quadratic in a size
// this scales (absorbed instances per host, ports per nested boundary). Four
// times the design must cost about four times as much, not sixteen. At -c opt
// the large one is ~80k splices into one host plus eight 10k-port nested
// boundaries (~1.5 s; the re-scan form alone took 271 s on it). -c dbg runs a
// tenth of it for coverage (debug containers are ~40x slower) and is not timed.
TEST(ProofPrep, OneSidedInlineIsLinearInTheSplicedSize) {
#ifdef NDEBUG
  constexpr int scale = 10;
  constexpr int reps  = 2;  // best of two: a loaded machine only ever adds time
#else
  constexpr int scale = 1;
  constexpr int reps  = 1;
#endif
  constexpr int wraps = 500 * scale;
  constexpr int ports = 250 * scale;
  const double  small = inline_secs(wraps, ports, reps);
  const double  large = inline_secs(4 * wraps, 4 * ports, reps);
  std::cout << std::format("one-sided inline: {:.3f}s at 1x, {:.3f}s at 4x (ratio {:.1f})\n", small, large, large / small);
#ifdef NDEBUG
  EXPECT_LT(large, 8.0 * small) << "one-sided inlining grows faster than linearly";
#endif
}

// formal.timeout covers proof preparation: a deadline already passed splices
// nothing and says so, and the caller reports TIMEOUT-time.
TEST(ProofPrep, OneSidedInlineHonoursTheDeadline) {
  auto               d = build(4, 4);
  hhds::GraphLibrary other_lib;
  const auto         other     = other_side(other_lib);
  bool               timed_out = false;
  const size_t       got       = se::inline_instances_missing_from_other_side({},
                                                                  d.graphs,
                                                                  other,
                                                                  d.top.get(),
                                                                  nullptr,
                                                                  std::chrono::steady_clock::now() - std::chrono::seconds(1),
                                                                  &timed_out);
  EXPECT_TRUE(timed_out);
  EXPECT_EQ(got, 0U);
  EXPECT_GT(count_op(*d.top, Ntype_op::Sub), 0);

  timed_out = true;  // reset by the call
  EXPECT_EQ(se::inline_instances_missing_from_other_side({},
                                                         d.graphs,
                                                         other,
                                                         d.top.get(),
                                                         nullptr,
                                                         std::chrono::steady_clock::now() + std::chrono::hours(1),
                                                         &timed_out),
            static_cast<size_t>(3 * 4 + 2 + 6));
  EXPECT_FALSE(timed_out);
}

// ...and so does the rest of proof preparation: a deadline already passed
// stops prepare_time_base before its first step (TIMEOUT-time, no query, the
// sides untouched), and one in the future changes nothing about what it does.
TEST(ProofPrep, TimeBasePreparationHonoursTheDeadline) {
  auto prepare = [](std::chrono::steady_clock::time_point deadline) {
    auto                      ref  = build(2, 3);
    auto                      impl = build(2, 3);
    std::vector<hhds::Graph*> ref_defs, impl_defs;
    for (size_t i = 1; i < ref.graphs.size(); ++i) {
      ref_defs.push_back(ref.graphs[i].get());
      impl_defs.push_back(impl.graphs[i].get());
    }
    const auto before = dump(*ref.top) + dump(*impl.top);
    const auto tb     = se::prepare_time_base({}, ref.top.get(), ref_defs, impl.top.get(), impl_defs, true, {}, deadline);
    return std::tuple{tb, before, dump(*ref.top) + dump(*impl.top)};
  };
  const auto [late, late_before, late_after] = prepare(std::chrono::steady_clock::now() - std::chrono::seconds(1));
  EXPECT_TRUE(late.timed_out);
  EXPECT_FALSE(late.timeout_stage.empty());
  EXPECT_FALSE(late.applied);
  EXPECT_EQ(late_before, late_after) << "stopped before the first step: nothing rewritten";

  const auto [none, none_before, none_after]       = prepare({});
  const auto [on_time, on_time_before, on_time_after] = prepare(std::chrono::steady_clock::now() + std::chrono::hours(1));
  EXPECT_FALSE(none.timed_out);
  EXPECT_FALSE(on_time.timed_out);
  EXPECT_EQ(on_time.applied, none.applied);
  EXPECT_EQ(on_time.declined, none.declined);
  EXPECT_EQ(on_time.error, none.error);
  EXPECT_EQ(on_time.recipe_steps, none.recipe_steps);
  EXPECT_EQ(on_time_after, none_after);
}

// A shared definition whose register clock leaves it: `wrap` buffers its `ck`
// port into its flop, and top drives one instance's port through a BUF cell
// and the other's through an INV cell. The second occurrence of wrap's buffer
// leads to a different top cell, so the walk is per occurrence: both top
// cells and wrap's buffer are spliced.
TEST(ProofPrep, HierClockConeWalksEveryOccurrence) {
  // The netlist declares its cells body-less; their behavior is the --lib
  // models, in a library of their own.
  hhds::GraphLibrary lib, cell_lib;
  se::Cell_models    models;
  const auto         cell = [&](const char* name, Ntype_op op) {
    for (auto* l : {&lib, &cell_lib}) {
      auto io = l->create_io(name);
      io->add_input("A", 1);
      io->add_output("Y", 2);
      io->set_bits("A", 1);
      io->set_bits("Y", 1);
    }
    auto g = cell_lib.find_io(name)->create_graph();
    if (op == Ntype_op::Not) {
      auto n = gu::create_typed_node(*g, Ntype_op::Not, 1);
      g->get_input_pin("A").connect_sink(n.create_sink_pin(0));
      auto y = n.create_driver_pin(0);
      gu::set_ubits(y, 1);
      y.connect_sink(g->get_output_pin("Y"));
    } else {
      g->get_input_pin("A").connect_sink(g->get_output_pin("Y"));
    }
    const auto decl = lib.find_io(name);
    models.emplace(decl->get_gid(), g.get());
    return decl;
  };
  const auto buf = cell("BUF", Ntype_op::And), inv = cell("INV", Ntype_op::Not);
  const auto instance = [](hhds::Graph& host, const std::shared_ptr<hhds::GraphIO>& decl, const hhds::Pin_class& a) {
    auto n = gu::create_typed_node(host, Ntype_op::Sub);
    n.set_subnode(decl);
    a.connect_sink(n.create_sink_pin(1));
    auto y = n.create_driver_pin(2);
    gu::set_ubits(y, 1);
    return y;
  };
  auto wio = lib.create_io("wrap");
  wio->add_input("ck", 1);
  wio->add_input("d", 2);
  wio->add_output("q", 3);
  for (const char* p : {"ck", "d", "q"}) {
    wio->set_bits(p, 1);
  }
  auto wrap = wio->create_graph();
  auto flop = gu::create_typed_node(*wrap, Ntype_op::Flop, 1);
  instance(*wrap, buf, wrap->get_input_pin("ck")).connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  wrap->get_input_pin("d").connect_sink(gu::setup_sink_by_name(flop, "din"));
  auto q = flop.create_driver_pin(0);
  gu::set_ubits(q, 1);
  q.connect_sink(wrap->get_output_pin("q"));
  auto tio = lib.create_io("top");
  tio->add_input("clk", 1);
  tio->add_input("a", 2);
  tio->add_input("b", 3);
  tio->add_output("qa", 4);
  tio->add_output("qb", 5);
  for (const char* p : {"clk", "a", "b", "qa", "qb"}) {
    tio->set_bits(p, 1);
  }
  auto top = tio->create_graph();
  for (const auto& [clock, data, out] : {
           std::tuple{buf, "a", "qa"},
           std::tuple{inv, "b", "qb"}
  }) {
    auto w = gu::create_typed_node(*top, Ntype_op::Sub);
    w.set_subnode(wio);
    instance(*top, clock, top->get_input_pin("clk")).connect_sink(w.create_sink_pin(1));
    top->get_input_pin(data).connect_sink(w.create_sink_pin(2));
    auto o = w.create_driver_pin(3);
    gu::set_ubits(o, 1);
    o.connect_sink(top->get_output_pin(out));
  }
  const auto cells = [&](hhds::Graph& g) {
    int n = 0;
    for (auto node : g.body().nodes()) {
      const auto io  = node.get_subnode_io();
      n             += io && (io->get_name() == "BUF" || io->get_name() == "INV");
    }
    return n;
  };
  ASSERT_EQ(cells(*top), 2);
  ASSERT_EQ(cells(*wrap), 1);
  EXPECT_EQ(se::inline_hier_clock_lib_cells(models, top.get()), "");
  EXPECT_EQ(cells(*top), 0);
  EXPECT_EQ(cells(*wrap), 0);
}
