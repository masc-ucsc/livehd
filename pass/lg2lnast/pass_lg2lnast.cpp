//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lg2lnast.hpp"

#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "diag.hpp"
#include "hhds/graph.hpp"
#include "lnast.hpp"
#include "lnast_builder.hpp"
#include "node_util.hpp"

static Pass_plugin sample("pass_lg2lnast", Pass_lg2lnast::setup);

namespace {

using livehd::graph_util::bits_of;
using livehd::graph_util::const_of;
using livehd::graph_util::is_unsign;
using livehd::graph_util::type_op_of;

// One graph -> one LNAST. Anything outside the supported subset sets `why`
// and the graph is skipped (never a partial, silently-wrong LNAST).
class Lowering {
public:
  explicit Lowering(hhds::Graph& g) : graph_(g) {}

  std::shared_ptr<Lnast> run();
  std::string            why;

private:
  hhds::Graph&   graph_;
  Lnast_builder  b_;
  absl::flat_hash_map<hhds::Class_index, std::string> name_of_;  // driver pin -> LNAST operand text
  struct Flop_rec {
    std::string       name;
    hhds::Node_class  node;
  };
  std::vector<Flop_rec> flop_next_;

  static std::string max_str(int bits, bool uns) {
    if (bits <= 0) {
      bits = 1;
    }
    return std::string(Dlop::get_mask_value(uns ? bits : bits - 1)->to_pyrope());
  }
  static std::string min_str(int bits, bool uns) {
    if (uns) {
      return "0";
    }
    if (bits <= 0) {
      bits = 1;
    }
    return std::string(Dlop::get_neg_mask_value(bits - 1)->to_pyrope());
  }

  void add_type(const Lnast_nid& parent, int bits, bool uns) {
    auto ty = b_.lnast->add_child(parent, Lnast_ntype::create_prim_type_int());
    b_.lnast->add_child(ty, Lnast_node::create_const(max_str(bits, uns)));
    b_.lnast->add_child(ty, Lnast_node::create_const(min_str(bits, uns)));
  }

  // Pin typing: "type_spec(ref name, prim_type_int(max,min))" from the proven range.
  void type_spec(const std::string& name, const hhds::Pin_class& p) {
    const int bits = bits_of(p);
    if (bits <= 0) {
      return;
    }
    auto ts = b_.add_child(Lnast_ntype::create_type_spec());
    b_.lnast->add_child(ts, Lnast_node::create_ref(name));
    add_type(ts, bits, is_unsign(p));
  }

  // store(ref name, value) with `name` kept whole: a dotted port/leaf name ("o.flag") is
  // ONE flattened name here, which Lnast_builder::create_assign_stmts would split as a path.
  void store_raw(std::string_view name, std::string_view value) {
    auto n = b_.add_child(Lnast_ntype::create_store());
    b_.lnast->add_child(n, Lnast_node::create_ref(std::string(name)));
    leaf(n, value);
  }

  void leaf(const Lnast_nid& parent, std::string_view v) {
    b_.add_value_child_pub(parent, v);
  }

  // Operand text of a driver pin (a name, or a literal).
  std::optional<std::string> operand(const hhds::Pin_class& d) {
    if (d.is_invalid()) {
      why = "unconnected operand";
      return std::nullopt;
    }
    if (d.is_const()) {
      const auto& c = const_of(d);
      if (c.has_unknowns()) {
        why = "constant with unknown bits";
        return std::nullopt;
      }
      return c.to_pyrope();
    }
    auto it = name_of_.find(d.get_class_index());
    if (it == name_of_.end()) {
      why = "operand used before it is defined (cycle or unsupported driver)";
      return std::nullopt;
    }
    return it->second;
  }

  // dest = op(operands...); returns the new tmp name typed by `result`.
  std::string emit(Lnast_ntype::Lnast_ntype_int op, const std::vector<std::string>& ops, const hhds::Pin_class& result,
                   bool typed = true) {
    auto dst = b_.create_lnast_tmp();
    auto n   = b_.add_child(op);
    b_.lnast->add_child(n, Lnast_node::create_ref(dst));
    for (const auto& o : ops) {
      leaf(n, o);
    }
    if (typed) {
      type_spec(dst, result);
    }
    return dst;
  }

