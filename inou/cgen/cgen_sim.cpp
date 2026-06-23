// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_sim.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "cell.hpp"        // Ntype / Ntype_op
#include "node_util.hpp"  // //graph:graph — livehd::graph_util::* helpers
#include "str_tools.hpp"  // str_tools::ends_with

using livehd::graph_util::bits_of;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::hydrate_const;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::is_type_register;
using livehd::graph_util::is_type_sub;
using livehd::graph_util::is_unsign;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {
// Width of a pin, floored at 1 (Slop<N> requires N >= 1).
int wbits_of(const hhds::Pin_class& pin) {
  int b = pin.is_invalid() ? 1 : bits_of(pin);
  return b <= 0 ? 1 : b;
}

// Edges of a node sorted by sink port_id (selector/operand order).
auto sorted_inp(const hhds::Node_class& node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.sink.get_port_id() < b.sink.get_port_id(); });
  return edges;
}

const char* op_name(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum: return "Sum";
    case Ntype_op::Mult: return "Mult";
    case Ntype_op::Div: return "Div";
    case Ntype_op::And: return "And";
    case Ntype_op::Or: return "Or";
    case Ntype_op::Xor: return "Xor";
    case Ntype_op::Not: return "Not";
    case Ntype_op::LT: return "LT";
    case Ntype_op::GT: return "GT";
    case Ntype_op::EQ: return "EQ";
    case Ntype_op::SHL: return "SHL";
    case Ntype_op::SRA: return "SRA";
    case Ntype_op::Mux: return "Mux";
    case Ntype_op::Hotmux: return "Hotmux";
    case Ntype_op::Get_mask: return "Get_mask";
    case Ntype_op::Set_mask: return "Set_mask";
    case Ntype_op::Sext: return "Sext";
    case Ntype_op::Nconst: return "Nconst";
    default: return "op?";
  }
}
}  // namespace

std::string Cgen_sim::cpp_id(std::string_view name) {
  std::string r;
  r.reserve(name.size() + 1);
  // strip LNAST backtick quotes (`a[0]`)
  if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
    name.remove_prefix(1);
    name.remove_suffix(1);
  }
  for (char c : name) {
    r.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
  }
  if (r.empty() || std::isdigit(static_cast<unsigned char>(r.front()))) {
    r.insert(r.begin(), '_');
  }
  return r;
}

hhds::Pin_class Cgen_sim::get_driver(const hhds::Pin_class& sink) {
  if (sink.is_invalid()) {
    return {};
  }
  auto edges = sink.inp_edges();
  if (edges.empty()) {
    return {};
  }
  return edges.front().driver;
}

hhds::Pin_class Cgen_sim::find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    return node.get_sink_pin(name);
  }
  auto pid = Ntype::get_sink_pid(op, name);
  if (pid == livehd::Port_invalid) {
    return {};
  }
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == pid) {
      return e.sink;
    }
  }
  return {};
}

std::string Cgen_sim::operand(const hhds::Pin_class& dpin, int target_bits, int sign_mode) {
  const std::string tw = std::to_string(target_bits);
  if (dpin.is_invalid()) {
    return absl::StrCat("Slop<", tw, ">::create_integer(0)");
  }
  if (is_const_pin(dpin)) {
    // Exact via the shared pyrope codec (handles wide constants too).
    return absl::StrCat("Slop<", tw, ">::from_pyrope(\"", hydrate_const(dpin).to_pyrope(), "\")");
  }
  auto it = pin2var.find(dpin.get_class_index());
  if (it == pin2var.end()) {
    return absl::StrCat("Slop<", tw, ">::create_integer(0) /*UNRESOLVED*/");  // already a Slop<tw> expr
  }
  const std::string& base     = it->second;
  const bool         unsigned_ = (sign_mode == -1) || (sign_mode == 0 && is_unsign(dpin));
  if (unsigned_) {
    return absl::StrCat(base, ".zext_to<", tw, ">()");  // zero-extend / mask
  }
  return absl::StrCat("Slop<", tw, ">{", base, "}");  // signed sext via the hlop cross-width ctor
}

