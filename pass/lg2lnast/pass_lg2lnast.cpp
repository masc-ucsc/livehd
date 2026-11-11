//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lg2lnast.hpp"

#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "debug_env.hpp"
#include "diag.hpp"
#include "hhds/graph.hpp"
#include "inline_sub.hpp"
#include "lnast.hpp"
#include "lnast_builder.hpp"
#include "lnast_stream.hpp"
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
  explicit Lowering(hhds::Graph& g) : graph_(g), b_(own_b_) {}
  // A compact-loop body lowered inline: it emits into the parent's builder (the `for` body scope).
  Lowering(hhds::Graph& g, Lowering& parent) : graph_(g), b_(parent.b_), parent_(&parent), root_(parent.root_) {}

  // `stream` (optional) receives the function as it is built: begin() once the io is complete, then
  // each top-level statement as soon as it is finished (see lnast/lnast_stream.hpp).
  std::shared_ptr<Lnast> run(livehd::lnast_stream::Sink* stream = nullptr);
  std::string            why;

private:
  hhds::Graph&   graph_;
  Lnast_builder  own_b_;
  Lnast_builder& b_;
  Lowering*      parent_ = nullptr;  // set for a loop body
  Lowering*      root_   = this;     // the outermost lowering: owns the unique-name counters
  // An instance on a ring of instances (a call needs an output of a call that needs one of its own)
  // has no statement order in which every argument exists before the call. The argument that does
  // not exist yet is passed as the SETTLED value of a net variable (`delay_assign tmp net 1`, what
  // Pyrope spells `net.[defer]`), and the net is assigned once its driver is lowered. The module
  // stays one self-contained function: nothing is copied into it from its callees.
  struct Late {
    hhds::Pin_class driver;
    std::string     net;
  };
  std::vector<Late> late_;
  size_t            late_count_ = 0;
  // constant pin -> its literal. Per GRAPH (this lowering, not the root's): a loop body is another
  // graph, and its pins carry the same indices as the caller's.
  absl::flat_hash_map<hhds::Class_index, std::string> const_text_;
  int            loop_cnt_ = 0;
  // driver pin -> name of the memory whose whole contents it carries (a read-all bus nothing needs
  // as a value): a loop body reads such a memory in place instead of through a packed copy
  struct View_alias {
    std::string mem;           // the memory whose contents the bus carries
    bool        ov = false;    // ... with entry `a` replaced by `d` while `c` holds (this cycle's write)
    std::string c, a, d;
  };
  absl::flat_hash_map<hhds::Class_index, View_alias> view_alias_;
  // `mem[a] = d` seen by a loop in the same cycle is a whole-array bus built from bit operations; when
  // the shape is exactly that, none of its cells is emitted and the loop body applies the overlay itself.
  struct Overlay_pins {
    std::string     mem;
    hhds::Pin_class c, a, d;
  };
  absl::flat_hash_map<hhds::Nid, Overlay_pins> overlay_at_;  // the mux cell -> its operands
  absl::flat_hash_set<hhds::Nid>               skip_;        // cone cells of a recognized overlay
  void find_overlays();
  livehd::lnast_stream::Sink* sink_ = nullptr;
  Lnast_nid                   last_sent_;
  bool                        any_sent_ = false;

  // Hand every top-level statement finished since the last call to the stream.
  void flush() {
    if (sink_ == nullptr) {
      return;
    }
    Lnast_nid next = any_sent_ ? b_.lnast->get_sibling_next(last_sent_) : b_.lnast->get_first_child(b_.idx_stmts);
    while (!next.is_invalid()) {
      sink_->stmt(b_.lnast, next);
      last_sent_ = next;
      any_sent_  = true;
      next       = b_.lnast->get_sibling_next(next);
    }
  }
  absl::flat_hash_map<hhds::Class_index, std::string> name_of_;  // driver pin -> LNAST operand text
  struct Flop_rec {
    std::string       name;
    hhds::Node_class  node;
    bool              latch = false;
  };
  std::vector<Flop_rec> flop_next_;

  // Memory: declared up front, read ports are worklist items (async) or registers (sync),
  // write ports and sync-read loads are emitted after every combinational cell, like a flop.
  struct Mem_port {
    int              kind = 0;  // Ntype::Memory_rdport_*
    hhds::Pin_class  addr;
    hhds::Pin_class  en;
    hhds::Pin_class  din;
    hhds::Pin_class  out;       // read-data driver pin (reads only)
    std::string      rd_reg;    // sync read: the register holding the data
  };
  struct Mem_rec {
    std::string            name;
    hhds::Node_class       node;
    std::vector<Mem_port>  ports;
    hhds::Pin_class        clock;  // the clock driver, invalid = default
    bool                   negedge = false;
    int                    bits    = 0;
    bool                   uns     = true;
    int64_t                size    = 0;
    hhds::Pin_class        read_all;   // async whole-array read bus (size*bits, entry 0 low)
    hhds::Pin_class        update;     // whole-array next-state bus (size*bits, entry 0 low)
    hhds::Pin_class        update_en;
    bool                   has_zero_image = false;
    hhds::Pin_class        reset;  // the (active-high) reset driver, invalid = none
    bool                   async_reset = false;
    int                    wensize     = 1;  // write-enable lanes of `bits/wensize` bits each
    bool                   view        = false;  // loop-body array view of an `update` bus (no storage)
    hhds::Pin_class        bus;                  // view: the packed contents it indexes
    View_alias             alias;                // view of a real memory (alias.mem non-empty)
  };
  std::vector<Mem_rec> mems_;
  int                  mem_cnt_ = 0;
  int                  wlane_cnt_ = 0;
  // Per-port sink name: pid = port * stride + the pin's base offset, spelled "<pid><base>" past port 0.
  static std::string mem_sink(int port, std::string_view base) {
    if (port == 0) {
      return std::string(base);
    }
    const auto off = Ntype::get_sink_pid(Ntype_op::Memory, base);
    return std::format("{}{}", port * Ntype::Memory_port_stride + off, base);
  }
  bool declare_memory(const hhds::Node_class& node);
  bool declare_view(const hhds::Node_class& node);
  bool lower_loop(const hhds::Node_class& node);
  bool view_only(const hhds::Pin_class& readall);
  bool lower_async_read(const Mem_rec& m, size_t port);
  bool lower_read_all(const Mem_rec& m);
  bool lower_memory_tail(const Mem_rec& m);

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
      // Unknown bits print as `?` (`0ub0?????`), the literal the front ends already emit. The text is
      // kept per constant: spelling a wide constant digit by digit at each of its uses was two fifths
      // of the whole lowering on minion.
      auto [it, fresh] = const_text_.try_emplace(d.get_class_index());
      if (fresh) {
        it->second = const_of(d).to_pyrope();
      }
      return it->second;
    }
    auto it = name_of_.find(d.get_class_index());
    if (it == name_of_.end()) {
      why = std::format("operand driven by a {} cell is used before it is defined (cycle or unsupported driver)",
                        Ntype::get_name(type_op_of(d.get_master_node())));
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
  bool lower_cells();
  int  inst_cnt_ = 0;
};

