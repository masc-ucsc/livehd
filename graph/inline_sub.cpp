// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inline_sub.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "cell.hpp"
#include "diag.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "sim_program.hpp"
#include "synth_policy.hpp"

namespace livehd::graph_util {

namespace {

class Sub_inliner {
public:
  Sub_inliner(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass, hhds::Graph* def = nullptr,
              bool name_state = false, bool prefix_instance = true, bool inherit_color = false,
              const std::optional<std::string>& state_name = std::nullopt)
      : parent_(parent)
      , inst_(inst)
      , from_pass_(from_pass)
      , def_(def)
      , name_state_(name_state)
      , prefix_instance_(prefix_instance)
      , inherit_color_(inherit_color)
      , state_name_(state_name) {}

  bool run();

private:
  hhds::Graph*               parent_;
  hhds::Node_class           inst_;
  std::string_view           from_pass_;
  // Explicit body, for an instance whose def is NOT in the parent's own graph
  // library (a `lec --lib` cell model lives in a side library, so
  // get_subnode_graph() is null for it). nullptr = resolve the ordinary way.
  hhds::Graph*               def_             = nullptr;
  // Name an UNNAMED spliced state node after the instance (see the header): a
  // cell model's internal flop carries no name attr, so the cut would otherwise
  // be keyed on a synthesized net name that corresponds to nothing.
  bool                       name_state_      = false;
  // False drops a NAMED instance's component too; an unnamed or `__flat___*`
  // instance adds none either way (logical_instance_prefix).
  bool                       prefix_instance_ = true;
  bool                       inherit_color_   = false;
  // Replaces the body's own name on every spliced flop/latch (see the header):
  // non-empty is prefixed like any name, empty leaves the node unnamed.
  std::optional<std::string> state_name_;
  hhds::Graph*               child_ = nullptr;
  std::string                prefix_;

  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> node_map_;   // child node -> parent clone
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class>   pin_cache_;  // child driver -> parent driver
  absl::flat_hash_map<std::string, uint32_t>              in_name2pid_;
  absl::flat_hash_map<uint32_t, std::string>              out_pid2name_;
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class>   fit_source_;  // fit_boundary driver -> the driver it fits
  bool                                                    failed_ = false;