std::string Cgen_sim::node_expr(const hhds::Node_class& node, int wbits) {
  const auto op = type_op_of(node);
  const auto tw = std::to_string(wbits);
  auto       e  = sorted_inp(node);

  auto fold = [&](const char* method) -> std::string {
    if (e.empty()) {
      return absl::StrCat("Slop<", tw, ">::create_integer(0)");
    }
    std::string s = operand(e[0].driver, wbits);
    for (size_t i = 1; i < e.size(); ++i) {
      s = absl::StrCat(s, ".", method, "(", operand(e[i].driver, wbits), ")");
    }
    return s;
  };

  switch (op) {
    case Ntype_op::Sum: {
      std::string adds, subs;
      for (const auto& ed : e) {
        auto& tgt = (ed.sink.get_port_id() == 0) ? adds : subs;
        if (!tgt.empty()) {
          tgt += ", ";
        }
        tgt += operand(ed.driver, wbits);
      }
      return absl::StrCat("Slop<", tw, ">::sum_op({", adds, "}, {", subs, "})");
    }
    case Ntype_op::And: return fold("and_op");
    case Ntype_op::Or: return fold("or_op");
    case Ntype_op::Xor: return fold("xor_op");
    case Ntype_op::Mult: return fold("mult_op");
    case Ntype_op::Not:
      return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : absl::StrCat(operand(e[0].driver, wbits), ".not_op()");
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ: {
      if (e.size() < 2) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      int cw = std::max({wbits_of(e[0].driver), wbits_of(e[1].driver), 1});
      const char* m = (op == Ntype_op::LT) ? "lt_op" : (op == Ntype_op::GT) ? "gt_op" : "eq_op";
      return absl::StrCat(operand(e[0].driver, cw), ".", m, "(", operand(e[1].driver, cw), ").zext_to<", tw, ">()");
    }
    case Ntype_op::SHL:
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits);
      }
      return absl::StrCat(operand(e[0].driver, wbits), ".shl_op(", operand(e[1].driver, wbits), ")");
    case Ntype_op::SRA:
      if (e.size() < 2) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits, /*signed=*/1);
      }
      // arithmetic shift: the shifted operand must be read as signed
      return absl::StrCat(operand(e[0].driver, wbits, /*signed=*/1), ".sra_op(", operand(e[1].driver, wbits), ")");
    case Ntype_op::Get_mask: {
      // value (e[0]) + optional mask (e[1]). Result is a non-negative magnitude,
      // so read operands UNSIGNED (zero-extend). The unary form is the common
      // tolg width-adjust; lower it to a plain zext.
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      if (e.size() == 1) {
        return operand(e[0].driver, wbits, /*unsigned=*/-1);
      }
      int cw = std::max({wbits_of(e[0].driver), wbits_of(e[1].driver), wbits, 1});
      return absl::StrCat("(", operand(e[0].driver, cw, -1), ".get_mask_op(", operand(e[1].driver, cw, -1), ")).zext_to<", tw,
                          ">()");
    }
    case Ntype_op::Set_mask: {
      // value.set_mask_op(mask, newbits) — best effort at the node width.
      if (e.size() < 3) {
        return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits, -1);
      }
      return absl::StrCat(operand(e[0].driver, wbits, -1), ".set_mask_op(", operand(e[1].driver, wbits, -1), ", ",
                          operand(e[2].driver, wbits, -1), ")");
    }
    case Ntype_op::Sext: {
      // value sign-extended from a bit position (2nd input, normally constant).
      if (e.empty()) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      int frombit = wbits - 1;
      if (e.size() > 1 && is_const_pin(e[1].driver)) {
        frombit = static_cast<int>(hydrate_const(e[1].driver).to_just_i64());
      }
      return absl::StrCat(operand(e[0].driver, wbits, /*signed=*/1), ".sext_op(", std::to_string(frombit), ")");
    }
    case Ntype_op::Mux:
    case Ntype_op::Hotmux: {
      if (e.size() < 3) {
        return absl::StrCat("Slop<", tw, ">::create_integer(0)");
      }
      std::string vals;
      for (size_t i = 1; i < e.size(); ++i) {
        if (!vals.empty()) {
          vals += ", ";
        }
        vals += operand(e[i].driver, wbits);
      }
      const char* m = (op == Ntype_op::Mux) ? "mux_op" : "hotmux_op";
      return absl::StrCat("Slop<", tw, ">::", m, "(", operand(e[0].driver, wbits), ", {", vals, "})");
    }
    default:
      // Compiling fallback: pass the first input through at the node width (the
      // per-operand width conversion already enforces wbits). Covers width-trim
      // Get_mask and not-yet-modeled ops; the iverilog differential test flags
      // any that need exact lowering.
      return e.empty() ? absl::StrCat("Slop<", tw, ">::create_integer(0)") : operand(e[0].driver, wbits);
  }
}