bool Lowering::lower_node(const hhds::Node_class& node) {
  using livehd::graph_util::get_driver_of_sink_name;
  const auto op   = type_op_of(node);
  auto       dpin = node.get_driver_pin(0);
  if (const auto nid = node.get_debug_nid(); skip_.contains(nid)) {
    for (const auto& o : node.out_sorted_pins()) {
      name_of_.insert_or_assign(o.get_class_index(), "0");  // never read: only the overlay cone consumes it
    }
    return true;
  } else if (auto ov = overlay_at_.find(nid); ov != overlay_at_.end()) {
    View_alias va;
    va.mem = ov->second.mem;
    va.ov  = true;
    auto c = operand(ov->second.c);
    auto a = operand(ov->second.a);
    auto d = operand(ov->second.d);
    if (!c || !a || !d) {
      return false;
    }
    va.c = *c;
    va.a = *a;
    va.d = *d;
    view_alias_.insert_or_assign(dpin.get_class_index(), va);
    name_of_.insert_or_assign(dpin.get_class_index(), va.mem);
    return true;
  }

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
    case Ntype_op::Set_mask: {
      auto r = livehd::graph_util::bit_range(node);
      auto a = named("a");
      auto v = named("value");
      if (!r || !a || !v) {
        why = why.empty() ? "set_mask without a constant range" : why;
        return false;
      }
      out = emit(Lnast_ntype::create_set_mask(), {*a, *v, std::to_string(r->first), std::to_string(r->second)}, dpin);
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
    case Ntype_op::Rxor:
    case Ntype_op::Popcount: {
      // The cell reduces the low `b` bits of `a` (extension follows a's sign).
      auto a = named("a");
      if (!a) {
        return false;
      }
      int count = 0;
      try {
        count = livehd::graph_util::reduction_count(node);
      } catch (const std::exception&) {
        why = "Rxor/Popcount without a constant bit count";
        return false;
      }
      if (count == 0) {
        out = "0";
        name_of_.insert_or_assign(dpin.get_class_index(), out);
        return true;
      }
      const auto masked = emit(Lnast_ntype::create_get_mask(), {*a, "0", std::to_string(count)}, dpin, false);
      out               = emit(op == Ntype_op::Rxor ? Lnast_ntype::create_red_xor() : Lnast_ntype::create_popcount(), {masked}, dpin);
      break;
    }
    case Ntype_op::Ror: {
      auto v = all_inputs();
      if (!v || v->empty()) {
        return false;
      }
      std::string acc = (*v)[0];
      if (v->size() > 1) {
        for (const auto& sink : node.inp_sorted_pins()) {
          if (!is_unsign(sink.get_driver_pin()) && !sink.get_driver_pin().is_const()) {
            why = "n-ary Ror over a signed operand (M1)";
            return false;
          }
        }
        acc = emit(Lnast_ntype::create_bit_or(), *v, dpin, false);
      }
      out = emit(Lnast_ntype::create_red_or(), {acc}, dpin);
      break;
    }
    case Ntype_op::Hotmux: {
      // Controls are one-bit, mutually exclusive; no arm hot => the fallback (0 when absent).
      const auto hm = livehd::graph_util::hotmux_inputs(node);
      std::string fb = "0";
      if (!hm.fallback.is_invalid()) {
        auto f = operand(hm.fallback);
        if (!f) {
          return false;
        }
        fb = *f;
      }
      // Arms that carry the same value are ONE arm on the OR of their controls (the controls are
      // exclusive, so the arms still are), and an arm that carries the fallback's value is the
      // fallback. minion decodes an "undefined when these collide" value as ~257 arms of one unknown
      // constant, about 15,000 times: one statement per arm was 3.8 million statements in one module.
      std::vector<std::pair<std::vector<std::string>, std::string>> groups;  // controls, value (first-use order)
      absl::flat_hash_map<std::string, size_t>                      group_of;
      hhds::Pin_class                                               any_control;
      for (const auto& [control, value] : hm.arms) {
        auto c = operand(control);
        auto v = operand(value);
        if (!c || !v) {
          return false;
        }
        if (*v == fb) {
          continue;
        }
        any_control         = control;
        auto [it, inserted] = group_of.try_emplace(*v, groups.size());
        if (inserted) {
          groups.emplace_back(std::vector<std::string>{}, *v);
        }
        groups[it->second].first.push_back(*c);
      }
      std::vector<std::pair<std::string, std::string>> arms;  // cond tmp, value
      for (auto& [controls, value] : groups) {
        const auto any = controls.size() == 1 ? controls.front() : emit(Lnast_ntype::create_bit_or(), controls, any_control, false);
        arms.emplace_back(emit(Lnast_ntype::create_ne(), {any, "0"}, any_control, false), value);
      }
      if (arms.empty()) {  // every arm is the fallback
        out = b_.create_lnast_tmp();
        store_raw(out, fb);
        type_spec(out, dpin);
        break;
      }
      out       = b_.create_lnast_tmp();
      auto if_n = b_.create_if_stmt(true);
      for (const auto& [c, v] : arms) {
        b_.add_if_cond(if_n, c);
        auto t = b_.add_if_stmts(if_n);
        b_.push_stmts(t);
        store_raw(out, v);
        b_.pop_stmts();
      }
      auto e = b_.add_if_stmts(if_n);
      b_.push_stmts(e);
      store_raw(out, fb);
      b_.pop_stmts();
      type_spec(out, dpin);
      break;
    }
    case Ntype_op::Sub:
      return lower_sub(node);
    case Ntype_op::Clock_cell: {
      // The front end's spelling of a clock gate (docs 07-typesystem "Clock and Reset"):
      // `Clock(clock_pin=clk, enable=en)`, `invert=true` for the active-low flavour.
      if (auto dv = get_driver_of_sink_name(node, "div");
          !dv.is_invalid() && !(dv.is_const() && const_of(dv).is_just_i64() && const_of(dv).to_just_i64() == 1)) {
        why = "clock cell with a divider";
        return false;
      }
      auto clk = named("clk_ref");
      if (!clk) {
        return false;
      }
      std::string en = "true";
      if (auto e = get_driver_of_sink_name(node, "en"); !e.is_invalid()) {
        auto o = operand(e);
        if (!o) {
          return false;
        }
        en = emit(Lnast_ntype::create_ne(), {*o, "0"}, e, false);
      }
      bool invert = false;
      if (auto iv = get_driver_of_sink_name(node, "invert"); iv.is_const()) {
        invert = !const_of(iv).is_known_false();
      }
      out       = b_.create_lnast_tmp();
      auto call = b_.add_child(Lnast_ntype::create_func_call());
      b_.lnast->add_child(call, Lnast_node::create_ref(out));
      b_.lnast->add_child(call, Lnast_node::create_ref("Clock"));
      auto arg = [&](std::string_view key, std::string_view value) {
        auto st = b_.lnast->add_child(call, Lnast_ntype::create_store());
        b_.lnast->add_child(st, Lnast_node::create_ref(std::string(key)));
        leaf(st, value);
      };
      arg("clock_pin", *clk);
      arg("enable", en);
      if (invert) {
        arg("invert", "true");
      }
      break;
    }
    case Ntype_op::Flop:
    case Ntype_op::Latch:
    case Ntype_op::IO:
      return true;  // handled by the caller
    default:
      why = std::format("unsupported cell {}", Ntype::get_name(op));
      return false;
  }
  name_of_.insert_or_assign(dpin.get_class_index(), out);
  return true;
}

// ---- Memory ----------------------------------------------------------------------------
// Supported: one clock, wensize 1, plain contents (no initial image / whole-array bus / reset),
// reads that see COMMITTED contents (fwd = undef = 0, the front end's `ordering "old"`).
bool Lowering::declare_memory(const hhds::Node_class& node) {
  using livehd::graph_util::get_driver_of_sink_name;
  auto const_i64 = [&](std::string_view nm) -> std::optional<int64_t> {
    auto d = get_driver_of_sink_name(node, nm);
    if (d.is_invalid() || !d.is_const() || !const_of(d).is_just_i64()) {
      return std::nullopt;
    }
    return const_of(d).to_just_i64();
  };
  // Contents: only a ZERO image (the front end's scalar `initial` broadcasts to every entry),
  // optionally restored by `reset`.
  bool has_zero_image = false;
  if (auto i = get_driver_of_sink_name(node, "initial"); !i.is_invalid()) {
    if (!i.is_const() || !const_of(i).is_known_zero()) {
      why = "memory with a non-zero initial image (M2)";
      return false;
    }
    has_zero_image = true;
  }
  hhds::Pin_class reset_pin;
  if (auto r = get_driver_of_sink_name(node, "reset"); !r.is_invalid()) {
    reset_pin = r;
    if (!has_zero_image) {
      why = "memory reset without an initial image";
      return false;
    }
  }
  const auto wensize = const_i64("wensize").value_or(1);
  if (wensize < 1 || wensize > 64) {
    why = "memory with an unsupported write-enable lane count";
    return false;
  }
  if (const_i64("fwd").value_or(0) != 0 || const_i64("undef").value_or(0) != 0) {
    why = "memory with read-during-write forwarding or undefined collisions (M2 covers `old` ordering)";
    return false;
  }
  const auto size = const_i64("size");
  const auto bits = const_i64("bits");
  if (!size || !bits || *size <= 0 || *bits <= 0) {
    why = "memory without constant size/bits";
    return false;
  }
  int n_ports = 0;
  for (const auto& sink : node.inp_sorted_pins()) {
    n_ports = std::max(n_ports, static_cast<int>(sink.get_port_id() / Ntype::Memory_port_stride) + 1);
  }
  Mem_rec m;
  m.node = node;
  m.name = std::string(livehd::graph_util::node_name_of(node));
  if (m.name.empty()) {
    m.name = std::format("__mem{}", mem_cnt_);
  }
  ++mem_cnt_;
  m.size = *size;
  m.bits = static_cast<int>(*bits);
  if (*bits % wensize != 0) {
    why = "memory whose width is not a multiple of its write-enable lanes";
    return false;
  }
  m.wensize = static_cast<int>(wensize);
  if (auto p = get_driver_of_sink_name(node, "posclk"); p.is_const()) {
    const auto v = const_of(p);
    if (v.is_just_i64() && v.to_just_i64() == Ntype::Memory_posclk_mixed) {
      why = "memory with mixed clock edges";
      return false;
    }
    m.negedge = v.is_known_false();
  }
  hhds::Pin_class clk;
  auto note_clock = [&](const hhds::Pin_class& c) {
    if (c.is_invalid()) {
      return true;
    }
    if (!clk.is_invalid() && clk.get_class_index() != c.get_class_index()) {
      why = "memory ports on different clocks (M2)";
      return false;
    }
    clk = c;
    return true;
  };
  if (!note_clock(get_driver_of_sink_name(node, "clock_pin"))) {
    return false;
  }
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> outs;
  for (const auto& o : node.out_sorted_pins()) {
    outs.emplace(o.get_port_id(), o);
  }
  for (int i = 0; i < n_ports; ++i) {
    Mem_port pt;
    auto     rd = get_driver_of_sink_name(node, mem_sink(i, "rdport"));
    pt.kind     = (!rd.is_invalid() && rd.is_const() && const_of(rd).is_just_i64()) ? static_cast<int>(const_of(rd).to_just_i64()) : 0;
    pt.addr     = get_driver_of_sink_name(node, mem_sink(i, "addr"));
    pt.en       = get_driver_of_sink_name(node, mem_sink(i, "enable"));
    pt.din      = get_driver_of_sink_name(node, mem_sink(i, "din"));
    if (i > 0 && !note_clock(get_driver_of_sink_name(node, mem_sink(i, "clock_pin")))) {
      return false;
    }
    if (pt.addr.is_invalid()) {
      why = "memory port without an address";
      return false;
    }
    if (pt.kind == Ntype::Memory_rdport_write) {
      if (pt.din.is_invalid()) {
        why = "memory write port without data";
        return false;
      }
    } else if (auto it = outs.find(static_cast<hhds::Port_id>(i)); it != outs.end()) {
      pt.out = it->second;
      m.uns  = is_unsign(pt.out);
    }
    m.ports.push_back(pt);
  }
  m.clock = clk;
  if (auto it = outs.find(Ntype::Memory_readall_pid); it != outs.end()) {
    m.read_all = it->second;
  }
  m.update         = get_driver_of_sink_name(node, "update");
  m.update_en      = get_driver_of_sink_name(node, "update_enable");
  m.has_zero_image = has_zero_image;
  m.reset          = reset_pin;
  m.async_reset    = node.attr(livehd::attrs::memory_async_reset).has();
  // declare + clock attrs, exactly the front end's spelling
  auto ord = b_.add_child(Lnast_ntype::create_attr_set());
  b_.lnast->add_child(ord, Lnast_node::create_ref(m.name));
  b_.lnast->add_child(ord, Lnast_node::create_const("ordering"));
  b_.lnast->add_child(ord, Lnast_node::create_const("\"old\""));
  auto d = b_.add_child(Lnast_ntype::create_declare());
  b_.lnast->add_child(d, Lnast_node::create_ref(m.name));
  auto arr = b_.lnast->add_child(d, Lnast_ntype::create_comp_type_array());
  add_type(arr, m.bits, m.uns);
  b_.lnast->add_child(arr, Lnast_node::create_const(std::format("[{}]", m.size)));
  b_.lnast->add_child(d, Lnast_node::create_const("reg"));
  b_.lnast->add_child(d, Lnast_node::create_const("nil"));
  // sync read ports are registers named after the memory
  for (size_t i = 0; i < m.ports.size(); ++i) {
    auto& pt = m.ports[i];
    if (pt.kind != Ntype::Memory_rdport_sync || pt.out.is_invalid()) {
      continue;
    }
    pt.rd_reg = std::format("{}__rd{}", m.name, i);
    b_.create_declare_stmts(pt.rd_reg, "reg", max_str(m.bits, m.uns), min_str(m.bits, m.uns), "nil");
    name_of_.insert_or_assign(pt.out.get_class_index(), pt.rd_reg);
  }
  mems_.push_back(std::move(m));
  return true;
}