  void                          fit_boundary();
  [[nodiscard]] hhds::Pin_class add_fit(const hhds::Pin_class& driver, int bits, bool is_signed);
  [[nodiscard]] bool            fit_loops_back(hhds::Pin_class src, const hhds::Pin_class& sink) const;
  [[noreturn]] void             report_cycle(const hhds::Pin_class& at);
  void                          create_nodes();
  void                          wire_edges();
  void                          rewire_instance_outputs();
  [[nodiscard]] hhds::Pin_class resolve_driver(const hhds::Pin_class& d);
  [[nodiscard]] hhds::Pin_class resolve_output_of(std::string_view oname);
  [[nodiscard]] hhds::Pin_class driver_feeding_inst_port(uint32_t pid);
  void                          carry_node_attrs(const hhds::Node_class& orig, const hhds::Node_class& neo);
  void                          carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo);
};

void Sub_inliner::carry_node_attrs(const hhds::Node_class& orig, const hhds::Node_class& neo) {
  const auto op = type_op_of(orig);
  auto policy = orig.attr(livehd::attrs::synth_policy).has() ? synth_attr::read(orig.attr(livehd::attrs::synth_policy).get()) : synth_attr::Policy{};
  if (auto a = inst_.attr(livehd::attrs::synth_policy); a.has()) {auto call=synth_attr::read(a.get()); if(!call.contains("_specialized")) synth_attr::rescope(policy,call);}
  if (!policy.empty()) neo.attr(livehd::attrs::synth_policy).set(synth_attr::write(policy));
  if (state_name_.has_value() && (is_type_flop(orig) || op == Ntype_op::Latch)) {
    if (!state_name_->empty()) {
      neo.attr(hhds::attrs::name).set(prefix_ + *state_name_);
    } else if (name_state_ && is_type_flop(neo) && !prefix_.empty()) {
      neo.attr(hhds::attrs::name).set(prefix_.substr(0, prefix_.size() - 1));
    }
  } else if (has_name(orig)) {
    auto nm = std::string{node_name_of(orig)};
    // fproperty/lgassert marker Subs pack "<kind>\x1f<loc>\x1f<msg>" into the name
    // attr; a prefix would corrupt the kind field (cgen/pass.formal parse it up to
    // the first \x1f -- an assume would re-emit as an assert). The payload is
    // parsed, never used as an identifier: copy it verbatim.
    neo.attr(hhds::attrs::name).set(nm.find('\x1f') == std::string::npos ? prefix_ + nm : nm);
  } else if (name_state_ && is_type_flop(neo) && !prefix_.empty()) {
    // Unnamed spliced STATE: take the instance's own name (prefix_ minus its
    // trailing '.'). Done here, once per spliced node, instead of re-walking the
    // parent body after every inline.
    neo.attr(hhds::attrs::name).set(prefix_.substr(0, prefix_.size() - 1));
  }
  if (auto a = orig.attr(livehd::attrs::lut); a.has()) {
    neo.attr(livehd::attrs::lut).set(std::string{a.get()});
  }
  if (auto a = orig.attr(hhds::attrs::srcid); a.has() && a.get() != 0) {
    auto newid = parent_->source_locator().import_from(child_->source_locator(), a.get());
    neo.attr(hhds::attrs::srcid).set(newid);
  }
  if (inherit_color_ && has_color(inst_)) {
    set_color(neo, color_of(inst_));
  }
  // A color on an absorbed node is meaningless in the parent's id space (colors
  // are per-def), and pass.color recolors the parent right after. Dropping it is
  // the honest move: carrying it would silently claim the child's region ids mean
  // something here.
  if (auto a = orig.attr(livehd::attrs::proven); a.has()) {
    neo.attr(livehd::attrs::proven).set(a.get());
  }
  if (auto a = orig.attr(livehd::attrs::runtime_check); a.has()) {
    neo.attr(livehd::attrs::runtime_check).set(a.get());
  }
  if (auto a = orig.attr(livehd::attrs::memory_async_reset); a.has()) {
    neo.attr(livehd::attrs::memory_async_reset).set(a.get());
  }
  // Scalar-replacement provenance, or the spliced bits of a blasted register
  // stop being recognizable as ONE register. pass.semdiff reassembles a mapped
  // `q_0..q_15` into the ref's single wide `q` from exactly these five, and the
  // LEC inlines the netlist's region defs into the top before it pairs state --
  // so dropping them here is what left br_arb_weighted_rr with 96 unpaired impl
  // flops, a flop-cut inductive miter built from the 18 that did pair, and a
  // PROVEN that disagreed with a real lgyosys counterexample.
  //
  // `aggregate_origin` is a hierarchical NAME and follows the same prefixing
  // rule the `name` attr does above; the ordinals are positions inside the
  // register and are position-invariant, so they copy verbatim.
  if (auto a = orig.attr(livehd::attrs::aggregate_origin); a.has() && !a.get().empty()) {
    neo.attr(livehd::attrs::aggregate_origin).set(prefix_ + std::string{a.get()});
  }
  // The ordinals carry no policy, so they go through the ONE table-driven copy
  // (graph/attr_carry.hpp) rather than yet another hand-kept if-chain -- that
  // header exists because these chains drift.
  carry_attr<livehd::attrs::aggregate_source_index_t>(orig, neo);
  carry_attr<livehd::attrs::aggregate_lane_ordinal_t>(orig, neo);
  carry_attr<livehd::attrs::aggregate_bit_offset_t>(orig, neo);
  carry_attr<livehd::attrs::aggregate_bit_width_t>(orig, neo);
  carry_attr<livehd::attrs::aggregate_extent_t>(orig, neo);
}

void Sub_inliner::carry_driver_attrs(const hhds::Pin_class& orig, const hhds::Pin_class& neo) {
  if (auto b = bits_of(orig); b != 0) {
    set_bits(neo, b);
  }
  if (!is_unsign(orig)) {
    set_sign(neo);
  }
  if (auto a = orig.attr(livehd::attrs::pin_name); a.has()) {
    set_pin_name(neo, prefix_ + std::string{a.get()});  // wire names are per-def: prefix like node names
  }
  if (auto o = orig.attr(livehd::attrs::pin_offset); o.has()) {
    neo.attr(livehd::attrs::pin_offset).set(o.get());
  }
}

// Spell every unfit connection as the fit its port performs (inline_sub.hpp,
// Sub port boundary), in the PARENT and before anything is cloned, so the
// splice below reads the fitted value through ordinary parent nodes. Outputs
// go first: an instance output that feeds one of the instance's own inputs is
// then read through its output fit, which the input check sees as the driver
// it really is.
void Sub_inliner::fit_boundary() {
  const auto gio = child_->get_io();

  // An output crosses TWO declarations, exactly as cgen spells it: the
  // callee's port, then the parent net the instance output drives (the
  // instance pin's own stamp; bitwidth seeds it from the port, so the two
  // normally agree). When that net is no wider than the port, only its low
  // bits survive and the port leaves those untouched, so the net alone decides.
  struct Out_fit {
    hhds::Pin_class                   drv;    // the instance's output driver pin
    std::vector<std::pair<int, bool>> steps;  // (bits, signed), applied in order
  };
  std::vector<Out_fit> out_fits;
  for (auto drv : inst_.out_sorted_pins()) {  // only outputs somebody reads
    const auto oit = out_pid2name_.find(static_cast<uint32_t>(drv.get_port_id()));
    if (oit == out_pid2name_.end()) {
      continue;
    }
    const auto opin  = child_->get_output_pin(oit->second);
    const auto inner = opin.is_invalid() ? hhds::Pin_class{} : opin.get_driver_pin();  // an output pin is a sink: one driver
    if (inner.is_invalid()) {
      continue;
    }
    const int  bits      = sub_port_width(opin, *gio, oit->second);
    const bool is_signed = !gio->is_unsign(oit->second);
    const int  net_bits  = bits_of(drv);
    const bool net_sign  = !is_unsign(drv);
    Out_fit    f{drv, {}};
    if (net_bits != 0 && net_bits <= bits) {
      if (!driver_fits_port(inner, net_bits, net_sign)) {
        f.steps.emplace_back(net_bits, net_sign);
      }
    } else {
      const bool port_step = !driver_fits_port(inner, bits, is_signed);
      if (port_step) {
        f.steps.emplace_back(bits, is_signed);
      }
      // A wider net extends the port's value by the port's sign.
      if (net_bits != 0
          && (port_step ? !range_fits_port(bits, is_signed, net_bits, net_sign) : !driver_fits_port(inner, net_bits, net_sign))) {
        f.steps.emplace_back(net_bits, net_sign);
      }
    }
    if (!f.steps.empty()) {
      out_fits.push_back(std::move(f));
    }
  }
  for (const auto& f : out_fits) {
    std::vector<hhds::Pin_class> readers;  // collect first: the rewire edits this fanout
    for (const auto& e : f.drv.out_edges()) {
      readers.push_back(e.sink);
    }
    auto fitted = f.drv;
    for (const auto& [bits, is_signed] : f.steps) {
      fitted = add_fit(fitted, bits, is_signed);
    }
    for (const auto& sink : readers) {
      sink.del_sink(f.drv);
      fitted.connect_sink(sink);
    }
  }

  // An input the child never reads needs no fit: nothing observes it.
  absl::flat_hash_map<uint32_t, std::string_view> in_pid2name;
  for (const auto& [name, pid] : in_name2pid_) {
    in_pid2name.emplace(pid, name);
  }
  struct In_fit {
    hhds::Pin_class driver;
    hhds::Pin_class sink;  // the instance's input sink pin
    int             bits;
    bool            is_signed;
  };
  std::vector<In_fit> in_fits;
  for (auto sink : inst_.inp_sorted_pins()) {
    const auto nit = in_pid2name.find(static_cast<uint32_t>(sink.get_port_id()));
    if (nit == in_pid2name.end()) {
      continue;
    }
    const auto ipin = child_->get_input_pin(std::string{nit->second});
    if (ipin.is_invalid() || !ipin.has_sink()) {
      continue;
    }
    const int  bits      = sub_port_width(ipin, *gio, nit->second);
    const bool is_signed = !gio->is_unsign(nit->second);
    for (auto driver : sink.get_driver_pins()) {
      if (!driver_fits_port(driver, bits, is_signed)) {
        in_fits.push_back(In_fit{driver, sink, bits, is_signed});
      }
    }
  }
  for (const auto& f : in_fits) {
    const auto fitted = add_fit(f.driver, f.bits, f.is_signed);
    f.sink.del_sink(f.driver);
    fitted.connect_sink(f.sink);
  }
}

// One fit node in the parent, colored like a clone of the body would be.
hhds::Pin_class Sub_inliner::add_fit(const hhds::Pin_class& driver, int bits, bool is_signed) {
  auto fitted = fit_to_port(*parent_, driver, bits, is_signed);
  if (inherit_color_ && has_color(inst_)) {
    set_color(fitted.get_master_node(), color_of(inst_));
  }
  fit_source_.emplace(fitted, driver);
  return fitted;
}

// Does `src` reach `sink` back through fit nodes only? A feed-through cycle
// (`y = a` in the child, the parent wiring inst.y back into inst.a) is caught
// by resolve_driver's alias walk, but once fit_boundary put a fit on that loop
// the walk stops at the fit's driver, and rewiring inst.y's reader -- the fit
// itself -- to that driver would close a silent combinational self-loop.
bool Sub_inliner::fit_loops_back(hhds::Pin_class src, const hhds::Pin_class& sink) const {
  const auto target = sink.get_master_node();
  for (auto it = fit_source_.find(src); it != fit_source_.end(); it = fit_source_.find(src)) {
    if (src.get_master_node() == target) {
      return true;
    }
    src = it->second;
  }
  return false;
}

void Sub_inliner::report_cycle(const hhds::Pin_class& at) {
  failed_ = true;
  livehd::diag::err(from_pass_, "inline-cycle", "unsupported")
      .msg("inline: combinational feed-through cycle through instance '{}' at '{}{}'",
           default_instance_name(inst_),
           prefix_,
           wire_name(at))
      .fatal();
}

void Sub_inliner::create_nodes() {
  for (auto n : child_->body().nodes(hhds::Node_order::forward)) {
    if (n.is_invalid() || is_builtin_node(n) || node_map_.contains(n)) {
      continue;
    }
    auto op = type_op_of(n);
    if (op == Ntype_op::IO || op == Ntype_op::Invalid) {
      continue;  // IO dissolves into the boundary; constants are recreated per consuming edge
    }
    auto neo = create_typed_node(*parent_, op);
    if (op == Ntype_op::Sub) {
      // A sub inside the child stays a sub: this transform is single-level, and
      // the target def already lives in the same library the parent resolves
      // through, so the gid binds without cloning anything.
      if (auto io = n.get_subnode_io()) {
        if (auto loop = n.subnode_loop()) {
          neo.set_subnode(io, *loop);
        } else {
          neo.set_subnode(io);
        }

        // Preserve the complete call boundary, including declared ports with
        // no parent-side edge.  HHDS hierarchy resolution crosses a child's
        // output node through the corresponding instance driver pin even when
        // that output is otherwise dead.  Edge-driven cloning alone therefore
        // leaves a nested Sub structurally incomplete: walking an internally
        // driven but unused output asks for a pin that was never created.  The
        // same applies to an internally read, unconnected input.
        for (const auto& d : io->get_input_pin_decls()) {
          (void)neo.create_sink_pin(d.port_id);
        }
        for (const auto& d : io->get_output_pin_decls()) {
          (void)neo.create_driver_pin(d.port_id);
        }
      }
    }
    node_map_[n] = neo;
    carry_node_attrs(n, neo);
    // Driver pin 0 rides with the NODE, not with the edge tables: a
    // single-output cell whose output has zero fanout (a dead hold-mux cprop
    // bypassed but did not delete) never appears as an edge driver, so the
    // edge-driven carry in resolve_driver() would clone it at bits==0 -- an
    // unsized cell that trips debug_assert_cells_sized at the next cprop
    // entry (same bug class as pass_partition carry_node_attrs). Port 0 is
    // THE driver of every single-output op and create_driver_pin(0) is the
    // non-allocating node-as-pin handle on both sides; live pins get the same
    // values re-stamped by resolve_driver (idempotent). Multi-driver ops
    // (Sub/Memory/Flop) are excluded: their outputs carry per-port decls.
    if (!Ntype::has_multiple_driver_pins(op)) {
      carry_driver_attrs(n.create_driver_pin(0), neo.create_driver_pin(0));
    }
  }
}

// The parent-side driver feeding instance port `pid`, or an invalid pin when the
// parent left that port unconnected.
hhds::Pin_class Sub_inliner::driver_feeding_inst_port(uint32_t pid) {
  // Read-only pin walk. The const clone below is deliberately OUTSIDE the loop:
  // it creates a node in the parent graph, which is the graph being walked.
  hhds::Pin_class drv;
  for (auto sink : inst_.inp_sorted_pins()) {
    if (static_cast<uint32_t>(sink.get_port_id()) != pid) {
      continue;
    }
    drv = sink.get_driver_pin();  // exactly one driver per sink pin
    break;
  }
  if (drv.is_invalid()) {
    return {};
  }
  if (drv.is_const()) {
    return create_const(*parent_, const_of(drv));
  }
  return drv;  // already a parent pin -- single-level inline needs no hop
}

// Parent driver pin for the child-local driver pin `d`. A child graph input pops
// out to whatever the parent wired into the instance port; anything else is the
// clone's own driver.
hhds::Pin_class Sub_inliner::resolve_driver(const hhds::Pin_class& d) {
  // Inlining a neighboring pass-through can turn a pin-level acyclic path
  // into a self-edge on this instance. Follow those IO aliases before deleting
  // the instance; retaining one of its own output pins would disconnect the
  // reader when del_node() removes that pin. This walk is per pin, so an
  // apparent cycle between independent lanes is resolved without recursion.
  std::vector<hhds::Pin_class>         path;
  absl::flat_hash_set<hhds::Pin_class> seen;
  hhds::Pin_class                      cur = d;
  hhds::Pin_class                      res;
  while (!cur.is_invalid()) {
    if (auto it = pin_cache_.find(cur); it != pin_cache_.end()) {
      res = it->second;
      break;
    }
    if (!seen.insert(cur).second) {
      report_cycle(cur);
    }
    path.push_back(cur);
    if (cur.is_const()) {
      res = create_const(*parent_, const_of(cur));
      break;
    }
    if (!is_graph_input_pin(cur)) {
      if (auto it = node_map_.find(cur.get_master_node()); it != node_map_.end()) {
        res = it->second.create_driver_pin(cur.get_port_id());
        carry_driver_attrs(cur, res);
      }
      break;
    }
    const auto pit = in_name2pid_.find(std::string{pin_name_of(cur)});
    if (pit == in_name2pid_.end()) {
      break;
    }
    auto parent_driver = driver_feeding_inst_port(pit->second);
    if (parent_driver.is_invalid() || parent_driver.is_const() || parent_driver.get_master_node() != inst_) {
      res = parent_driver;
      break;
    }
    const auto oit = out_pid2name_.find(static_cast<uint32_t>(parent_driver.get_port_id()));
    if (oit == out_pid2name_.end()) {
      break;
    }
    auto output = child_->get_output_pin(oit->second);
    cur         = {};
    if (!output.is_invalid()) {
      cur = output.get_driver_pin();  // a graph output pin is a sink: one driver
    }
  }
  if (!res.is_invalid()) {
    for (const auto& pin : path) {
      pin_cache_[pin] = res;
    }
  }
  return res;
}

// The parent driver behind the child's output port `oname` -- the child-internal
// driver, itself resolved (so a child feed-through lands on the parent's own
// driver, and a constant output is cloned into the parent).
hhds::Pin_class Sub_inliner::resolve_output_of(std::string_view oname) {
  auto opin = child_->get_output_pin(std::string{oname});
  if (opin.is_invalid()) {
    return {};
  }
  auto drv = opin.get_driver_pin();  // output pin is a sink: at most one driver
  if (drv.is_invalid()) {
    return {};  // declared but undriven
  }
  if (drv.is_const()) {
    return create_const(*parent_, const_of(drv));
  }
  return resolve_driver(drv);
}

void Sub_inliner::wire_edges() {
  for (auto n : child_->body().nodes(hhds::Node_order::forward)) {
    if (failed_) {
      return;
    }
    auto it = node_map_.find(n);
    if (it == node_map_.end()) {
      continue;  // consts and builtins: not cloned
    }
    auto neo = it->second;
    // SNAPSHOT: the body below creates pins/nodes and adds edges, and
    // resolve_driver() can clone a child constant into the parent. The walk is
    // over the CHILD body, but keeping a snapshot makes it immune to any
    // mutation either graph sees mid-loop -- the old inp_edges() materialized
    // for exactly this reason.
    for (auto sink : n.inp_pins_snapshot()) {
      auto sp = neo.create_sink_pin(sink.get_port_id());
      for (auto driver : sink.get_driver_pins()) {
        if (driver.is_const()) {
          create_const(*parent_, const_of(driver)).connect_sink(sp);
        } else if (auto dp = resolve_driver(driver); !dp.is_invalid()) {
          dp.connect_sink(sp);
        }
      }
    }
  }
}

// Everything the instance drove now reads the child-internal driver directly.
void Sub_inliner::rewire_instance_outputs() {
  // SNAPSHOT: the loop below both adds edges and (via the caller) deletes the
  // node it is walking, so the whole fanout is collected first. A driver's
  // fanout is a SET, so the inner read stays edge-shaped; out_sorted_pins()
  // only saves decoding the instance's SINK-pin edges on the way there.
  std::vector<std::pair<uint32_t, hhds::Pin_class>> readers;
  for (auto drv : inst_.out_sorted_pins()) {
    const auto pid = static_cast<uint32_t>(drv.get_port_id());
    for (const auto& e : drv.out_edges()) {
      readers.emplace_back(pid, e.sink);
    }
  }
  // Resolve every output before rewiring: a feed-through can read another
  // instance input, which must still have its original, single driver.
  std::vector<std::pair<hhds::Pin_class, hhds::Pin_class>> rewires;
  for (const auto& [pid, sink] : readers) {
    if (failed_) {
      return;
    }
    auto oit = out_pid2name_.find(pid);
    if (oit == out_pid2name_.end()) {
      continue;  // a driver port with no decl: nothing on the child side to bind
    }
    if (auto src = resolve_output_of(oit->second); !src.is_invalid()) {
      if (fit_loops_back(src, sink)) {
        report_cycle(src);
      }
      rewires.emplace_back(src, sink);
    }
    // An undriven child output leaves the reader unconnected -- exactly what the
    // instance did.
  }
  for (const auto& [src, sink] : rewires) {
    src.connect_sink(sink);
  }
}

bool Sub_inliner::run() {
  // A loop Sub denotes `count` occurrences in native HHDS structure.
  // Inlining it would splice ONE body copy and delete the node, silently
  // dropping count-1 replicas. Refuse — WITH a diagnostic: every caller treats
  // a false return as a hard failure whose message came from here (pass.color
  // bails out of coloring entirely), so a silent false aborts the run with no
  // output at all.
  if (inst_.is_loop_subnode()) {
    livehd::diag::err(from_pass_, "inline-replicated-sub", "unsupported")
        .msg("inline: instance '{}' is a replicated Sub (it stands for several occurrences)", default_instance_name(inst_))
        .hint("consume occurrences() directly, or materialize into a backend-private output graph")
        .emit();
    return false;
  }
  auto cg = def_ != nullptr ? std::shared_ptr<hhds::Graph>(def_, [](hhds::Graph*) {}) : inst_.get_subnode_graph();
  if (!cg) {
    livehd::diag::err(from_pass_, "inline-no-body", "internal")
        .msg("inline: instance '{}' has no body to inline", default_instance_name(inst_))
        .fatal();
    return false;
  }
  child_  = cg.get();
  prefix_ = prefix_instance_ ? logical_instance_prefix(inst_) : std::string{};

  auto gio = child_->get_io();
  if (!gio) {
    livehd::diag::err(from_pass_, "inline-no-io", "internal")
        .msg("inline: instance '{}' target has no GraphIO", default_instance_name(inst_))
        .fatal();
    return false;
  }
  // The two maps are keyed by the INSTANCE's port ids (its sink/driver pins)
  // and name the CHILD's ports. For the instance's own definition that is one
  // GraphIO, so the ids coincide. An override `def_` is a different GraphIO
  // that may number the same ports differently (a --lib cell model against the
  // library copy the instance was elaborated with), so it is bound by NAME
  // through the instance's IO -- checked before anything is mutated.
  auto iio = inst_.get_subnode_io();
  if (def_ != nullptr && iio.get() != gio.get()) {
    if (const auto why = sub_def_port_mismatch(inst_, def_); !why.empty()) {
      livehd::diag::err(from_pass_, "inline-port-mismatch", "unsupported")
          .msg("inline: instance '{}' does not bind to the definition '{}' by port name: {}",
               default_instance_name(inst_),
               child_->get_name(),
               why)
          .hint("the override would rewire the instance's edges onto other ports; nothing was spliced")
          .emit();
      return false;
    }
    for (const auto& d : gio->get_input_pin_decls()) {
      in_name2pid_[d.name] = static_cast<uint32_t>(iio->get_input_port_id(d.name));
    }
    for (const auto& d : iio->get_output_pin_decls()) {
      out_pid2name_[static_cast<uint32_t>(d.port_id)] = d.name;
    }
  } else {
    for (const auto& d : gio->get_input_pin_decls()) {
      in_name2pid_[d.name] = static_cast<uint32_t>(d.port_id);
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      out_pid2name_[static_cast<uint32_t>(d.port_id)] = d.name;
    }
  }

  fit_boundary();
  create_nodes();
  if (!failed_) {
    wire_edges();
  }
  if (!failed_) {
    rewire_instance_outputs();
  }
  if (failed_) {
    return false;
  }
  if (const auto program = child_->get_input_node().attr(livehd::attrs::simulation_init);
      program.has() && !program.get().empty()) {
    auto metadata = parent_->get_input_node().attr(livehd::attrs::simulation_init);
    auto combined = metadata.has() && !metadata.get().empty()
                        ? livehd::sim_ir::decode(metadata.get())
                        : livehd::sim_ir::Node{"seq", "", 0, false, {}};
    combined.kids.push_back(livehd::sim_ir::decode(program.get()));
    metadata.set(livehd::sim_ir::encode(combined));
  }
  inst_.del_node();  // its edges go with it; everything it carried is now inline
  return true;
}

}  // namespace