  // A comparison yields a Bool in LNAST; the lgraph value is the integer 0/1, so
  // convert exactly as the slang front end does: `if c {t=1} else {t=0}`.
  std::string cmp_as_int(Lnast_ntype::Lnast_ntype_int op, const std::vector<std::string>& ops, const hhds::Pin_class& result) {
    const auto c = emit(op, ops, result, false);
    auto       t = b_.create_lnast_tmp();
    auto       n = b_.create_if_stmt(false);
    b_.add_if_cond(n, c);
    auto th = b_.add_if_stmts(n);
    b_.push_stmts(th);
    store_raw(t, "1");
    b_.pop_stmts();
    auto el = b_.add_if_stmts(n);
    b_.push_stmts(el);
    store_raw(t, "0");
    b_.pop_stmts();
    type_spec(t, result);
    return t;
  }

  bool lower_node(const hhds::Node_class& node);
  bool lower_flop_next(const Flop_rec& f);
  bool lower_sub(const hhds::Node_class& node);
  int  inst_cnt_ = 0;
};

bool Lowering::lower_node(const hhds::Node_class& node) {
  using livehd::graph_util::get_driver_of_sink_name;
  const auto op   = type_op_of(node);
  auto       dpin = node.get_driver_pin(0);

  auto all_inputs = [&]() -> std::optional<std::vector<std::string>> {
    std::vector<std::string> v;
    for (const auto& sink : node.inp_sorted_pins()) {
      auto o = operand(sink.get_driver_pin());
      if (!o) {
        return std::nullopt;
      }
      v.push_back(*o);
    }
    return v;
  };
  auto named = [&](std::string_view sink) { return operand(get_driver_of_sink_name(node, sink)); };

  std::string out;
  switch (op) {
    case Ntype_op::Sum: {
      std::vector<std::string> pos;
      std::vector<std::string> neg;
      for (const auto& sink : node.inp_sorted_pins()) {
        auto o = operand(sink.get_driver_pin());
        if (!o) {
          return false;
        }
        (Ntype::sink_bank(Ntype_op::Sum, sink.get_port_id()) == 0 ? pos : neg).push_back(*o);
      }
      if (pos.empty()) {
        pos.push_back("0");
      }
      std::string p = pos.size() == 1 ? pos[0] : emit(Lnast_ntype::create_plus(), pos, dpin, false);
      if (neg.empty()) {
        // plain sum: re-emit typed under the pin result
        out = pos.size() == 1 ? emit(Lnast_ntype::create_plus(), {p, "0"}, dpin) : p;
        if (pos.size() > 1) {
          type_spec(out, dpin);
        }
      } else {
        std::string m = neg.size() == 1 ? neg[0] : emit(Lnast_ntype::create_plus(), neg, dpin, false);
        out           = emit(Lnast_ntype::create_minus(), {p, m}, dpin);
      }
      break;
    }
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor: {
      auto v = all_inputs();
      if (!v) {
        return false;
      }
      out = emit(op == Ntype_op::And ? Lnast_ntype::create_bit_and()
                 : op == Ntype_op::Or ? Lnast_ntype::create_bit_or()
                                      : Lnast_ntype::create_bit_xor(),
                 *v,
                 dpin);
      break;
    }
    case Ntype_op::Mult:
    case Ntype_op::Div:
    case Ntype_op::Rem: {
      auto v = all_inputs();
      if (!v || v->size() != 2) {
        why = "mult/div/rem arity";
        return false;
      }
      out = emit(op == Ntype_op::Mult ? Lnast_ntype::create_mult()
                 : op == Ntype_op::Div ? Lnast_ntype::create_div()
                                       : Lnast_ntype::create_mod(),
                 *v,
                 dpin);
      break;
    }
    case Ntype_op::Not: {
      auto a = named("a");
      if (!a) {
        return false;
      }
      // Unsigned result: the typed form flips exactly `bits` bits. Signed result:
      // the untyped form, LiveHD's unbounded `-a - 1`.
      if (is_unsign(dpin)) {
        out = emit(Lnast_ntype::create_bit_not(), {*a, std::to_string(bits_of(dpin))}, dpin);
      } else {
        out = emit(Lnast_ntype::create_bit_not(), {*a}, dpin);
      }
      break;
    }
    case Ntype_op::Concat: {
      // p0 = msb value, p1 = its declared width, ... (MSB first): the same operand
      // order the LNAST concat node uses.
      auto v = all_inputs();
      if (!v) {
        return false;
      }
      out = emit(Lnast_ntype::create_concat(), *v, dpin);
      break;
    }
    case Ntype_op::SHL:
    case Ntype_op::SRA: {
      auto v = all_inputs();
      if (!v || v->size() != 2) {
        why = "shift arity";
        return false;
      }
      out = emit(op == Ntype_op::SHL ? Lnast_ntype::create_shl() : Lnast_ntype::create_sra(), *v, dpin);
      break;
    }
    case Ntype_op::Sext: {
      auto a = named("a");
      auto pb = livehd::graph_util::get_driver_of_sink_name(node, "b");
      if (!a || !pb.is_const() || !const_of(pb).is_just_i64()) {
        why = why.empty() ? "sext with a non-constant width" : why;
        return false;
      }
      // The cell's `b` is the kept bit COUNT; LNAST sext takes the sign-bit POSITION.
      out = emit(Lnast_ntype::create_sext(), {*a, std::to_string(const_of(pb).to_just_i64() - 1)}, dpin);
      break;
    }
    case Ntype_op::Get_mask: {
      auto r = livehd::graph_util::bit_range(node);
      auto a = named("a");
      if (!r || !a) {
        why = why.empty() ? "get_mask without a constant range" : why;
        return false;
      }
      out = emit(Lnast_ntype::create_get_mask(), {*a, std::to_string(r->first), std::to_string(r->second)}, dpin);
      break;
    }
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ: {
      std::vector<std::string> lhs;
      std::vector<std::string> rhs;
      for (const auto& sink : node.inp_sorted_pins()) {
        auto o = operand(sink.get_driver_pin());
        if (!o) {
          return false;
        }
        if (op == Ntype_op::EQ || Ntype::get_sink_name(op, sink.get_port_id()) == "as") {
          lhs.push_back(*o);
        } else {
          rhs.push_back(*o);
        }
      }
      if (op == Ntype_op::EQ) {
        if (lhs.size() != 2) {
          why = "n-ary eq";
          return false;
        }
        out = cmp_as_int(Lnast_ntype::create_eq(), lhs, dpin);
      } else {
        if (lhs.size() != 1 || rhs.size() != 1) {
          why = "n-ary compare";
          return false;
        }
        out = cmp_as_int(op == Ntype_op::LT ? Lnast_ntype::create_lt() : Lnast_ntype::create_gt(), {lhs[0], rhs[0]}, dpin);
      }
      break;
    }
    case Ntype_op::Mux: {
      auto pins = node.inp_pins_snapshot();
      if (pins.size() != 3) {
        why = "mux with more than two data inputs";
        return false;
      }
      auto s = operand(pins[0].get_driver_pin());
      auto d0 = operand(pins[1].get_driver_pin());
      auto d1 = operand(pins[2].get_driver_pin());
      if (!s || !d0 || !d1) {
        return false;
      }
      const auto sel = std::make_optional(emit(Lnast_ntype::create_ne(), {*s, "0"}, dpin, false));  // an integer is not a condition
      out        = b_.create_lnast_tmp();
      auto if_n  = b_.create_if_stmt(false);
      b_.add_if_cond(if_n, *sel);
      auto t     = b_.add_if_stmts(if_n);
      b_.push_stmts(t);
      store_raw(out, *d1);
      b_.pop_stmts();
      auto e = b_.add_if_stmts(if_n);
      b_.push_stmts(e);
      store_raw(out, *d0);
      b_.pop_stmts();
      type_spec(out, dpin);
      break;
    }
    case Ntype_op::Sub:
      return lower_sub(node);
    case Ntype_op::Flop:
    case Ntype_op::IO:
      return true;  // handled by the caller
    default:
      why = std::format("unsupported cell {}", Ntype::get_name(op));
      return false;
  }
  name_of_.insert_or_assign(dpin.get_class_index(), out);
  return true;
}

// A module instance: fcall(inst, module, store(port, arg)...), then one tuple_get
// per output that something reads -- the shape the slang front end emits.
bool Lowering::lower_sub(const hhds::Node_class& node) {
  auto sub_io = node.get_subnode_io();
  if (!sub_io || node.is_loop_subnode()) {
    why = "compact-loop or unresolved sub instance (M1)";
    return false;
  }
  const auto modname = std::string(sub_io->get_name());
  if (livehd::graph_util::is_marker_module_name(modname)) {
    why = std::format("marker module {} (M1)", modname);
    return false;
  }
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> in_conn;
  for (const auto& sink : node.inp_sorted_pins()) {
    in_conn.emplace(sink.get_port_id(), sink.get_driver_pin());
  }
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> out_conn;
  for (const auto& odrv : node.out_sorted_pins()) {
    out_conn.emplace(odrv.get_port_id(), odrv);
  }
  auto inst = std::string(livehd::graph_util::node_name_of(node));
  if (inst.empty()) {
    inst = std::format("__inst{}", inst_cnt_);
  }
  ++inst_cnt_;
  auto call = b_.add_child(Lnast_ntype::create_func_call());
  b_.lnast->add_child(call, Lnast_node::create_ref(inst));
  b_.lnast->add_child(call, Lnast_node::create_ref(modname));
  for (const auto& d : sub_io->get_input_pin_decls()) {
    auto it = in_conn.find(d.port_id);
    if (it == in_conn.end()) {
      continue;
    }
    auto o = operand(it->second);
    if (!o) {
      return false;
    }
    auto st = b_.lnast->add_child(call, Lnast_ntype::create_store());
    b_.lnast->add_child(st, Lnast_node::create_ref(std::string(d.name)));
    leaf(st, *o);
  }
  for (const auto& d : sub_io->get_output_pin_decls()) {
    auto it = out_conn.find(d.port_id);
    if (it == out_conn.end()) {
      continue;
    }
    auto t = b_.create_lnast_tmp();
    auto g = b_.add_child(Lnast_ntype::create_tuple_get());
    b_.lnast->add_child(g, Lnast_node::create_ref(t));
    b_.lnast->add_child(g, Lnast_node::create_ref(inst));
    b_.lnast->add_child(g, Lnast_node::create_const(std::string(d.name)));
    name_of_.insert_or_assign(it->second.get_class_index(), t);
  }
  return true;
}

// next-state of one register: reset (priority) -> initial, else enable-gated din.
bool Lowering::lower_flop_next(const Flop_rec& f) {
  using livehd::graph_util::get_driver_of_sink_name;
  const auto din = get_driver_of_sink_name(f.node, "din");
  if (din.is_invalid()) {
    return true;  // disconnected flop: holds
  }
  auto d = operand(din);
  if (!d) {
    return false;
  }
  std::optional<std::string> en;
  if (auto e = get_driver_of_sink_name(f.node, "enable"); !e.is_invalid() && !(e.is_const() && !const_of(e).is_known_false())) {
    auto o = operand(e);
    if (!o) {
      return false;
    }
    en = emit(Lnast_ntype::create_ne(), {*o, "0"}, e, false);
  }
  auto attr = [&](std::string_view key, std::string_view value) {
    auto n = b_.add_child(Lnast_ntype::create_attr_set());
    b_.lnast->add_child(n, Lnast_node::create_ref(f.name));
    b_.lnast->add_child(n, Lnast_node::create_const(std::string(key)));
    b_.add_value_child_pub(n, value);
  };
  const auto init_pin = get_driver_of_sink_name(f.node, "initial");
  std::string init_txt;
  if (!init_pin.is_invalid()) {
    auto io = operand(init_pin);
    if (!io) {
      return false;
    }
    init_txt = *io;
  }
  if (auto r = get_driver_of_sink_name(f.node, "reset_pin"); !r.is_invalid() && !(r.is_const() && const_of(r).is_known_false())) {
    auto o = operand(r);
    if (!o) {
      return false;
    }
    bool neg = false;
    if (auto n = get_driver_of_sink_name(f.node, "negreset"); n.is_const()) {
      neg = !const_of(n).is_known_false();
    }
    attr("initial", init_txt.empty() ? "0" : init_txt);
    attr("reset_pin", *o);
    attr("sync", "true");
    attr("negreset", neg ? "true" : "false");
  } else if (!init_txt.empty()) {
    attr("initial", init_txt);  // power-on value, no reset signal (the front end's spelling)
    attr("reset_pin", "false");
  }
  auto store_gated = [&]() {
    if (!en) {
      store_raw(f.name, *d);
      return;
    }
    auto n  = b_.create_if_stmt(false);
    b_.add_if_cond(n, *en);
    auto th = b_.add_if_stmts(n);
    b_.push_stmts(th);
    store_raw(f.name, *d);
    b_.pop_stmts();
  };
  store_gated();
  return true;
}

std::shared_ptr<Lnast> Lowering::run() {
  auto gio = graph_.get_io();
  if (!gio) {
    why = "no io";
    return nullptr;
  }
  // Same tree order as the slang front end: top { io, stmts }.
  b_.lnast = std::make_shared<Lnast>(graph_.get_name());
  b_.lnast->set_lambda_kind("mod");
  b_.lnast->set_verilog_origin(true);  // flops are state, not Pyrope feed-forward stages
  auto root    = b_.lnast->set_root(Lnast_ntype::create_top());
  auto io_nid  = b_.lnast->add_child(root, Lnast_ntype::create_io());
  auto in_tup  = b_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  auto out_tup = b_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  b_.idx_stmts = b_.lnast->add_child(root, Lnast_ntype::create_stmts());

  for (const auto& d : gio->get_input_pin_decls()) {
    auto p = graph_.get_input_pin(d.name);
    auto entry = b_.lnast->add_child(in_tup, Lnast_ntype::create_store());
    b_.lnast->add_child(entry, Lnast_node::create_ref(std::string(d.name)));
    b_.lnast->add_child(entry, Lnast_node::create_const("nil"));
    add_type(entry, bits_of(p, *gio, d.name), d.unsign);
    name_of_.insert_or_assign(p.get_class_index(), std::string(d.name));
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    auto p = graph_.get_output_pin(d.name);
    auto entry = b_.lnast->add_child(out_tup, Lnast_ntype::create_store());
    b_.lnast->add_child(entry, Lnast_node::create_ref(std::string(d.name)));
    b_.lnast->add_child(entry, Lnast_node::create_const("nil"));
    add_type(entry, bits_of(p, *gio, d.name), d.unsign);
    auto st = b_.lnast->add_child(entry, Lnast_ntype::create_stages());
    b_.lnast->add_child(st, Lnast_node::create_const("nil"));
    b_.lnast->add_child(st, Lnast_node::create_const("nil"));
  }

  // Registers first so every read of Q has a name.
  for (auto node : graph_.body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(node) != Ntype_op::Flop) {
      continue;
    }
    // The default clock is the graph input named `clk` (the front end wires every
    // un-annotated register to it, so no clock appears in the LNAST).
    if (auto c = livehd::graph_util::get_driver_of_sink_name(node, "clock_pin"); !c.is_invalid()) {
      if (!livehd::graph_util::is_graph_input_pin(c) || livehd::graph_util::pin_name_of(c) != "clk") {
        why = "flop clocked by something other than the `clk` input (M1)";
        return nullptr;
      }
    }
    if (auto a = livehd::graph_util::get_driver_of_sink_name(node, "async"); !a.is_invalid() && !(a.is_const() && const_of(a).is_known_false())) {
      why = "flop with an asynchronous reset (M1)";
      return nullptr;
    }
    if (auto pc = livehd::graph_util::get_driver_of_sink_name(node, "posclk"); !pc.is_invalid() && !(pc.is_const() && !const_of(pc).is_known_false())) {
      why = "negedge flop (M1)";
      return nullptr;
    }
    for (const char* pin : {"pipe_min", "pipe_max"}) {
      if (!livehd::graph_util::get_driver_of_sink_name(node, pin).is_invalid()) {
        why = std::format("flop with explicit {} pin (M1 handles only default clock/reset)", pin);
        return nullptr;
      }
    }
    auto q    = node.get_driver_pin(0);
    auto nm   = std::string(livehd::graph_util::pin_name_of(q));
    if (nm.empty()) {
      nm = std::string(livehd::graph_util::node_name_of(node));
    }
    if (nm.empty()) {
      nm = std::format("__reg{}", name_of_.size());
    }
    // `nil` init: the reset value and reset signal ride attr_set statements, exactly as the
    // slang front end spells them (a plain init would bind to the DEFAULT reset signal).
    b_.create_declare_stmts(nm, "reg", max_str(bits_of(q), is_unsign(q)), min_str(bits_of(q), is_unsign(q)), "nil");
    name_of_.insert_or_assign(q.get_class_index(), nm);
    flop_next_.push_back({nm, node});
  }