// read_all: every entry, MSB first (entry size-1 .. entry 0), as one concat.
bool Lowering::lower_read_all(const Mem_rec& m) {
  if (m.view) {
    // A loop-body array: the packed value it was handed, with each write applied in port order.
    // An entry write is spelled as the front end spells `x#[p..+W] = v` at a position that is not
    // a constant -- (x & ((1 << p) - 1)) | (((x >> p) with its low W bits replaced) << p), cut to
    // x's width -- which the native back end recognizes and turns into one store.
    auto bus = operand(m.bus);
    if (!bus) {
      return false;
    }
    const int64_t total = m.size * m.bits;
    const auto    typed = [&](Lnast_ntype::Lnast_ntype_int op, const std::vector<std::string>& ops, int64_t bits) {
      auto dst = b_.create_lnast_tmp();
      auto n   = b_.add_child(op);
      b_.lnast->add_child(n, Lnast_node::create_ref(dst));
      for (const auto& o : ops) {
        leaf(n, o);
      }
      auto ts = b_.add_child(Lnast_ntype::create_type_spec());
      b_.lnast->add_child(ts, Lnast_node::create_ref(dst));
      add_type(ts, static_cast<int>(bits), true);
      return dst;
    };
    int position_bits = 1;
    while ((int64_t{1} << position_bits) <= total) {
      ++position_bits;
    }
    std::string cur = *bus;
    for (const auto& pt : m.ports) {
      if (pt.kind != Ntype::Memory_rdport_write) {
        continue;
      }
      auto a = operand(pt.addr);
      auto d = operand(pt.din);
      if (!a || !d) {
        return false;
      }
      const auto  at    = typed(Lnast_ntype::create_mult(), {*a, std::to_string(m.bits)}, position_bits);
      const auto  down  = typed(Lnast_ntype::create_sra(), {cur, at}, total);
      std::string value = *d;
      if (!pt.en.is_invalid() && !(pt.en.is_const() && const_of(pt.en).is_just_i64() && const_of(pt.en).to_just_i64() != 0)) {
        // a write that may not happen stores the entry's own value back
        auto en = operand(pt.en);
        if (!en) {
          return false;
        }
        const auto old
            = typed(Lnast_ntype::create_bit_and(), {down, std::string(Dlop::get_mask_value(m.bits)->to_pyrope())}, m.bits);
        const auto cnd = emit(Lnast_ntype::create_ne(), {*en, "0"}, pt.en, false);
        value          = b_.create_lnast_tmp();
        auto n         = b_.create_if_stmt(false);
        b_.add_if_cond(n, cnd);
        auto th = b_.add_if_stmts(n);
        b_.push_stmts(th);
        store_raw(value, *d);
        b_.pop_stmts();
        auto el = b_.add_if_stmts(n);
        b_.push_stmts(el);
        store_raw(value, old);
        b_.pop_stmts();
        auto ts = b_.add_child(Lnast_ntype::create_type_spec());
        b_.lnast->add_child(ts, Lnast_node::create_ref(value));
        add_type(ts, m.bits, true);
      }
      const auto one  = typed(Lnast_ntype::create_shl(), {"1", at}, total);
      const auto low  = typed(Lnast_ntype::create_minus(), {one, "1"}, total);
      const auto kept = typed(Lnast_ntype::create_bit_and(), {cur, low}, total);
      const auto fill = typed(Lnast_ntype::create_set_mask(), {down, value, "0", std::to_string(m.bits)}, total);
      const auto up   = typed(Lnast_ntype::create_shl(), {fill, at}, 2 * total);
      const auto cut  = typed(Lnast_ntype::create_get_mask(), {up, "0", std::to_string(total)}, total);
      cur             = typed(Lnast_ntype::create_bit_or(), {kept, cut}, total);
    }
    name_of_.insert_or_assign(m.read_all.get_class_index(), cur);
    return true;
  }
  if (view_only(m.read_all)) {
    // Only compact-loop bodies read this bus, and they read entries of the memory itself: no packed copy.
    view_alias_.insert_or_assign(m.read_all.get_class_index(), View_alias{m.name});
    name_of_.insert_or_assign(m.read_all.get_class_index(), m.name);
    return true;
  }
  std::vector<std::string> lanes;
  for (int64_t i = m.size - 1; i >= 0; --i) {
    auto t = b_.create_lnast_tmp();
    auto g = b_.add_child(Lnast_ntype::create_tuple_get());
    b_.lnast->add_child(g, Lnast_node::create_ref(t));
    b_.lnast->add_child(g, Lnast_node::create_ref(m.name));
    leaf(g, std::to_string(i));
    lanes.push_back(t);
    lanes.push_back(std::to_string(m.bits));
  }
  const auto out = emit(Lnast_ntype::create_concat(), lanes, m.read_all);
  name_of_.insert_or_assign(m.read_all.get_class_index(), out);
  return true;
}

bool Lowering::lower_async_read(const Mem_rec& m, size_t port) {
  const auto& pt = m.ports[port];
  auto        a  = operand(pt.addr);
  if (!a) {
    return false;
  }
  if (m.view && m.name.empty()) {
    // An array view of a packed bus that is a real value: entry = (bus >> addr * bits) & mask.
    auto bus = operand(m.bus);
    if (!bus) {
      return false;
    }
    const auto off = emit(Lnast_ntype::create_mult(), {*a, std::to_string(m.bits)}, pt.addr, false);
    const auto sh  = emit(Lnast_ntype::create_sra(), {*bus, off}, m.bus);
    const auto v   = emit(Lnast_ntype::create_bit_and(), std::vector<std::string>{sh, std::string(Dlop::get_mask_value(m.bits)->to_pyrope())}, pt.out);
    name_of_.insert_or_assign(pt.out.get_class_index(), v);
    return true;
  }
  auto t = b_.create_lnast_tmp();
  auto g = b_.add_child(Lnast_ntype::create_tuple_get());
  b_.lnast->add_child(g, Lnast_node::create_ref(t));
  b_.lnast->add_child(g, Lnast_node::create_ref(m.name));
  leaf(g, *a);
  type_spec(t, pt.out);
  if (m.view && m.alias.ov) {
    // this cycle's write is visible: entry == a && c ? d : committed entry
    const auto hit = cmp_as_int(Lnast_ntype::create_eq(), {*a, m.alias.a}, pt.addr);
    const auto en  = emit(Lnast_ntype::create_bit_and(), {hit, m.alias.c}, pt.addr);
    const auto cnd = emit(Lnast_ntype::create_ne(), {en, "0"}, pt.addr, false);
    const auto r   = b_.create_lnast_tmp();
    auto       n   = b_.create_if_stmt(false);
    b_.add_if_cond(n, cnd);
    auto th = b_.add_if_stmts(n);
    b_.push_stmts(th);
    store_raw(r, m.alias.d);
    b_.pop_stmts();
    auto el = b_.add_if_stmts(n);
    b_.push_stmts(el);
    store_raw(r, t);
    b_.pop_stmts();
    type_spec(r, pt.out);
    name_of_.insert_or_assign(pt.out.get_class_index(), r);
    return true;
  }
  name_of_.insert_or_assign(pt.out.get_class_index(), t);
  return true;
}

