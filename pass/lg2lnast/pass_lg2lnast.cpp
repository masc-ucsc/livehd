//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lg2lnast.hpp"

#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
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
  };
  std::vector<Mem_rec> mems_;
  int                  mem_cnt_ = 0;
  // Per-port sink name: pid = port * stride + the pin's base offset, spelled "<pid><base>" past port 0.
  static std::string mem_sink(int port, std::string_view base) {
    if (port == 0) {
      return std::string(base);
    }
    const auto off = Ntype::get_sink_pid(Ntype_op::Memory, base);
    return std::format("{}{}", port * Ntype::Memory_port_stride + off, base);
  }
  bool declare_memory(const hhds::Node_class& node);
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
      const auto& c = const_of(d);
      // Unknown bits print as `?` (`0ub0?????`), the literal the front ends already emit.
      return c.to_pyrope();
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
      std::vector<std::pair<std::string, std::string>> arms;  // cond tmp, value
      for (const auto& [control, value] : hm.arms) {
        auto c = operand(control);
        auto v = operand(value);
        if (!c || !v) {
          return false;
        }
        arms.emplace_back(emit(Lnast_ntype::create_ne(), {*c, "0"}, control, false), *v);
      }
      std::string fb = "0";
      if (!hm.fallback.is_invalid()) {
        auto f = operand(hm.fallback);
        if (!f) {
          return false;
        }
        fb = *f;
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
  if (const_i64("wensize").value_or(1) != 1) {
    why = "memory with byte/bit write enables (wensize != 1; M2)";
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
  auto t = b_.create_lnast_tmp();
  auto g = b_.add_child(Lnast_ntype::create_tuple_get());
  b_.lnast->add_child(g, Lnast_node::create_ref(t));
  b_.lnast->add_child(g, Lnast_node::create_ref(m.name));
  leaf(g, *a);
  type_spec(t, pt.out);
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
      if (!gated(pt.en, [&] {
            auto st = b_.add_child(Lnast_ntype::create_store());
            b_.lnast->add_child(st, Lnast_node::create_ref(m.name));
            leaf(st, *a);
            leaf(st, *d);
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
  // Clock: always explicit, so the result never depends on the default-clock naming rule.
  if (auto c = get_driver_of_sink_name(f.node, "clock_pin"); !c.is_invalid() && !c.is_const()) {
    auto o = operand(c);
    if (!o) {
      return false;
    }
    attr("clock_pin", *o);
  }
  if (auto pc = get_driver_of_sink_name(f.node, "posclk"); pc.is_const() && const_of(pc).is_known_false()) {
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
    if (type_op_of(node) == Ntype_op::Memory && !declare_memory(node)) {
      return nullptr;
    }
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
      if (op != Ntype_op::Flop && op != Ntype_op::IO && op != Ntype_op::Memory) {
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
        // read_all has no combinational inputs
      } else if (nodes[i].mem >= 0) {
        need(mems_[static_cast<size_t>(nodes[i].mem)].ports[nodes[i].port].addr);
      } else {
        for (const auto& sink : nodes[i].node.inp_sorted_pins()) {
          for (const auto& d : sink.get_driver_pins()) {
            need(d);
          }
        }
      }
      if (waiting[i] == 0) {
        ready.push_back(i);
      }
    }
    size_t done = 0;
    for (size_t head = 0; head < ready.size(); ++head) {
      const auto i = ready[head];
      const bool ok = nodes[i].mem < 0 ? lower_node(nodes[i].node)
                      : nodes[i].port == SIZE_MAX ? lower_read_all(mems_[static_cast<size_t>(nodes[i].mem)])
                                                  : lower_async_read(mems_[static_cast<size_t>(nodes[i].mem)], nodes[i].port);
      if (!ok) {
        return nullptr;
      }
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
          if (--waiting[w] == 0) {
            ready.push_back(w);
          }
        }
      }
    }
    if (done != nodes.size()) {
      std::string names;
      for (size_t i = 0, shown = 0; i < nodes.size() && shown < 4; ++i) {
        if (waiting[i] != 0) {
          const auto op = type_op_of(nodes[i].node);
          names += std::format(" {}{}", Ntype::get_name(op),
                               op == Ntype_op::Sub && nodes[i].node.get_subnode_io() ? std::format("({})", std::string(nodes[i].node.get_subnode_io()->get_name())) : "");
          ++shown;
        }
      }
      why = std::format("cycle through instance-level dependencies (a call needs an output of a call that needs it); stuck:{}", names);
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
  for (const auto& m : mems_) {
    if (!lower_memory_tail(m)) {
      return nullptr;
    }
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