int sub_port_width(const hhds::Pin_class& port, const hhds::GraphIO& io, std::string_view name) {
  return std::max(1, bits_of(port, io, name));
}

bool driver_fits_port(const hhds::Pin_class& driver, int bits, bool is_signed) {
  if (driver.is_const()) {
    const auto& value = const_of(driver);
    if (value.is_nil()) {
      return true;
    }
    return is_signed ? value.get_signed_bits() <= bits : !value.is_negative() && value.get_payload_bits() <= bits;
  }
  int  dbits   = bits_of(driver);
  bool dsigned = !is_unsign(driver);
  if (is_graph_input_pin(driver)) {
    const auto io = driver.get_graph()->get_io();
    if (io == nullptr) {
      return true;
    }
    dbits   = sub_port_width(driver, *io, driver.get_pin_name());
    dsigned = !io->is_unsign(driver.get_pin_name());
  } else if (dbits == 0) {
    // An unstamped Sub output is as wide as the callee realizes that port.
    const auto sub = driver.get_master_node();
    const auto io  = type_op_of(sub) == Ntype_op::Sub ? sub.get_subnode_io() : nullptr;
    if (io == nullptr) {
      return true;
    }
    for (const auto& decl : io->get_output_pin_decls()) {
      if (decl.port_id == driver.get_port_id()) {
        const auto callee = sub.get_subnode_graph();
        dbits
            = callee ? sub_port_width(callee->get_output_pin(decl.name), *io, decl.name) : std::max(1, static_cast<int>(decl.bits));
        dsigned = !decl.unsign;
        break;
      }
    }
    if (dbits == 0) {
      return true;
    }
  }
  return range_fits_port(dbits, dsigned, bits, is_signed);
}