// Writes (program order = port order) and the loads of the sync-read registers.
bool Lowering::lower_memory_tail(const Mem_rec& m) {
  auto attr = [&](const std::string& target, std::string_view key, std::string_view value) {
    auto n = b_.add_child(Lnast_ntype::create_attr_set());
    b_.lnast->add_child(n, Lnast_node::create_ref(target));
    b_.lnast->add_child(n, Lnast_node::create_const(std::string(key)));
    b_.add_value_child_pub(n, value);
  };
  std::string clock_op;
  if (!m.clock.is_invalid()) {
    auto c = operand(m.clock);
    if (!c) {
      return false;
    }
    clock_op = *c;
    attr(m.name, "__store_clock_pin", clock_op);
  }
  if (m.negedge) {
    attr(m.name, "__store_posclk", "false");
  } else {
    attr(m.name, "__store_posclk", "true");
  }
  if (m.has_zero_image) {
    attr(m.name, "initial", "0");
    if (m.reset.is_invalid()) {
      attr(m.name, "reset_pin", "false");  // power-on contents only
    } else {
      auto r = operand(m.reset);
      if (!r) {
        return false;
      }
      attr(m.name, "reset_pin", *r);
      attr(m.name, "negreset", "false");
      attr(m.name, "sync", m.async_reset ? "false" : "true");
    }
  }
  auto gated = [&](const hhds::Pin_class& en, const auto& body) {
    if (en.is_invalid() || (en.is_const() && !const_of(en).is_known_false())) {
      body();
      return true;
    }
    auto o = operand(en);
    if (!o) {
      return false;
    }
    const auto c = emit(Lnast_ntype::create_ne(), {*o, "0"}, en, false);
    auto       n = b_.create_if_stmt(false);
    b_.add_if_cond(n, c);
    auto th = b_.add_if_stmts(n);
    b_.push_stmts(th);
    body();
    b_.pop_stmts();
    return true;
  };
  // Whole-array update first (the per-port writes commit after it), spelled as the front end does:
  // set_mask(mem, mem, bus, 0, size*bits) under the optional enable.
  if (!m.update.is_invalid()) {
    auto u = operand(m.update);
    if (!u) {
      return false;
    }
    if (!gated(m.update_en, [&] {
          auto n = b_.add_child(Lnast_ntype::create_set_mask());
          b_.lnast->add_child(n, Lnast_node::create_ref(m.name));
          b_.lnast->add_child(n, Lnast_node::create_ref(m.name));
          leaf(n, *u);
          leaf(n, "0");
          leaf(n, std::to_string(m.size * m.bits));
        })) {
      return false;
    }
  }
  for (const auto& pt : m.ports) {
    if (pt.kind == Ntype::Memory_rdport_write) {
      auto a = operand(pt.addr);
      auto d = operand(pt.din);
      if (!a || !d) {
        return false;
      }
      std::optional<std::string> en_op;
      if (m.wensize > 1) {
        if (pt.en.is_invalid()) {
          why = "memory with write-enable lanes but no enable";
          return false;
        }
        en_op = operand(pt.en);
        if (!en_op) {
          return false;
        }
      }
      if (!gated(pt.en, [&] {
            if (m.wensize == 1) {
              auto st = b_.add_child(Lnast_ntype::create_store());
              b_.lnast->add_child(st, Lnast_node::create_ref(m.name));
              leaf(st, *a);
              leaf(st, *d);
              return;
            }
            // Per-lane write: start from the entry as it is, replace each enabled lane, store back.
            const int lane_bits = m.bits / m.wensize;
            auto      old_t     = b_.create_lnast_tmp();
            auto      g         = b_.add_child(Lnast_ntype::create_tuple_get());
            b_.lnast->add_child(g, Lnast_node::create_ref(old_t));
            b_.lnast->add_child(g, Lnast_node::create_ref(m.name));
            leaf(g, *a);
            const auto merged = std::format("__wlane{}", wlane_cnt_++);  // mutable: a %tmp is single-assignment
            store_raw(merged, old_t);
            for (int j = 0; j < m.wensize; ++j) {
              const auto lane_en = b_.create_get_mask_stmts(*en_op, std::to_string(j), std::to_string(j + 1));
              auto       c       = b_.create_lnast_tmp();
              auto       ne      = b_.add_child(Lnast_ntype::create_ne());
              b_.lnast->add_child(ne, Lnast_node::create_ref(c));
              b_.lnast->add_child(ne, Lnast_node::create_ref(lane_en));
              leaf(ne, "0");
              auto n  = b_.create_if_stmt(false);
              b_.add_if_cond(n, c);
              auto th = b_.add_if_stmts(n);
              b_.push_stmts(th);
              const auto lane = b_.create_get_mask_stmts(*d, std::to_string(j * lane_bits), std::to_string((j + 1) * lane_bits));
              b_.create_set_mask_stmts(merged, lane, std::to_string(j * lane_bits), std::to_string((j + 1) * lane_bits));
              b_.pop_stmts();
            }
            auto st = b_.add_child(Lnast_ntype::create_store());
            b_.lnast->add_child(st, Lnast_node::create_ref(m.name));
            leaf(st, *a);
            leaf(st, merged);
          })) {
        return false;
      }
    } else if (pt.kind == Ntype::Memory_rdport_sync && !pt.rd_reg.empty()) {
      auto a = operand(pt.addr);
      if (!a) {
        return false;
      }
      if (!clock_op.empty()) {
        attr(pt.rd_reg, "clock_pin", clock_op);
      }
      if (m.negedge) {
        attr(pt.rd_reg, "posclk", "false");
      }
      if (!gated(pt.en, [&] {
            auto t = b_.create_lnast_tmp();
            auto g = b_.add_child(Lnast_ntype::create_tuple_get());
            b_.lnast->add_child(g, Lnast_node::create_ref(t));
            b_.lnast->add_child(g, Lnast_node::create_ref(m.name));
            leaf(g, *a);
            store_raw(pt.rd_reg, t);
          })) {
        return false;
      }
    }
  }
  return true;
}