  for (auto node : graph_.body().nodes(hhds::Node_order::forward)) {
    if (!lower_node(node)) {
      return nullptr;
    }
  }

  // Outputs and register next values.
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = graph_.get_output_pin(d.name);
    auto drs  = spin.get_driver_pins();
    auto drv  = drs.empty() ? hhds::Pin_class{} : *drs.begin();
    if (drv.is_invalid()) {
      continue;
    }
    auto o = operand(drv);
    if (!o) {
      return nullptr;
    }
    store_raw(std::string(d.name), *o);
  }
  for (const auto& f : flop_next_) {
    if (!lower_flop_next(f)) {
      return nullptr;
    }
  }
  return b_.lnast;
}

}  // namespace

void Pass_lg2lnast::setup() {
  Eprp_method m1("pass.lg2lnast", "lower lgraphs to ordinary front-end LNAST (prototype)", &Pass_lg2lnast::work);
  register_pass(m1);
}

Pass_lg2lnast::Pass_lg2lnast(const Eprp_var& var) : Pass("pass.lg2lnast", var) {}

void Pass_lg2lnast::work(Eprp_var& var) {
  Pass_lg2lnast p(var);
  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    Lowering lw(*g);
    auto     ln = lw.run();
    if (!ln) {
      livehd::diag::warn("pass.lg2lnast", "graph-skipped", "unsupported")
          .msg("graph {} skipped: {}", std::string(g->get_name()), lw.why)
          .emit();
      continue;
    }
    var.lnasts.push_back(std::move(ln));
  }
}