bool range_fits_port(int dbits, bool dsigned, int bits, bool is_signed) {
  if (dsigned) {
    return is_signed && dbits <= bits;
  }
  return dbits <= (is_signed ? bits - 1 : bits);
}

hhds::Pin_class fit_to_port(hhds::Graph& graph, const hhds::Pin_class& driver, int bits, bool is_signed) {
  if (is_signed) {
    auto sext = create_typed_node(graph, Ntype_op::Sext);
    driver.connect_sink(setup_sink_pid(sext, 0));
    create_const(graph, *Dlop::create_integer(bits)).connect_sink(setup_sink_pid(sext, 1));  // the KEPT bit count
    auto fitted = sext.create_driver_pin(0);
    set_sbits(fitted, bits);
    return fitted;
  }
  auto fitted = create_get_mask(graph, driver, 0, bits).create_driver_pin(0);
  set_ubits(fitted, bits);
  return fitted;
}

std::string sub_def_port_mismatch(const hhds::Node_class& inst, hhds::Graph* def) {
  const auto dio = def != nullptr ? def->get_io() : nullptr;
  if (dio == nullptr) {
    return "the definition has no declared IO";
  }
  const auto iio = inst.get_subnode_io();
  if (iio == nullptr) {
    return "the instance declares no IO to bind by name";
  }
  if (iio.get() == dio.get()) {
    return {};  // the instance's own definition
  }
  auto width_differs = [](uint32_t a, uint32_t b) { return a != 0 && b != 0 && a != b; };
  for (const auto& d : iio->get_input_pin_decls()) {
    if (!dio->has_input(d.name)) {
      return std::format("input '{}' of the instance is not an input of '{}'", d.name, dio->get_name());
    }
    if (const auto b = dio->get_bits(d.name); width_differs(d.bits, b)) {
      return std::format("input '{}' is {} bit(s) on the instance but {} on '{}'", d.name, d.bits, b, dio->get_name());
    }
  }
  for (const auto& d : iio->get_output_pin_decls()) {
    if (!dio->has_output(d.name)) {
      return std::format("output '{}' of the instance is not an output of '{}'", d.name, dio->get_name());
    }
    if (const auto b = dio->get_bits(d.name); width_differs(d.bits, b)) {
      return std::format("output '{}' is {} bit(s) on the instance but {} on '{}'", d.name, d.bits, b, dio->get_name());
    }
  }
  // An input the definition reads but the instance cannot drive would float.
  // A definition-only OUTPUT is harmless: nothing on the instance can read it.
  for (const auto& d : dio->get_input_pin_decls()) {
    if (!iio->has_input(d.name)) {
      return std::format("'{}' reads input '{}', which the instance does not declare", dio->get_name(), d.name);
    }
  }
  return {};
}

bool inline_sub_instance(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass, hhds::Graph* def,
                         bool name_state, bool prefix_instance, bool inherit_color, const std::optional<std::string>& state_name) {
  Sub_inliner s(parent, inst, from_pass, def, name_state, prefix_instance, inherit_color, state_name);
  return s.run();
}

}  // namespace livehd::graph_util