// Recognizes the whole-array bus the front end builds for `if c { mem[a] = d }` seen by a loop in the
// same cycle:  mux(c, R, (R & ~(M << K)) | (M & (d << K)))  with R = read_all(mem), M = (2^bits-1) <<
// shifted by K = a << log2(bits). Anything else (or any cell of it read elsewhere) is left alone.
void Lowering::find_overlays() {
  auto drivers = [](const hhds::Node_class& n) {
    std::vector<hhds::Pin_class> v;
    for (const auto& sink : n.inp_sorted_pins()) {
      v.push_back(sink.get_driver_pin());
    }
    return v;
  };
  auto driver_of = [](const hhds::Node_class& n, hhds::Port_id pid) {
    for (const auto& sink : n.inp_sorted_pins()) {
      if (sink.get_port_id() == pid) {
        return sink.get_driver_pin();
      }
    }
    return hhds::Pin_class{};
  };
  auto op_of = [](const hhds::Pin_class& p) { return p.is_invalid() || p.is_const() ? Ntype_op::Invalid : type_op_of(p.get_master_node()); };
  auto same  = [](const hhds::Pin_class& a, const hhds::Pin_class& b) {
    return !a.is_invalid() && !b.is_invalid() && a.get_class_index() == b.get_class_index();
  };
  // text of a constant with leading zeros dropped
  auto hex = [](const hhds::Pin_class& p) {
    if (p.is_invalid() || !p.is_const()) {
      return std::string();
    }
    auto t = const_of(p).to_pyrope();
    if (t.size() > 2 && t[0] == '0' && t[1] == 'x') {
      t.erase(0, 2);
      t.erase(0, std::min(t.find_first_not_of('0'), t.size()));
    }
    return std::string(t);
  };
  auto ones = [&](const hhds::Pin_class& p, int64_t bits) {
    const auto t = hex(p);
    if (t.empty() || bits < 1 || t.size() != static_cast<size_t>((bits + 3) / 4)) {
      return false;
    }
    const char top = bits % 4 == 0 ? 'f' : "?137"[bits % 4];
    return t[0] == top && t.find_first_not_of('f', 1) == std::string::npos;
  };
  for (const auto& mux : graph_.body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(mux) != Ntype_op::Mux || mux.get_driver_pin(0).is_invalid()) {
      continue;
    }
    const auto c = driver_of(mux, 0), a = driver_of(mux, 1), b = driver_of(mux, 2);
    if (c.is_invalid() || a.is_invalid() || a.is_const() || b.is_invalid() || op_of(b) != Ntype_op::Or) {
      continue;
    }
    if (type_op_of(a.get_master_node()) != Ntype_op::Memory || a.get_port_id() != Ntype::Memory_readall_pid) {
      continue;
    }
    const Mem_rec* mr = nullptr;
    for (const auto& m : mems_) {
      if (m.node == a.get_master_node() && !m.read_all.is_invalid() && same(m.read_all, a)) {
        mr = &m;
      }
    }
    if (mr == nullptr || mr->bits < 1 || (mr->bits & (mr->bits - 1)) != 0) {
      continue;
    }
    const auto orn = b.get_master_node();
    const auto ors = drivers(orn);
    if (ors.size() != 2 || op_of(ors[0]) != Ntype_op::And || op_of(ors[1]) != Ntype_op::And) {
      continue;
    }
    hhds::Pin_class and1, and2;
    for (int k = 0; k < 2; ++k) {
      const auto ins = drivers(ors[k].get_master_node());
      if (ins.size() == 2 && (same(ins[0], a) || same(ins[1], a))) {
        and1 = ors[k];
        and2 = ors[1 - k];
      }
    }
    if (and1.is_invalid()) {
      continue;
    }
    const auto in1 = drivers(and1.get_master_node());
    const auto x   = same(in1[0], a) ? in1[1] : in1[0];
    const auto in2 = drivers(and2.get_master_node());
    if (op_of(x) != Ntype_op::Xor || in2.size() != 2) {
      continue;
    }
    const auto xs = drivers(x.get_master_node());
    if (xs.size() != 2) {
      continue;
    }
    const auto s = xs[0].is_const() ? xs[1] : xs[0];
    const auto allones = xs[0].is_const() ? xs[0] : xs[1];
    if (!(xs[0].is_const() != xs[1].is_const()) || op_of(s) != Ntype_op::SHL || !ones(allones, bits_of(s))) {
      continue;
    }
    const auto t = same(in2[0], s) ? in2[1] : (same(in2[1], s) ? in2[0] : hhds::Pin_class{});
    if (t.is_invalid() || op_of(t) != Ntype_op::SHL) {
      continue;
    }
    const auto sn = s.get_master_node(), tn = t.get_master_node();
    const auto lane = driver_of(sn, 0), k1 = driver_of(sn, 1), d = driver_of(tn, 0), k2 = driver_of(tn, 1);
    if (!ones(lane, mr->bits) || !same(k1, k2) || op_of(k1) != Ntype_op::SHL) {
      continue;
    }
    const auto kn   = k1.get_master_node();
    const auto addr = driver_of(kn, 0), sh = driver_of(kn, 1);
    if (addr.is_invalid() || d.is_invalid() || !sh.is_const() || !const_of(sh).is_just_i64()
        || (int64_t{1} << const_of(sh).to_just_i64()) != mr->bits) {
      continue;
    }
    // every cell of the cone is read only inside it (and the mux), the bus only by loop bodies
    std::vector<hhds::Node_class> cone{kn, sn, tn, x.get_master_node(), and1.get_master_node(), and2.get_master_node(), orn};
    bool                          closed = true;
    for (const auto& cn : cone) {
      for (const auto& o : cn.out_sorted_pins()) {
        for (const auto& e : o.out_edges()) {
          const auto sm = e.sink.get_master_node();
          bool       in = sm == mux;
          for (const auto& other : cone) {
            in = in || sm == other;
          }
          closed = closed && in;
        }
      }
    }
    for (const auto& e : a.out_edges()) {
      const auto sm = e.sink.get_master_node();
      closed        = closed && (sm == mux || sm == and1.get_master_node() || type_op_of(sm) == Ntype_op::Sub);
    }
    if (!closed || !view_only(mux.get_driver_pin(0))) {
      continue;
    }
    overlay_at_[mux.get_debug_nid()] = Overlay_pins{mr->name, c, addr, d};
    for (const auto& cn : cone) {
      skip_.insert(cn.get_debug_nid());
    }
  }
}

// True when every reader of the read-all bus `ra` is a compact-loop body that only indexes it
// through an array-view memory (`update` bus): such a bus never needs to exist as a value.
bool Lowering::view_only(const hhds::Pin_class& ra) {
  bool any = false;
  for (const auto& e : ra.out_edges()) {
    any            = true;
    const auto snk = e.sink;
    const auto n   = snk.get_master_node();
    if (skip_.contains(n.get_debug_nid()) || overlay_at_.contains(n.get_debug_nid())) {
      continue;  // a recognized overlay cell: it is not emitted
    }
    if (type_op_of(n) != Ntype_op::Sub || !n.is_loop_subnode()) {
      return false;
    }
    auto io   = n.get_subnode_io();
    auto body = n.get_subnode_graph();
    if (!io || !body) {
      return false;
    }
    std::string pname;
    for (const auto& d : io->get_input_pin_decls()) {
      if (d.port_id == snk.get_port_id()) {
        pname = std::string(d.name);
      }
    }
    if (pname.empty()) {
      return false;
    }
    const auto bp = body->get_input_pin(pname);
    for (const auto& be : bp.out_edges()) {
      const auto bs = be.sink;
      if (type_op_of(bs.get_master_node()) != Ntype_op::Memory || bs.get_port_id() != Ntype::get_sink_pid(Ntype_op::Memory, "update")) {
        return false;
      }
    }
  }
  return any;
}

// A loop-body memory: reads of an array bound to the `update` bus (the loop's invariant input).
bool Lowering::declare_view(const hhds::Node_class& node) {
  using livehd::graph_util::get_driver_of_sink_name;
  auto const_i64 = [&](std::string_view nm) -> std::optional<int64_t> {
    auto d = get_driver_of_sink_name(node, nm);
    if (d.is_invalid() || !d.is_const() || !const_of(d).is_just_i64()) {
      return std::nullopt;
    }
    return const_of(d).to_just_i64();
  };
  Mem_rec m;
  m.node   = node;
  m.view   = true;
  m.bus    = get_driver_of_sink_name(node, "update");
  m.update = hhds::Pin_class{};
  const auto size = const_i64("size");
  const auto bits = const_i64("bits");
  if (m.bus.is_invalid() || !size || !bits || *size <= 0 || *bits <= 0) {
    why = "loop-body memory that is not an array view of an input";
    return false;
  }
  m.size = *size;
  m.bits = static_cast<int>(*bits);
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> outs;
  for (const auto& o : node.out_sorted_pins()) {
    outs.emplace(o.get_port_id(), o);
  }
  int n_ports = 0;
  for (const auto& sink : node.inp_sorted_pins()) {
    n_ports = std::max(n_ports, static_cast<int>(sink.get_port_id() / Ntype::Memory_port_stride) + 1);
  }
  int writes = 0, reads = 0;
  for (int i = 0; i < n_ports; ++i) {
    Mem_port pt;
    auto     rd = get_driver_of_sink_name(node, mem_sink(i, "rdport"));
    pt.kind     = (!rd.is_invalid() && rd.is_const() && const_of(rd).is_just_i64()) ? static_cast<int>(const_of(rd).to_just_i64()) : 0;
    pt.addr     = get_driver_of_sink_name(node, mem_sink(i, "addr"));
    if (pt.kind != Ntype::Memory_rdport_async && pt.kind != Ntype::Memory_rdport_write) {
      why = "loop-body memory with a synchronous port";
      return false;
    }
    if (pt.addr.is_invalid()) {
      why = "memory port without an address";
      return false;
    }
    if (pt.kind == Ntype::Memory_rdport_write) {
      // An array the loop body writes: `a[i] = v`. The array is the packed value on the `update`
      // bus, and what the body hands on (the read-all bus) is that value with the entry replaced.
      pt.en  = get_driver_of_sink_name(node, mem_sink(i, "enable"));
      pt.din = get_driver_of_sink_name(node, mem_sink(i, "din"));
      if (pt.din.is_invalid()) {
        why = "memory write port without data";
        return false;
      }
      ++writes;
    } else if (auto it = outs.find(static_cast<hhds::Port_id>(i)); it != outs.end()) {
      pt.out = it->second;
      m.uns  = is_unsign(pt.out);
      ++reads;
    }
    m.ports.push_back(pt);
  }
  if (auto it = outs.find(Ntype::Memory_readall_pid); it != outs.end()) {
    m.read_all = it->second;
  }
  if (auto it = view_alias_.find(m.bus.get_class_index()); it != view_alias_.end()) {
    m.alias = it->second;
    m.name  = it->second.mem;  // read the real memory in place
  }
  if (writes != 0 && (reads != 0 || !m.name.empty())) {
    // Whether an indexed read of the same cell sees the write is the cell's to say, and a write
    // through a view of a real memory would have to reach that memory: neither is lowered yet.
    why = reads != 0 ? "loop-body array written and indexed in one cell" : "loop-body write to an array that is a memory";
    return false;
  }
  mems_.push_back(std::move(m));
  return true;
}