void Cgen_sim::do_from_graph(const std::shared_ptr<hhds::Graph>& graph) {
  pin2var.clear();
  tmp_cnt = 0;

  hhds::Graph* g   = graph.get();
  auto         gio = g->get_io();
  const auto   gname = std::string{graph->get_name()};
  const auto   mod   = cpp_id(gname);

  // VCD trace (compile.sim.vcd=FILE): only the --top module emits it (so a
  // hierarchy writes one file). top empty -> single-module run, emit anyway.
  const auto entity  = gname.substr(gname.rfind('.') + 1);
  const bool vcd_on  = !vcd_file.empty() && (top.empty() || entity == top);

  const std::string filename
      = odir.empty() ? absl::StrCat(gname, ".hpp") : absl::StrCat(odir, "/", gname, ".hpp");
  auto fout = std::make_shared<File_output>(filename);

  fout->append("// Generated by inou.cgen.sim (LiveHD, TODO 3d). Do not edit.\n");
  fout->append("#pragma once\n#include <array>\n#include \"slop.hpp\"\n");
  if (vcd_on) {
    fout->append("#include <memory>\n#include \"vcd_writer.hpp\"\n");
  }
  fout->append("\n");

  // ---- IO decls (sorted by port_id) ----
  struct Io {
    std::string field;
    std::string raw;
    int         bits;
    bool        is_input;
    uint32_t    port_id;
  };
  std::vector<Io> ios;
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      ios.push_back({cpp_id(d.name), std::string{d.name}, 0, true, static_cast<uint32_t>(d.port_id)});
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      ios.push_back({cpp_id(d.name), std::string{d.name}, 0, false, static_cast<uint32_t>(d.port_id)});
    }
  }
  for (auto& io : ios) {
    auto pin = io.is_input ? g->get_input_pin(io.raw) : g->get_output_pin(io.raw);
    io.bits  = pin.is_invalid() ? 1 : bits_of(pin, *gio, io.raw);
    if (io.bits <= 0) {
      io.bits = 1;
    }
  }
  std::sort(ios.begin(), ios.end(), [](const Io& a, const Io& b) { return a.port_id < b.port_id; });

  // ---- flops (Flop cells; Latch/Memory -> later phase) ----
  struct Flop {
    hhds::Node_class         node;
    std::string              member;
    int                      bits;
    int                      depth;   // pipe_min shift-register depth (>=1)
    std::vector<std::string> stages;  // depth-1 intermediate stage members (q is the last)
  };
  std::vector<Flop> flops;
  for (auto node : g->fast_class()) {
    if (!is_type_flop(node)) {
      continue;
    }
    auto qpin  = node.get_driver_pin(0);
    auto nm    = pin_name_of(qpin);
    int  depth = 1;
    if (auto pm = get_driver(find_sink_pin(node, "pipe_min")); !pm.is_invalid() && is_const_pin(pm)) {
      depth = std::max<int>(1, static_cast<int>(hydrate_const(pm).to_just_i64()));
    }
    Flop f{node, cpp_id(nm.empty() ? wire_name(qpin) : std::string{nm}), wbits_of(qpin), depth, {}};
    for (int i = 0; i < depth - 1; ++i) {
      f.stages.push_back(absl::StrCat(f.member, "_p", i));
    }
    flops.push_back(std::move(f));
  }

  // ---- memories (Ntype_op::Memory -> std::array<Slop<bits>,size> member) ----
  struct MemPort {
    bool            rd = false;
    hhds::Pin_class addr, enable, din;
    int             dout_pid = -1;  // read port: driver pin id = n_wr + rd_index
    int             rdidx    = -1;
    int             wridx    = -1;  // write port index (for the FWD bit)
  };
  struct Mem {
    hhds::Node_class     node;
    std::string          member;
    int                  bits = 1, size = 0, type = 2, fwd = 0;
    std::vector<MemPort> ports;  // real ports, in port order (phantoms dropped)
  };
  std::vector<Mem> mems;
  for (auto node : g->fast_class()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    Mem                  m;
    m.node = node;
    std::vector<MemPort> pv;  // indexed by port_id (raw_pid/12)
    for (auto e : node.inp_edges()) {
      int  raw = static_cast<int>(e.sink.get_port_id());
      auto pn  = Ntype::get_sink_name(Ntype_op::Memory, raw);
      auto pid = static_cast<size_t>(raw) / 12;
      if (pn == "bits") {
        m.bits = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "size") {
        m.size = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "type") {
        m.type = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "fwd") {
        m.fwd = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "wensize" || pn == "init") {
        // v1: init=0 assumed; wensize (sub-word write-enable) not modeled yet
      } else {
        if (pv.size() <= pid) {
          pv.resize(pid + 1);
        }
        if (str_tools::ends_with(pn, "clock_pin")) {
          // clock irrelevant to the functional sim
        } else if (str_tools::ends_with(pn, "addr")) {
          pv[pid].addr = e.driver;
        } else if (str_tools::ends_with(pn, "enable")) {
          pv[pid].enable = e.driver;
        } else if (str_tools::ends_with(pn, "din")) {
          pv[pid].din = e.driver;
        } else if (str_tools::ends_with(pn, "rdport")) {
          pv[pid].rd = !hydrate_const(e.driver).is_known_false();
        }
      }
    }
    if (m.bits <= 0) {
      m.bits = 1;
    }
    int n_wr = 0;
    for (auto& p : pv) {
      if (!p.addr.is_invalid() && !p.rd) {
        ++n_wr;
      }
    }
    int rd = 0, wr = 0;
    for (auto& p : pv) {
      if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid()) {
        continue;  // phantom slot (shared clock landed here)
      }
      if (p.rd) {
        p.dout_pid = n_wr + rd;
        p.rdidx    = rd++;
      } else {
        p.wridx = wr++;
      }
      m.ports.push_back(p);
    }
    m.member = cpp_id(default_instance_name(node));
    mems.push_back(std::move(m));
  }

  // ---- sub-module instances (Ntype_op::Sub -> nested struct member) ----
  struct Sub {
    hhds::Node_class node;
    std::string      inst;           // member name
    std::string      callee_struct;  // cpp_id of the callee module name
  };
  std::vector<Sub>         subs;
  std::vector<std::string> sub_includes;  // distinct callee headers
  for (auto node : g->fast_class()) {
    if (!is_type_sub(node)) {
      continue;
    }
    auto sio = node.get_subnode_io();
    if (!sio) {
      continue;
    }
    std::string cname{sio->get_name()};
    if (cname.empty() || cname == livehd::graph_util::lgassert_module_name) {
      continue;  // a recognized primitive, not a real sub-graph
    }
    subs.push_back({node, cpp_id(default_instance_name(node)), cpp_id(cname)});
    auto hdr = absl::StrCat(cname, ".hpp");
    if (std::find(sub_includes.begin(), sub_includes.end(), hdr) == sub_includes.end()) {
      sub_includes.push_back(hdr);
    }
  }

  for (const auto& h : sub_includes) {
    fout->append(absl::StrCat("#include \"", h, "\"\n"));
  }
  fout->append("struct ", mod, " {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append("  Slop<", std::to_string(f.bits), "> ", s, "{};  // pipe stage\n");
    }
    fout->append("  Slop<", std::to_string(f.bits), "> ", f.member, "{};  // flop\n");
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  std::array<Slop<", m.bits, ">, ", m.size, "> ", m.member, "{};  // memory\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {  // sync read: registered dout
        fout->append(absl::StrCat("  Slop<", m.bits, "> ", m.member, "_q", p.rdidx, "{};  // sync read reg\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.callee_struct, " ", s.inst, ";  // sub instance\n"));
  }

  // ---- In / Out ----
  fout->append("  struct In {\n");
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
    }
  }
  fout->append("  };\n  struct Out {\n");
  for (const auto& io : ios) {
    if (!io.is_input) {
      fout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
    }
  }
  fout->append("  };\n");

  // ---- VCD trace state (compile.sim.vcd): traces In, Out, and flop state of
  // this (top) module. Lazily opens the file on the first cycle(). ----
  struct VcdSig {
    std::string var, vname, accessor;
    int         bits;
  };
  std::vector<VcdSig> vsig;
  if (vcd_on) {
    int  k   = 0;
    auto add = [&](const std::string& nm, int b, const std::string& acc) {
      vsig.push_back({absl::StrCat("__vv", k++), (b > 1 ? absl::StrCat(nm, "[", b - 1, ":0]") : nm), acc, b});
    };
    for (const auto& io : ios) {
      if (io.is_input) {
        add(io.field, io.bits, absl::StrCat("in.", io.field));
      }
    }
    for (const auto& io : ios) {
      if (!io.is_input) {
        add(io.field, io.bits, absl::StrCat("o.", io.field));
      }
    }
    for (const auto& f : flops) {
      add(f.member, f.bits, f.member);
      for (const auto& s : f.stages) {
        add(s, f.bits, s);
      }
    }
    fout->append("  std::unique_ptr<vcd::VCDWriter> __vcd;\n  unsigned __vcd_cycle = 0;\n");
    for (const auto& v : vsig) {
      fout->append(absl::StrCat("  vcd::VarPtr ", v.var, ";\n"));
    }
    fout->append("  void __vcd_init() {\n");
    fout->append(absl::StrCat("    __vcd = std::make_unique<vcd::VCDWriter>(\"", vcd_file, "\", vcd::makeVCDHeader());\n"));
    for (const auto& v : vsig) {
      fout->append(
          absl::StrCat("    ", v.var, " = __vcd->register_var(\"", mod, "\", \"", v.vname, "\", vcd::VariableType::wire, ", v.bits, ");\n"));
    }
    fout->append("  }\n");
  }

  // ---- reset_cycle: each flop (+ its pipe stages) to its reset value (the
  // `initial` pin, normally a constant; default 0). ----
  fout->append("  void reset_cycle() {\n");
  for (const auto& f : flops) {
    auto        init = get_driver(find_sink_pin(f.node, "initial"));
    std::string rv   = init.is_invalid() ? absl::StrCat("Slop<", f.bits, ">::create_integer(0)") : operand(init, f.bits);
    for (const auto& s : f.stages) {
      fout->append("    ", s, " = ", rv, ";\n");
    }
    fout->append("    ", f.member, " = ", rv, ";\n");
  }
  for (const auto& m : mems) {
    fout->append("    for (auto& __e : ", m.member, ") __e = Slop<", std::to_string(m.bits), ">::create_integer(0);\n");
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("    ", m.member, "_q", p.rdidx, " = Slop<", m.bits, ">::create_integer(0);\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append("    ", s.inst, ".reset_cycle();\n");
  }
  fout->append("  }\n");

  // ---- cycle ----
  fout->append("  Out cycle(In in) {\n");
  // map input ports and flop q outputs into pin2var
  for (const auto& io : ios) {
    if (!io.is_input) {
      continue;
    }
    auto pin = g->get_input_pin(io.raw);
    if (!pin.is_invalid()) {
      pin2var[pin.get_class_index()] = absl::StrCat("in.", io.field);
    }
  }
  for (const auto& f : flops) {
    auto qpin = f.node.get_driver_pin(0);
    if (!qpin.is_invalid()) {
      pin2var[qpin.get_class_index()] = f.member;
    }
  }

  // combinational SSA bindings, in dependency order
  for (auto node : g->forward_class()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Memory) {
      // Emit the read data (read-first: the current array) and register each
      // read port's dout driver pin so downstream nodes resolve. Writes commit
      // at the edge (below). Sync read (type 0) consumers see the dout register.
      for (const auto& m : mems) {
        if (m.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        for (const auto& p : m.ports) {
          if (!p.rd || p.addr.is_invalid()) {
            continue;
          }
          auto dout = node.create_driver_pin(static_cast<hhds::Port_id>(p.dout_pid));
          if (m.type == 1) {
            pin2var[dout.get_class_index()] = absl::StrCat(m.member, "_q", std::to_string(p.rdidx));
          } else {
            // async read of the current array, then same-cycle write forwarding
            // (per-write-port FWD bit), matching the cgen_memory wrapper.
            int  ab  = std::max(1, bits_of(p.addr));
            auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
            std::string body
                = absl::StrCat("    Slop<", m.bits, "> ", var, " = [&]{ size_t __r = static_cast<size_t>((", operand(p.addr, ab),
                               ").to_just_i64()); Slop<", m.bits, "> __v = __r < ", m.size, " ? ", m.member,
                               "[__r] : Slop<", m.bits, ">::create_integer(0);");
            for (const auto& wp : m.ports) {
              if (wp.rd || wp.addr.is_invalid() || !((m.fwd >> wp.wridx) & 1)) {
                continue;
              }
              std::string we = "true";
              if (!wp.enable.is_invalid()) {
                if (is_const_pin(wp.enable)) {
                  if (hydrate_const(wp.enable).is_known_false()) {
                    continue;
                  }
                } else {
                  we = absl::StrCat("(", operand(wp.enable, 1), ").is_known_true()");
                }
              }
              body += absl::StrCat(" { size_t __w = static_cast<size_t>((", operand(wp.addr, std::max(1, bits_of(wp.addr))),
                                   ").to_just_i64()); if (", we, " && __w == __r) __v = ", operand(wp.din, m.bits), "; }");
            }
            body += " return __v; }();  // mem read\n";
            fout->append(body);
            pin2var[dout.get_class_index()] = var;
          }
        }
        break;
      }
      continue;
    }
    if (op == Ntype_op::Sub) {
      // Instantiate the callee directly: build its In from this Sub's input
      // drivers, call child.cycle(), and register each output driver pin to the
      // returned struct field (no intermediate wires).
      for (const auto& s : subs) {
        if (s.node.get_class_index() != node.get_class_index()) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (!sio) {
          break;
        }
        fout->append(absl::StrCat("    ", s.callee_struct, "::In ", s.inst, "__i;\n"));
        for (const auto& d : sio->get_input_pin_decls()) {
          auto drv = get_driver(node.get_sink_pin(d.name));
          int  wb  = d.bits > 0 ? static_cast<int>(d.bits) : 1;
          fout->append(absl::StrCat("    ", s.inst, "__i.", cpp_id(d.name), " = ", operand(drv, wb), ";\n"));
        }
        fout->append(absl::StrCat("    auto ", s.inst, "__o = ", s.inst, ".cycle(", s.inst, "__i);\n"));
        for (const auto& d : sio->get_output_pin_decls()) {
          auto opin = node.get_driver_pin(d.name);
          if (!opin.is_invalid()) {
            pin2var[opin.get_class_index()] = absl::StrCat(s.inst, "__o.", cpp_id(d.name));
          }
        }
        break;
      }
      continue;
    }
    if (Ntype::is_multi_driver(op)) {
      continue;
    }
    if (!node.has_out_edges() || is_type_register(node)) {
      continue;
    }
    auto dpin = node.get_driver_pin(0);
    int  wb   = wbits_of(dpin);
    auto var  = absl::StrCat("cg_", std::to_string(tmp_cnt++));
    fout->append(absl::StrCat("    Slop<", wb, "> ", var, " = ", node_expr(node, wb), ";  // ", op_name(op), "\n"));
    pin2var[dpin.get_class_index()] = var;
  }

  // flop next-state (all computed from current state before any commit). Each
  // stage/q: reset ? rstval : (enable ? value_in : hold), mirroring cgen's
  // always block. value_in = din for stage0, the previous stage otherwise.
  for (const auto& f : flops) {
    auto din  = get_driver(find_sink_pin(f.node, "din"));
    auto rstp = get_driver(find_sink_pin(f.node, "reset_pin"));
    auto initp = get_driver(find_sink_pin(f.node, "initial"));
    auto enp  = get_driver(find_sink_pin(f.node, "enable"));
    bool negreset = false;
    if (auto np = get_driver(find_sink_pin(f.node, "negreset")); !np.is_invalid() && is_const_pin(np)) {
      negreset = !hydrate_const(np).is_known_false();
    }
    const std::string rstval = initp.is_invalid() ? absl::StrCat("Slop<", f.bits, ">::create_integer(0)") : operand(initp, f.bits);
    std::string       rtest;          // C++ bool: reset asserted (empty = no reset)
    bool              reset_always = false;
    if (!rstp.is_invalid()) {
      if (is_const_pin(rstp)) {
        reset_always = !hydrate_const(rstp).is_known_false();
      } else {
        rtest = absl::StrCat(operand(rstp, 1), negreset ? ".is_known_false()" : ".is_known_true()");
      }
    }
    std::string etest;                // C++ bool: write enabled (empty = always)
    if (!enp.is_invalid() && !is_const_pin(enp)) {
      etest = absl::StrCat(operand(enp, 1), ".is_known_true()");
    }
    auto next_of = [&](const std::string& value_in, const std::string& hold) -> std::string {
      if (reset_always) {
        return rstval;
      }
      std::string core = etest.empty() ? value_in : absl::StrCat("(", etest, " ? ", value_in, " : ", hold, ")");
      return rtest.empty() ? core : absl::StrCat("(", rtest, " ? ", rstval, " : ", core, ")");
    };
    const std::string din_expr = din.is_invalid() ? f.member : operand(din, f.bits);
    if (f.depth <= 1) {
      fout->append("    auto ", f.member, "_next = ", next_of(din_expr, f.member), ";\n");
    } else {
      fout->append("    auto ", f.stages[0], "_next = ", next_of(din_expr, f.stages[0]), ";\n");
      for (size_t i = 1; i < f.stages.size(); ++i) {
        fout->append("    auto ", f.stages[i], "_next = ", next_of(f.stages[i - 1], f.stages[i]), ";\n");
      }
      fout->append("    auto ", f.member, "_next = ", next_of(f.stages.back(), f.member), ";\n");
    }
  }

  // outputs (from current state)
  fout->append("    Out o;\n");
  for (const auto& io : ios) {
    if (io.is_input) {
      continue;
    }
    auto spin = g->get_output_pin(io.raw);
    auto drv  = spin.is_invalid() ? hhds::Pin_class{} : get_driver(spin);
    if (drv.is_invalid()) {
      fout->append("    // output ", io.field, " is undriven\n");
    } else {
      fout->append("    o.", io.field, " = ", operand(drv, io.bits), ";\n");
    }
  }

  // VCD dump (pre-commit, current-state values at this cycle's timestamp)
  if (vcd_on) {
    fout->append("    if (!__vcd) __vcd_init();\n");
    fout->append("    vcd::global_timestamp = __vcd_cycle * 10;\n");
    for (const auto& v : vsig) {
      fout->append(absl::StrCat("    __vcd->change(", v.var, ", vcd::to_vcd_bits(", v.accessor, ", ", v.bits, "));\n"));
    }
    fout->append("    ++__vcd_cycle;\n");
  }

  // commit flops (+ pipe stages) at the clock edge
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append("    ", s, " = ", s, "_next;\n");
    }
    fout->append("    ", f.member, " = ", f.member, "_next;\n");
  }

  // memory edge: sync-read registers sample the CURRENT array (before writes),
  // then writes apply (read-before-write, matching a reg-array sync memory).
  for (const auto& m : mems) {
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1 && !p.addr.is_invalid()) {
        int ab = std::max(1, bits_of(p.addr));
        fout->append(absl::StrCat("    ", m.member, "_q", std::to_string(p.rdidx), " = [&]{ size_t __a = static_cast<size_t>((",
                                  operand(p.addr, ab), ").to_just_i64()); return __a < ", m.size, " ? ", m.member,
                                  "[__a] : Slop<", m.bits, ">::create_integer(0); }();\n"));
      }
    }
    for (const auto& p : m.ports) {
      if (p.rd || p.addr.is_invalid() || p.din.is_invalid()) {
        continue;
      }
      // write enable: invalid -> always; const-false -> never; else runtime test
      std::string guard = "true";
      if (!p.enable.is_invalid()) {
        if (is_const_pin(p.enable)) {
          if (hydrate_const(p.enable).is_known_false()) {
            continue;
          }
        } else {
          guard = absl::StrCat("(", operand(p.enable, 1), ").is_known_true()");
        }
      }
      int ab = std::max(1, bits_of(p.addr));
      fout->append(absl::StrCat("    { size_t __a = static_cast<size_t>((", operand(p.addr, ab), ").to_just_i64()); if (", guard,
                                " && __a < ", m.size, ") ", m.member, "[__a] = ", operand(p.din, m.bits), "; }\n"));
    }
  }
  fout->append("    return o;\n  }\n");

  fout->append("};\n");
}