// A compact loop: `for` over the index domain with the body graph spelled inline. Carries are
// ordinary variables assigned at the end of the body, finals are assigned every trip, invariant
// inputs are the caller's operands, and a memory the body indexes is read where it lives. No call,
// no per-trip copy of any array. The loop stays rolled (the code footprint does not grow with the
// trip count); upass unrolls it on the LEC round trip, which is exact.
bool Lowering::lower_loop(const hhds::Node_class& node) {
  auto sio  = node.get_subnode_io();
  auto body = node.get_subnode_graph();
  auto desc = node.subnode_loop();
  if (!sio || !body || !desc) {
    why = "unresolved compact loop (M1)";
    return false;
  }
  if (desc->count == 0 || desc->step == 0) {
    why = "compact loop with an empty domain";
    return false;
  }
  if (desc->next_active_output) {
    why = "compact loop with break/continue";
    return false;
  }
  auto bio = body->get_io();
  if (!bio) {
    why = "compact loop body without io";
    return false;
  }
  {
    // Module calls in the body are spliced into the body graph (in memory), so the loop stays one `for`
    // with everything inline: no call per trip.
    std::vector<hhds::Node_class> calls;
    for (const auto& bn : body->body().nodes(hhds::Node_order::forward)) {
      if (type_op_of(bn) != Ntype_op::Sub || bn.is_loop_subnode()) {
        continue;
      }
      auto io = bn.get_subnode_io();
      if (io && !livehd::graph_util::is_marker_module_name(io->get_name()) && bn.get_subnode_graph()) {
        calls.push_back(bn);
      }
    }
    if (!calls.empty()) {
      livehd::graph_util::Host_inliner inliner(body.get(), "pass.lg2lnast");
      size_t                           spliced = 0;
      for (const auto& c : calls) {
        spliced += inliner.splice(c) ? 1 : 0;
      }
      inliner.flush();
      if (spliced != calls.size()) {
        why = "module call inside a compact-loop body that cannot be inlined";
        return false;
      }
    }
  }
  for (const auto& bn : body->body().nodes(hhds::Node_order::forward)) {
    const auto op = type_op_of(bn);
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Clock_cell) {
      why = "state element inside a compact-loop body";
      return false;
    }
  }
  const auto id = root_->loop_cnt_++;

  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> actual;
  for (const auto& sink : node.inp_sorted_pins()) {
    for (const auto& d : sink.get_driver_pins()) {
      if (!(d.get_master_node() == node)) {  // the carry self-edge is the descriptor, not a value
        actual.emplace(sink.get_port_id(), d);
        break;
      }
    }
  }
  absl::flat_hash_map<hhds::Port_id, hhds::Pin_class> out_conn;
  for (const auto& o : node.out_sorted_pins()) {
    out_conn.emplace(o.get_port_id(), o);
  }
  struct Carry {
    hhds::Port_id in = 0, out = 0;
    std::string   in_name, out_name, var;
  };
  std::vector<Carry> carries;
  for (const auto& c : node.subnode_group().carries()) {
    Carry k;
    k.in  = c.input_port();
    k.out = c.output_port();
    for (const auto& d : sio->get_input_pin_decls()) {
      if (d.port_id == k.in) {
        k.in_name = std::string(d.name);
      }
    }
    for (const auto& d : sio->get_output_pin_decls()) {
      if (d.port_id == k.out) {
        k.out_name = std::string(d.name);
      }
    }
    if (k.in_name.empty() || k.out_name.empty()) {
      why = "compact loop carry without port declarations";
      return false;
    }
    k.var = std::format("__lc{}_{}", id, k.out_name);
    carries.push_back(std::move(k));
  }
  auto carry_of_in = [&](hhds::Port_id p) -> const Carry* {
    for (const auto& c : carries) {
      if (c.in == p) {
        return &c;
      }
    }
    return nullptr;
  };
  auto carry_of_out = [&](hhds::Port_id p) -> const Carry* {
    for (const auto& c : carries) {
      if (c.out == p) {
        return &c;
      }
    }
    return nullptr;
  };

  auto type_bits = [&](const std::string& name, int bits, bool uns) {
    if (bits <= 0) {
      return;
    }
    auto ts = b_.add_child(Lnast_ntype::create_type_spec());
    b_.lnast->add_child(ts, Lnast_node::create_ref(name));
    add_type(ts, bits, uns);
  };
  Lowering    sub(*body, *this);
  const auto  ivar = std::format("__li{}", id);
  std::string bad;
  for (const auto& d : sio->get_input_pin_decls()) {
    const auto bp = body->get_input_pin(d.name);
    if (desc->index_input && *desc->index_input == d.port_id) {
      sub.name_of_.insert_or_assign(bp.get_class_index(), ivar);
    } else if (desc->activation_input && *desc->activation_input == d.port_id) {
      auto it = actual.find(d.port_id);
      if (it != actual.end() && !(it->second.is_const() && !const_of(it->second).is_known_false())) {
        why = "compact loop with a run-time activation";
        return false;
      }
      sub.name_of_.insert_or_assign(bp.get_class_index(), "1");
    } else if (const auto* c = carry_of_in(d.port_id)) {
      sub.name_of_.insert_or_assign(bp.get_class_index(), c->var);
    } else {
      auto it = actual.find(d.port_id);
      if (it == actual.end()) {
        continue;  // unconnected: the body must not read it (operand() reports it if it does)
      }
      if (auto va = view_alias_.find(it->second.get_class_index()); va != view_alias_.end()) {
        sub.view_alias_.insert_or_assign(bp.get_class_index(), va->second);
        sub.name_of_.insert_or_assign(bp.get_class_index(), va->second.mem);
        continue;
      }
      auto o = operand(it->second);
      if (!o) {
        return false;
      }
      sub.name_of_.insert_or_assign(bp.get_class_index(), *o);
    }
  }
  auto out_decl_bits = [&](std::string_view name) { return bits_of(body->get_output_pin(name), *bio, name); };

  // Initial values: the carry seeds and a defined value for every final.
  struct Final {
    hhds::Port_id port;
    std::string   name, var;
    bool          uns;
  };
  std::vector<Final> finals;
  for (const auto& c : carries) {
    std::string init = "0";
    if (auto it = actual.find(c.in); it != actual.end()) {
      auto o = operand(it->second);
      if (!o) {
        return false;
      }
      init = *o;
    }
    bool uns = true;
    for (const auto& d : sio->get_output_pin_decls()) {
      if (d.port_id == c.out) {
        uns = d.unsign;
      }
    }
    store_raw(c.var, init);
    type_bits(c.var, out_decl_bits(c.out_name), uns);
  }
  for (const auto& d : sio->get_output_pin_decls()) {
    if (carry_of_out(d.port_id) != nullptr || (desc->next_active_output && *desc->next_active_output == d.port_id)) {
      continue;
    }
    if (out_conn.find(d.port_id) == out_conn.end()) {
      continue;  // nobody reads it
    }
    Final f{d.port_id, std::string(d.name), std::format("__lf{}_{}", id, std::string(d.name)), d.unsign};
    store_raw(f.var, "0");
    type_bits(f.var, out_decl_bits(f.name), f.uns);
    finals.push_back(std::move(f));
  }

  // for __liN in first..last { body; carries/finals <- this trip's values }
  // A domain that is not 0.. or 1.. up (a negative first index or any other step) counts trips `0..count-1`
  // and derives the index in the body: index = first + trip * step.
  const bool direct = desc->first >= 0 && desc->step == 1;
  const auto kvar   = direct ? ivar : std::format("__lk{}", id);
  const auto last   = direct ? desc->first + static_cast<int64_t>(desc->count - 1) : static_cast<int64_t>(desc->count - 1);
  auto       rt     = b_.create_lnast_tmp();
  auto       rg     = b_.add_child(Lnast_ntype::create_range());
  b_.lnast->add_child(rg, Lnast_node::create_ref(rt));
  leaf(rg, std::to_string(direct ? desc->first : 0));
  leaf(rg, std::to_string(last));
  auto lp = b_.add_child(Lnast_ntype::create_for());
  b_.lnast->add_child(lp, Lnast_node::create_ref(kvar));
  b_.lnast->add_child(lp, Lnast_node::create_ref(rt));
  auto bs = b_.lnast->add_child(lp, Lnast_ntype::create_stmts());
  b_.push_stmts(bs);
  if (!direct && desc->index_input) {
    const auto scaled = b_.create_lnast_tmp();
    auto       m      = b_.add_child(Lnast_ntype::create_mult());
    b_.lnast->add_child(m, Lnast_node::create_ref(scaled));
    leaf(m, kvar);
    leaf(m, std::to_string(desc->step));
    auto p = b_.add_child(Lnast_ntype::create_plus());
    b_.lnast->add_child(p, Lnast_node::create_ref(ivar));
    leaf(p, scaled);
    leaf(p, std::to_string(desc->first));
    for (const auto& d : sio->get_input_pin_decls()) {
      if (d.port_id == *desc->index_input) {
        type_bits(ivar, bits_of(body->get_input_pin(d.name), *bio, d.name), d.unsign);
      }
    }
  }
  for (const auto& bn : body->body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(bn) == Ntype_op::Memory && !sub.declare_view(bn)) {
      why = sub.why;
      b_.pop_stmts();
      return false;
    }
  }
  if (!sub.lower_cells()) {
    why = sub.why;
    b_.pop_stmts();
    return false;
  }
  auto body_value = [&](std::string_view name) -> std::optional<std::string> {
    auto spin = body->get_output_pin(name);
    auto drs  = spin.get_driver_pins();
    if (drs.empty()) {
      sub.why = "compact loop output without a driver";
      return std::nullopt;
    }
    return sub.operand(*drs.begin());
  };
  // Values first (they may name a carry variable that is overwritten below), then the stores.
  absl::flat_hash_set<std::string> carry_vars;
  for (const auto& c : carries) {
    carry_vars.insert(c.var);
  }
  std::vector<std::pair<std::string, std::string>> stores;  // variable, value
  std::vector<std::pair<std::string, std::pair<int, bool>>> tys;
  for (const auto& c : carries) {
    auto v = body_value(c.out_name);
    if (!v) {
      why = sub.why;
      b_.pop_stmts();
      return false;
    }
    bool uns = true;
    for (const auto& d : sio->get_output_pin_decls()) {
      if (d.port_id == c.out) {
        uns = d.unsign;
      }
    }
    stores.emplace_back(c.var, *v);
    tys.emplace_back(c.var, std::make_pair(out_decl_bits(c.out_name), uns));
  }
  for (const auto& f : finals) {
    auto v = body_value(f.name);
    if (!v) {
      why = sub.why;
      b_.pop_stmts();
      return false;
    }
    stores.emplace_back(f.var, *v);
    tys.emplace_back(f.var, std::make_pair(out_decl_bits(f.name), f.uns));
  }
  for (auto& [var, val] : stores) {
    if (carry_vars.contains(val) && val != var && stores.size() > 1) {
      const auto snap = b_.create_lnast_tmp();
      store_raw(snap, val);
      val = snap;
    }
  }
  for (size_t i = 0; i < stores.size(); ++i) {
    store_raw(stores[i].first, stores[i].second);
    type_bits(tys[i].first, tys[i].second.first, tys[i].second.second);
  }
  b_.pop_stmts();
  b_.lnast->add_child(lp, Lnast_node::create_const("val"));

  for (const auto& c : carries) {
    if (auto it = out_conn.find(c.out); it != out_conn.end()) {
      name_of_.insert_or_assign(it->second.get_class_index(), c.var);
    }
  }
  for (const auto& f : finals) {
    name_of_.insert_or_assign(out_conn.at(f.port).get_class_index(), f.var);
  }
  return true;
}

// A module instance: fcall(inst, module, store(port, arg)...), then one tuple_get
// per output that something reads -- the shape the slang front end emits.
bool Lowering::lower_sub(const hhds::Node_class& node) {
  if (node.is_loop_subnode()) {
    return lower_loop(node);
  }
  auto sub_io = node.get_subnode_io();
  if (!sub_io) {
    why = "unresolved sub instance (M1)";
    return false;
  }
  const auto modname = std::string(sub_io->get_name());
  if (livehd::graph_util::is_marker_module_name(modname)) {
    // fproperty / lgassert / lgundef: body-less, stateless, no hardware and no readable output
    // (a later read of one fails by name below), so they only matter to the checkers.
    return true;
  }
  if (parent_ != nullptr) {
    why = "module call inside a compact-loop body (inline it first)";
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
  if (f.latch) {
    // `reg l:[latch=true, enable=gate, enable_high=false]`: the gate IS the enable (graph/cell.cpp),
    // and the stored value follows `din` while it is open. No clock, no edge.
    attr("latch", "true");
    attr("enable", en ? *en : std::string("true"));
    if (auto pc = get_driver_of_sink_name(f.node, "posclk"); pc.is_const() && const_of(pc).is_known_false()) {
      attr("enable_high", "false");
    }
  }
  // Clock: always explicit, so the result never depends on the default-clock naming rule.
  if (auto c = get_driver_of_sink_name(f.node, "clock_pin"); !f.latch && !c.is_invalid() && !c.is_const()) {
    auto o = operand(c);
    if (!o) {
      return false;
    }
    attr("clock_pin", *o);
  }
  if (auto pc = get_driver_of_sink_name(f.node, "posclk"); !f.latch && pc.is_const() && const_of(pc).is_known_false()) {
    attr("posclk", "false");
  }
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
    bool is_async = false;
    if (auto a = get_driver_of_sink_name(f.node, "async"); a.is_const()) {
      is_async = !const_of(a).is_known_false();
    }
    attr("sync", is_async ? "false" : "true");
    attr("negreset", neg ? "true" : "false");
  } else if (!init_txt.empty()) {
    attr("initial", init_txt);  // power-on value, no reset signal (the front end's spelling)
    attr("reset_pin", "false");
  }
  auto store_gated = [&]() {
    if (!en || f.latch) {  // a latch's enable is its gate (the attribute above), not a write condition
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

bool Lowering::lower_cells() {
  if (parent_ == nullptr) {
    find_overlays();
  }
  // Dependency order (Kahn): hhds `forward` order is not topological around loop-last cells
  // such as Sub, and a use must follow its definition in the LNAST text. Registers and graph
  // inputs are already named, so they never block.
  {
    struct Item {
      hhds::Node_class node;
      int              mem  = -1;  // index into mems_ for an async memory read
      size_t           port = 0;
    };
    std::vector<Item> nodes;
    for (auto node : graph_.body().nodes(hhds::Node_order::forward)) {
      const auto op = type_op_of(node);
      if (op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::IO && op != Ntype_op::Memory) {
        nodes.push_back({node, -1, 0});
      }
    }
    for (size_t mi = 0; mi < mems_.size(); ++mi) {
      for (size_t pi = 0; pi < mems_[mi].ports.size(); ++pi) {
        const auto& pt = mems_[mi].ports[pi];
        if (pt.kind == Ntype::Memory_rdport_async && !pt.out.is_invalid()) {
          nodes.push_back({mems_[mi].node, static_cast<int>(mi), pi});
        }
      }
      if (!mems_[mi].read_all.is_invalid()) {
        nodes.push_back({mems_[mi].node, static_cast<int>(mi), SIZE_MAX});
      }
    }
    std::vector<size_t>                                       waiting(nodes.size(), 0);
    absl::flat_hash_map<hhds::Class_index, std::vector<size_t>> waiters;
    std::vector<size_t>                                       ready;
    for (size_t i = 0; i < nodes.size(); ++i) {
      absl::flat_hash_set<hhds::Class_index> seen;
      auto need = [&](const hhds::Pin_class& d) {
        if (d.is_invalid() || d.is_const() || name_of_.contains(d.get_class_index()) || !seen.insert(d.get_class_index()).second) {
          return;
        }
        waiters[d.get_class_index()].push_back(i);
        ++waiting[i];
      };
      if (nodes[i].mem >= 0 && nodes[i].port == SIZE_MAX) {
        // read_all has no combinational inputs -- except a loop-body array's, which is the value it
        // was handed with this trip's writes applied
        const auto& vm = mems_[static_cast<size_t>(nodes[i].mem)];
        if (vm.view) {
          need(vm.bus);
          for (const auto& pt : vm.ports) {
            if (pt.kind == Ntype::Memory_rdport_write) {
              need(pt.addr);
              need(pt.din);
              need(pt.en);
            }
          }
        }
      } else if (nodes[i].mem >= 0) {
        const auto& vm = mems_[static_cast<size_t>(nodes[i].mem)];
        need(vm.ports[nodes[i].port].addr);
        if (vm.view && vm.name.empty()) {
          need(vm.bus);
        }
      } else {
        for (const auto& sink : nodes[i].node.inp_sorted_pins()) {
          for (const auto& d : sink.get_driver_pins()) {
            if (d.get_master_node() == nodes[i].node) {
              continue;  // a compact loop's carry self-edge
            }
            need(d);
          }
        }
      }
      if (waiting[i] == 0) {
        ready.push_back(i);
      }
    }
    // First the order alone (nothing is emitted). When nothing is ready and cells remain, the
    // cells left are on, or behind, a ring: an instance among them is called early, with what it
    // still waits for read as settled values (see Late). The one waiting for the fewest drivers
    // goes first -- a choice that depends on the graph alone, so the text is stable.
    std::vector<uint8_t> forced(nodes.size(), 0);
    size_t               done = 0;
    for (size_t head = 0; done != nodes.size();) {
      if (head == ready.size()) {
        size_t pick = SIZE_MAX;
        for (size_t i = 0; i < nodes.size(); ++i) {
          if (waiting[i] == 0 || forced[i] != 0 || nodes[i].mem >= 0 || type_op_of(nodes[i].node) != Ntype_op::Sub
              || nodes[i].node.is_loop_subnode()) {
            continue;
          }
          const auto io = nodes[i].node.get_subnode_io();
          if (!io || livehd::graph_util::is_marker_module_name(io->get_name()) || !nodes[i].node.get_subnode_graph()) {
            continue;
          }
          if (pick == SIZE_MAX || waiting[i] < waiting[pick]) {
            pick = i;
          }
        }
        if (pick == SIZE_MAX) {
          std::string names;
          for (size_t i = 0, shown = 0; i < nodes.size() && shown < 4; ++i) {
            if (waiting[i] != 0 && forced[i] == 0) {
              const auto op  = type_op_of(nodes[i].node);
              names         += std::format(" {}{}",
                                   Ntype::get_name(op),
                                   op == Ntype_op::Sub && nodes[i].node.get_subnode_io()
                                               ? std::format("({})", std::string(nodes[i].node.get_subnode_io()->get_name()))
                                               : "");
              ++shown;
            }
          }
          why = std::format("combinational cycle through cells that are not instances; stuck:{}", names);
          return false;
        }
        forced[pick]  = 1;
        waiting[pick] = 0;
        ready.push_back(pick);
      }
      const auto i = ready[head++];
      ++done;
      std::vector<hhds::Pin_class> produced;
      if (nodes[i].mem >= 0) {
        const auto& mm = mems_[static_cast<size_t>(nodes[i].mem)];
        produced.push_back(nodes[i].port == SIZE_MAX ? mm.read_all : mm.ports[nodes[i].port].out);  // only THIS read's data
      } else {
        for (const auto& o : nodes[i].node.out_sorted_pins()) {
          produced.push_back(o);
        }
      }
      for (const auto& o : produced) {
        auto it = waiters.find(o.get_class_index());
        if (it == waiters.end()) {
          continue;
        }
        for (const auto w : it->second) {
          if (forced[w] == 0 && --waiting[w] == 0) {
            ready.push_back(w);
          }
        }
      }
    }
    for (const auto i : ready) {
      // An instance called before a driver it reads exists (it was called early, or it reads one of
      // its own outputs): that driver is a net read at its settled value.
      std::vector<hhds::Class_index> borrowed;
      std::vector<std::string>       borrowed_as;
      if (nodes[i].mem < 0 && type_op_of(nodes[i].node) == Ntype_op::Sub && !nodes[i].node.is_loop_subnode()) {
        std::vector<hhds::Pin_class> missing;
        for (const auto& sink : nodes[i].node.inp_sorted_pins()) {
          for (const auto& d : sink.get_driver_pins()) {
            if (!d.is_invalid() && !d.is_const() && !name_of_.contains(d.get_class_index())
                && std::ranges::find(borrowed, d.get_class_index()) == borrowed.end()) {
              missing.push_back(d);
              borrowed.push_back(d.get_class_index());
            }
          }
        }
        borrowed.clear();
        for (const auto& d : missing) {
          const int bits = bits_of(d);
          if (bits <= 0) {
            why = "an instance on a ring reads a value of unknown width";
            return false;
          }
          const auto net = std::format("__late_{}", root_->late_count_++);
          b_.create_declare_stmts(net, "mut", max_str(bits, is_unsign(d)), min_str(bits, is_unsign(d)));
          const auto tmp = b_.create_settled_read_stmts(net);
          type_spec(tmp, d);
          name_of_[d.get_class_index()] = tmp;
          borrowed.push_back(d.get_class_index());
          borrowed_as.push_back(tmp);
          if (livehd::debug::env<"LHD_LG2LNAST_DEBUG">() != nullptr) {
            const auto dn = d.get_master_node();
            std::fprintf(stderr,
                         "lg2lnast: %s: instance of %s reads %s late (driver: %s%s, %d bits)\n",
                         std::string(graph_.get_name()).c_str(),
                         std::string(nodes[i].node.get_subnode_io()->get_name()).c_str(),
                         net.c_str(),
                         std::string(Ntype::get_name(type_op_of(dn))).c_str(),
                         dn == nodes[i].node ? " (itself)" : "",
                         bits);
          }
          late_.push_back({d, net});
        }
      }
      const bool ok = nodes[i].mem < 0            ? lower_node(nodes[i].node)
                      : nodes[i].port == SIZE_MAX ? lower_read_all(mems_[static_cast<size_t>(nodes[i].mem)])
                                                  : lower_async_read(mems_[static_cast<size_t>(nodes[i].mem)], nodes[i].port);
      if (!ok) {
        return false;
      }
      for (size_t b = 0; b < borrowed.size(); ++b) {
        // Everyone else waits for the driver itself -- which the call just named, when the instance
        // reads one of its own outputs.
        if (auto it = name_of_.find(borrowed[b]); it != name_of_.end() && it->second == borrowed_as[b]) {
          name_of_.erase(it);
        }
      }
      flush();
    }
    for (const auto& late : late_) {
      auto value = operand(late.driver);
      if (!value) {
        return false;
      }
      store_raw(late.net, *value);
    }
    late_.clear();
    flush();
  }
  return true;
}

std::shared_ptr<Lnast> Lowering::run(livehd::lnast_stream::Sink* stream) {
  sink_ = stream;
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

  if (sink_ != nullptr) {
    sink_->begin(b_.lnast);
  }

  // Registers first so every read of Q has a name.
  for (auto node : graph_.body().nodes(hhds::Node_order::forward)) {
    const bool latch = type_op_of(node) == Ntype_op::Latch;
    if (type_op_of(node) != Ntype_op::Flop && !latch) {
      continue;
    }
    for (const char* pin : {"pipe_min", "pipe_max"}) {
      if (!latch && !livehd::graph_util::get_driver_of_sink_name(node, pin).is_invalid()) {
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
    flop_next_.push_back({nm, node, latch});
  }

  flush();
  for (auto node : graph_.body().nodes(hhds::Node_order::forward)) {
    if (type_op_of(node) == Ntype_op::Memory && !declare_memory(node)) {
      return nullptr;
    }
  }
  flush();

  if (!lower_cells()) {
    return nullptr;
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
  flush();
  for (const auto& m : mems_) {
    if (!lower_memory_tail(m)) {
      return nullptr;
    }
    flush();
  }
  for (const auto& f : flop_next_) {
    if (!lower_flop_next(f)) {
      return nullptr;
    }
    flush();
  }
  if (sink_ != nullptr) {
    sink_->end();
  }
  return b_.lnast;
}

}  // namespace

void Pass_lg2lnast::setup() {
  Eprp_method m1("pass.lg2lnast", "lower lgraphs to ordinary front-end LNAST (prototype)", &Pass_lg2lnast::work);
  m1.add_label_optional("passes", "comma separated LNAST stream passes applied as statements are produced (balance)", "");
  register_pass(m1);
}

Pass_lg2lnast::Pass_lg2lnast(const Eprp_var& var) : Pass("pass.lg2lnast", var) {}

void Pass_lg2lnast::work(Eprp_var& var) {
  Pass_lg2lnast p(var);
  (void)lower(var.graphs, var.get("passes"), var.lnasts);
}

bool Pass_lg2lnast::lower(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view passes,
                          std::vector<std::shared_ptr<Lnast>>& lnasts, std::vector<std::pair<std::string, std::string>>* skipped,
                          bool warn) {
  // A compact-loop body is lowered inline into its caller (a `for`); on its own it is not a module.
  absl::flat_hash_set<std::string> loop_bodies;
  for (const auto& g : graphs) {
    if (!g) {
      continue;
    }
    for (const auto& n : g->body().nodes(hhds::Node_order::forward)) {
      if (type_op_of(n) == Ntype_op::Sub && n.is_loop_subnode()) {
        if (auto cg = n.get_subnode_graph()) {
          loop_bodies.insert(std::string(cg->get_name()));
        }
      }
    }
  }
  for (const auto& g : graphs) {
    if (!g || loop_bodies.contains(std::string(g->get_name()))) {
      continue;
    }
    Lowering lw(*g);
    // `passes`: comma separated stream passes (lnast/lnast_stream.hpp) run on the
    // statements as they are produced; the output is what the last one forwards.
    livehd::lnast_stream::Chain     chain;
    livehd::lnast_stream::Tree_sink tree;
    std::string                     chain_error;
    if (!livehd::lnast_stream::parse_chain(std::string(passes), chain, chain_error)) {
      livehd::diag::warn("pass.lg2lnast", "bad-passes", "unsupported").msg("{}", chain_error).emit();
      return false;
    }
    auto ln = chain.empty() ? lw.run() : lw.run(&chain.link(tree));
    if (ln && !chain.empty()) {
      ln = tree.result();
    }
    if (!ln) {
      if (skipped != nullptr) {
        skipped->emplace_back(std::string(g->get_name()), lw.why);
      }
      if (warn) {
        livehd::diag::warn("pass.lg2lnast", "graph-skipped", "unsupported")
            .msg("graph {} skipped: {}", std::string(g->get_name()), lw.why)
            .emit();
      }
      continue;
    }
    lnasts.push_back(std::move(ln));
  }
  return true;
}
