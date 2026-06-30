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
      const char* m   = (op == Ntype_op::Mux) ? "mux_op" : "hotmux_op";
      std::string sel = operand(e[0].driver, wbits);  // Slop<tw> selector
      if (op == Ntype_op::Mux) {
        // A binary Mux selector is an INDEX (0..n-1). Slop encodes a boolean
        // `true` as all-ones, which as a wide selector value falls outside
        // [0,n) -> mux_op returns invalid and the mux never picks. Keep only
        // the ceil(log2(n)) low index bits (then re-widen to tw): all-ones -> 1.
        size_t n_vals = e.size() - 1;
        int    sel_w  = 1;
        while ((static_cast<size_t>(1) << sel_w) < n_vals) {
          ++sel_w;
        }
        if (sel_w < wbits) {
          sel = absl::StrCat("(", sel, ").zext_to<", sel_w, ">().zext_to<", wbits, ">()");
        }
      }
      return absl::StrCat("Slop<", tw, ">::", m, "(", sel, ", {", vals, "})");
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
  // A `test` block lowers to a compiler-minted (`%`-named) comb — a testbench,
  // not synthesizable hardware. Its asserts are checked by running the `lhd sim`
  // driver (prp_sim), never by emitting a Slop unit (which would pull in the
  // formal-property header it does not need). Skip it; the kernel's sim_into()
  // drops it from the build's source list too so the two stay consistent.
  if (!entity.empty() && entity.front() == '%') {
    return;
  }
  // `is_top` = the testbench-driven module (vs an instantiated sub-module). The
  // clock/reset waveform CONFIG + the functional reset window live on the top so
  // the reset behaves identically whether or not a VCD is dumped; `vcd_on` adds
  // the actual VCD writer/vars (a strict subset: VCD only when also top).
  const bool is_top  = top.empty() || entity == top;
  const bool vcd_on  = !vcd_file.empty() && is_top;

  const std::string base = odir.empty() ? std::string{gname} : absl::StrCat(odir, "/", gname);
  auto              hout = std::make_shared<File_output>(absl::StrCat(base, ".hpp"));  // interface
  auto              fout = std::make_shared<File_output>(absl::StrCat(base, ".cpp"));  // definitions ("the slop")

  // Header (<name>.hpp): data members + In/Out + method DECLARATIONS only. A
  // module that instantiates this one #includes this small header (by-value
  // member), so it recompiles when this interface changes, not when the body
  // (in the .cpp) does. The cycle()/reset_cycle() bodies live in the .cpp and
  // are compiled exactly once.
  hout->append("// Generated by inou.cgen.sim (LiveHD, TODO 3d). Do not edit.\n");
  hout->append("#pragma once\n#include <array>\n#include <cstdint>\n#include <map>\n#include <string>\n#include <vector>\n"
               "#include \"slop.hpp\"\n");
  // Forward-declare the signal record (full definition in checkpoint.hpp, included
  // by the .cpp); the header only needs it for the describe_signals() signature.
  hout->append("namespace hlop::ckpt { struct Signal; }\n");
  if (vcd_on) {
    hout->append("#include <memory>\n#include \"vcd_writer.hpp\"\n");
  }
  hout->append("\n");

  // Source (<name>.cpp): includes its own header (which transitively pulls the
  // child interface headers) and holds every method body.
  fout->append("// Generated by inou.cgen.sim (LiveHD, TODO 3d). Do not edit.\n");
  fout->append(absl::StrCat("#include \"", gname, ".hpp\"\n"));
  fout->append("#include \"checkpoint.hpp\"  // name-keyed dump_state/load_state helpers\n\n");

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
    int                      depth;          // pipe_min shift-register depth (>=1)
    std::vector<std::string> stages;         // depth-1 intermediate stage members (q is the last)
    bool                     posedge = true; // false = negedge flop (posclk known-false)
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
    // posedge (default) vs negedge clock: the comptime `posclk` pin, known-false
    // means negedge -- only matters for which sub-tick slot dumps its VCD data.
    if (auto pc = get_driver(find_sink_pin(node, "posclk")); !pc.is_invalid() && is_const_pin(pc)) {
      f.posedge = !hydrate_const(pc).is_known_false();
    }
    for (int i = 0; i < depth - 1; ++i) {
      f.stages.push_back(absl::StrCat(f.member, "_p", i));
    }
    flops.push_back(std::move(f));
  }

  // Identify the top-level clock INPUT port -- the net feeding the flops'
  // clock_pin. In a cycle-based sim it is otherwise just an input forced to 0
  // (which is why the clock never toggled), so we trace it as a dedicated clock
  // waveform driven by `step` rather than as an ordinary io signal. Reset is NOT
  // special: it is an ordinary input the testbench pokes (`acc.reset = ...`) and
  // is traced like any other input.
  std::string clk_field;
  {
    absl::flat_hash_map<pin_key_t, std::string> in_pin_field;
    for (const auto& io : ios) {
      if (!io.is_input) {
        continue;
      }
      auto p = g->get_input_pin(io.raw);
      if (!p.is_invalid()) {
        in_pin_field[p.get_class_index()] = io.field;
      }
    }
    for (const auto& f : flops) {
      auto d = get_driver(find_sink_pin(f.node, "clock_pin"));
      if (!d.is_invalid()) {
        auto it = in_pin_field.find(d.get_class_index());
        if (it != in_pin_field.end()) {
          clk_field = it->second;
          break;
        }
      }
    }
    // name fallback for the common `clock` port -- only when there are flops (a
    // purely combinational module has no clock edge, so a data input that happens
    // to be named `clock` must trace as an ordinary signal, not the waveform).
    if (clk_field.empty() && !flops.empty()) {
      for (const auto& io : ios) {
        if (io.is_input && io.field == "clock") {
          clk_field = "clock";
          break;
        }
      }
    }
  }
  // The VCD clock waveform label defaults to the real port name; a `tick
  // clocks=(name=ratio)` clause can override the label at run time.
  const std::string clk_label = clk_field.empty() ? "clock" : clk_field;

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
    // Whole-array support (the `update` bus is driven): one update/read_all bus
    // instead of N per-entry ports. registered when a clock is present.
    hhds::Pin_class update, update_enable, init, reset, clock;
    bool             has_read_all = false;
    bool is_whole() const { return !update.is_invalid(); }
    bool registered() const { return !clock.is_invalid(); }
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
      auto pid = static_cast<size_t>(raw) / Ntype::Memory_port_stride;
      if (pn == "bits") {
        m.bits = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "size") {
        m.size = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "type") {
        m.type = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "fwd") {
        m.fwd = static_cast<int>(hydrate_const(e.driver).to_just_i64());
      } else if (pn == "update") {
        m.update = e.driver;
      } else if (pn == "update_enable") {  // MUST precede ends_with("enable") below
        m.update_enable = e.driver;
      } else if (pn == "reset") {
        m.reset = e.driver;
      } else if (pn == "init") {
        m.init = e.driver;  // whole-array reset-value bus (runtime); plain mem: comptime, still assumed 0
      } else if (pn == "wensize") {
        // wensize (sub-word write-enable) not modeled yet
      } else {
        if (pv.size() <= pid) {
          pv.resize(pid + 1);
        }
        if (str_tools::ends_with(pn, "clock_pin")) {
          m.clock = e.driver;  // presence marks a registered whole-array (timing only)
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
    for (const auto& e2 : node.out_edges()) {  // read_all is a DRIVER pin (not in inp_edges)
      if (static_cast<hhds::Port_id>(e2.driver.get_port_id()) == Ntype::Memory_readall_pid) {
        m.has_read_all = true;
        break;
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
    hout->append(absl::StrCat("#include \"", h, "\"\n"));
  }
  hout->append("struct ", mod, " {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      hout->append("  Slop<", std::to_string(f.bits), "> ", s, "{};  // pipe stage\n");
    }
    hout->append("  Slop<", std::to_string(f.bits), "> ", f.member, "{};  // flop\n");
  }
  for (const auto& m : mems) {
    hout->append(absl::StrCat("  std::array<Slop<", m.bits, ">, ", m.size, "> ", m.member, "{};  // memory\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {  // sync read: registered dout
        hout->append(absl::StrCat("  Slop<", m.bits, "> ", m.member, "_q", p.rdidx, "{};  // sync read reg\n"));
      }
    }
  }
  for (const auto& s : subs) {
    hout->append(absl::StrCat("  ", s.callee_struct, " ", s.inst, ";  // sub instance\n"));
  }

  // ---- In / Out ----
  hout->append("  struct In {\n");
  for (const auto& io : ios) {
    if (io.is_input) {
      hout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
    }
  }
  hout->append("  };\n  struct Out {\n");
  for (const auto& io : ios) {
    if (!io.is_input) {
      hout->append("    Slop<", std::to_string(io.bits), "> ", io.field, "{};\n");
    }
  }
  hout->append("  };\n");

  // Persistent input latch. The testbench pokes inputs (`acc.x = v` -> __in.x)
  // and advances with `step()`. Outputs are read by recomputing from the current
  // committed state (`acc.y` -> peek(__in).y -- correct before the first step
  // too, unlike a cached snapshot); internal registers are read as plain members
  // (`acc.total`). Keeping __in in the instance (not the driver) means a future
  // state copy / checkpoint captures the pokes too.
  hout->append("  In __in{};\n");

  // ---- VCD trace state (compile.sim.vcd): traces In, Out, and flop state of
  // this (top) module. Lazily opens the file on the first cycle(). The clock is
  // the one signal NOT traced as an ordinary io port -- it gets the synthetic
  // `__vv_clk` waveform driven by `step`; reset and every other input trace
  // normally (reset is just a poked input now). ----
  struct VcdSig {
    std::string var, vname, accessor;
    int         bits;
    bool        posedge;  // dumped at the +3 (posedge) or +half+3 (negedge) sub-tick
  };
  std::vector<VcdSig> vsig;
  bool                any_negedge = false;
  if (vcd_on) {
    int  k   = 0;
    auto add = [&](const std::string& nm, int b, const std::string& acc, bool pe = true) {
      vsig.push_back({absl::StrCat("__vv", k++), (b > 1 ? absl::StrCat(nm, "[", b - 1, ":0]") : nm), acc, b, pe});
      if (!pe) {
        any_negedge = true;
      }
    };
    for (const auto& io : ios) {
      if (io.is_input && io.field != clk_field) {
        add(io.field, io.bits, absl::StrCat("in.", io.field));  // clock gets the dedicated waveform
      }
    }
    for (const auto& io : ios) {
      if (!io.is_input) {
        add(io.field, io.bits, absl::StrCat("o.", io.field));
      }
    }
    for (const auto& f : flops) {
      add(f.member, f.bits, f.member, f.posedge);
      for (const auto& s : f.stages) {
        add(s, f.bits, s, f.posedge);
      }
    }
    hout->append("  std::unique_ptr<vcd::VCDWriter> __vcd;\n");
    hout->append("  vcd::VarPtr __vv_clk;\n");
    for (const auto& v : vsig) {
      hout->append(absl::StrCat("  vcd::VarPtr ", v.var, ";\n"));
    }
  }

  // Uniquify the clock waveform LABEL against the traced signal names, so a user
  // signal literally named e.g. "clock" can't duplicate the synthetic clock var
  // at VCD registration (vsig is empty unless tracing, so this is a no-op without
  // VCD).
  auto uniquify_label = [&](const std::string& label) {
    auto taken = [&](const std::string& s) {
      for (const auto& v : vsig) {
        if (v.vname == s) {
          return true;
        }
      }
      return false;
    };
    if (!taken(label)) {
      return label;
    }
    for (int i = 0;; ++i) {
      auto cand = absl::StrCat(label, "_vcd", i);
      if (!taken(cand)) {
        return cand;
      }
    }
  };
  const std::string clk_name_baked = uniquify_label(clk_label);

  // Clock waveform config + the VCD path / period counter. Plain scalars on every
  // module; the driver sets the path + clock ratio/name on each instance.
  {
    std::string vcd_baked = (vcd_file == "1" || vcd_file == "true") ? std::string{} : vcd_file;
    hout->append(absl::StrCat("  std::string __vcd_path = \"", vcd_baked, "\";\n"));
    hout->append("  unsigned __vcd_tick = 0;       // clock periods elapsed (10 VCD time-units each)\n");
    hout->append(absl::StrCat("  std::string __clk_name = \"", clk_name_baked, "\";\n"));
    hout->append("  unsigned __clk_ratio = 1;      // VCD ticks per clock period\n");
  }

  // ---- method declarations, then close the struct (bodies follow in the .cpp) ----
  if (vcd_on) {
    hout->append("  void __vcd_init();\n");
  }
  hout->append("  void reset_cycle();\n");
  hout->append("  Out cycle(In in);\n");
  // one clock edge: cycle() commits next-state (and dumps the pre-edge VCD
  // sample), then peek() recomputes the outputs from the just-committed state so
  // `acc.<out>` reads the POST-edge value. (peek saves/restores flops+mems and
  // suppresses VCD, so it has no net effect besides refreshing __out.)
  hout->append("  void step() { cycle(__in); }  // poke __in, then advance one clock\n");
  hout->append("  Out peek(In in);  // outputs of the current committed state; restores state, no VCD\n");
  // Editable, name-keyed checkpoint (sim_checkpoint_debug_plan): flops/regs ->
  // the `_r` map keyed by the hierarchical `_p`+member name (pyrope literal),
  // memories -> one `<_dir>/<_p><member>.hex` file each, sub-instances recursed.
  // design_hash folds member names+widths (cross-version warn, never reject).
  hout->append("  void dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const;\n");
  hout->append("  void load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir);\n");
  hout->append("  std::uint64_t design_hash() const;\n");
  // Observability (lhd sim --list-signals / --probe / --break-when): every scalar
  // signal (flop / pipe stage / sync-read reg / input) by hierarchical name.
  hout->append("  void describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const;\n");
  hout->append("  void probe_signals(const std::string& _p, std::map<std::string, long>& _m) const;\n");
  hout->append("};\n");

  // ---- __vcd_init definition (source) ----
  if (vcd_on) {
    fout->append("void ", mod, "::__vcd_init() {\n");
    fout->append("  __vcd = std::make_unique<vcd::VCDWriter>(__vcd_path, vcd::makeVCDHeader());\n");
    fout->append(absl::StrCat("  __vv_clk = __vcd->register_var(\"", mod, "\", __clk_name, vcd::VariableType::wire, 1);\n"));
    for (const auto& v : vsig) {
      fout->append(
          absl::StrCat("  ", v.var, " = __vcd->register_var(\"", mod, "\", \"", v.vname, "\", vcd::VariableType::wire, ", v.bits, ");\n"));
    }
    fout->append("}\n");
  }

  // ---- reset_cycle: each flop (+ its pipe stages) to its reset value (the
  // `initial` pin, normally a constant; default 0). ----
  fout->append("void ", mod, "::reset_cycle() {\n");
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
  fout->append("}\n");

  // ---- cycle ----
  // Reset is an ordinary input the testbench pokes into `in` (via step()/__in);
  // no special override here. The flop next-state logic reads it through the
  // normal reset_pin path below.
  fout->append(mod, "::Out ", mod, "::cycle(In in) {\n");
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
        // Combinational whole-array: the `update` bus IS the contents this cycle
        // (no clock edge), so scatter it into `member` BEFORE the reads so reads
        // and read_all observe the post-update value. (Registered whole-arrays
        // apply update at the edge below; reads here see committed state.)
        if (m.is_whole() && !m.registered()) {
          fout->append(absl::StrCat("    slop_apply_update(", m.member, ", ", operand(m.update, m.bits * m.size), ");\n"));
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
        // Async whole-array read: pack the current `member` into one bus.
        if (m.has_read_all) {
          auto ra  = node.create_driver_pin(static_cast<hhds::Port_id>(Ntype::Memory_readall_pid));
          auto var = absl::StrCat("cg_", std::to_string(tmp_cnt++));
          fout->append(absl::StrCat("    auto ", var, " = slop_read_all(", m.member, ");  // read_all\n"));
          pin2var[ra.get_class_index()] = var;
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
    if (Ntype::has_multiple_driver_pins(op)) {
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

  // VCD dump (pre-commit, current-state values at this cycle's timestamp).
  // Guarded by `if (__vcd)` so a path-less instance (e.g. peek(), which clears
  // the path to suppress dumping) never derefs a null writer.
  //
  // One cycle() call advances one clock period of `__clk_ratio` ticks (10 VCD
  // time-units each). Within the period, edges and data are spread out so a
  // waveform viewer shows clock-edge -> data causality:
  //   base          : clock rises 0->1, reset takes this period's level
  //   base + 3      : posedge-sourced data settles (just after the rising edge)
  //   base + half   : clock falls 1->0   (half = ratio*5, the period midpoint)
  //   base + half+3 : negedge-sourced data settles (just after the falling edge)
  // change() only writes when a value differs from the previous timestamp.
  if (vcd_on) {
    fout->append("    if (!__vcd && !__vcd_path.empty()) __vcd_init();\n");
    fout->append("    if (__vcd) {\n");
    fout->append("      const unsigned __b    = __vcd_tick * 10;\n");
    fout->append("      const unsigned __half = (__clk_ratio > 0 ? __clk_ratio : 1) * 5;\n");
    // rising clock edge (reset and other inputs are ordinary traced signals)
    fout->append("      vcd::global_timestamp = __b;\n");
    fout->append("      __vcd->change(__vv_clk, \"1\");\n");
    // posedge-sourced data, just after the rising edge
    fout->append("      vcd::global_timestamp = __b + 3;\n");
    for (const auto& v : vsig) {
      if (v.posedge) {
        fout->append(absl::StrCat("      __vcd->change(", v.var, ", vcd::to_vcd_bits(", v.accessor, ", ", v.bits, "));\n"));
      }
    }
    // falling clock edge at the period midpoint
    fout->append("      vcd::global_timestamp = __b + __half;\n");
    fout->append("      __vcd->change(__vv_clk, \"0\");\n");
    // negedge-sourced data, just after the falling edge (only when present)
    if (any_negedge) {
      fout->append("      vcd::global_timestamp = __b + __half + 3;\n");
      for (const auto& v : vsig) {
        if (!v.posedge) {
          fout->append(absl::StrCat("      __vcd->change(", v.var, ", vcd::to_vcd_bits(", v.accessor, ", ", v.bits, "));\n"));
        }
      }
    }
    fout->append("    }\n");
  }
  // Advance the period counter every cycle -- independent of VCD and of is_top,
  // so the reset window (read at the top of cycle()) tracks identically whether
  // or not a trace is dumped. The member exists on every module.
  fout->append("    __vcd_tick += (__clk_ratio > 0 ? __clk_ratio : 1);\n");

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
    if (m.is_whole() && !m.registered()) {
      continue;  // combinational whole-array: contents applied in the combinational section
    }
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1 && !p.addr.is_invalid()) {
        int ab = std::max(1, bits_of(p.addr));
        fout->append(absl::StrCat("    ", m.member, "_q", std::to_string(p.rdidx), " = [&]{ size_t __a = static_cast<size_t>((",
                                  operand(p.addr, ab), ").to_just_i64()); return __a < ", m.size, " ? ", m.member,
                                  "[__a] : Slop<", m.bits, ">::create_integer(0); }();\n"));
      }
    }
    // Registered whole-array next-state: reset (highest) > per-port write > update
    // (lowest). Apply the bulk update FIRST so the per-port writes below override
    // the entries they touch (later assignment wins).
    std::string whole_close;
    if (m.is_whole()) {
      const int W = m.bits * m.size;
      if (!m.reset.is_invalid()) {
        std::string initbus
            = m.init.is_invalid() ? absl::StrCat("Slop<", W, ">::create_integer(0)") : operand(m.init, W);
        fout->append(absl::StrCat("    if ((", operand(m.reset, 1), ").is_known_true()) { slop_apply_update(", m.member, ", ",
                                  initbus, "); } else {\n"));
        whole_close = "    }\n";
      }
      std::string ue = m.update_enable.is_invalid() ? "" : absl::StrCat("if ((", operand(m.update_enable, 1), ").is_known_true()) ");
      fout->append(absl::StrCat("    ", ue, "slop_apply_update(", m.member, ", ", operand(m.update, W), ");\n"));
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
    fout->append(whole_close);  // close the reset `else` block, if any
  }
  fout->append("    return o;\n}\n");

  // peek(): the outputs implied by the CURRENT (post-last-edge) state, with NO
  // net effect. It runs cycle() (which computes the outputs then advances) but
  // snapshots and restores ALL state first -- flops, pipe stages, memories, AND
  // sub-instances (a child's cycle() commits its own state, so children must be
  // saved too or they double-advance) -- and suppresses VCD, so step() / an
  // output peek observes the post-edge value without perturbing state or the
  // waveform. (A non-copying save/restore is used because the VCD writer member
  // is move-only; sub-instances are plain copyable structs.)
  fout->append(mod, "::Out ", mod, "::peek(In in) {\n");
  for (const auto& f : flops) {
    fout->append(absl::StrCat("    auto _pk_", f.member, " = ", f.member, ";\n"));
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("    auto _pk_", s, " = ", s, ";\n"));
    }
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("    auto _pk_", m.member, " = ", m.member, ";\n"));
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("    auto _pk_", s.inst, " = ", s.inst, ";  // sub-instance snapshot\n"));
  }
  fout->append("    auto _pk_tick = __vcd_tick;\n");  // peek must not perturb the period counter
  if (vcd_on) {
    fout->append("    auto _pk_vcd = std::move(__vcd); auto _pk_vp = __vcd_path; __vcd_path.clear();\n");
  }
  fout->append("    Out o = cycle(in);\n");
  for (const auto& f : flops) {
    fout->append(absl::StrCat("    ", f.member, " = _pk_", f.member, ";\n"));
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("    ", s, " = _pk_", s, ";\n"));
    }
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("    ", m.member, " = _pk_", m.member, ";\n"));
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("    ", s.inst, " = _pk_", s.inst, ";\n"));
  }
  fout->append("    __vcd_tick = _pk_tick;\n");
  if (vcd_on) {
    fout->append("    __vcd = std::move(_pk_vcd); __vcd_path = _pk_vp;\n");
  }
  fout->append("    return o;\n}\n");

  // ---- dump_state: flops/regs -> the `_r` map (by hierarchical name, pyrope
  // literal); memories -> one editable `<_dir>/<_p><member>.hex` each; recurse
  // into sub-instances. Mirrors the reset_cycle/peek member walk. ----
  fout->append("void ", mod,
               "::dump_state(const std::string& _p, std::map<std::string, std::string>& _r, const std::string& _dir) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _r[_p + \"", s, "\"] = ", s, ".to_pyrope();\n"));
    }
    fout->append(absl::StrCat("  _r[_p + \"", f.member, "\"] = ", f.member, ".to_pyrope();\n"));
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  hlop::ckpt::write_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _r[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx, ".to_pyrope();\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".dump_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
  }
  // The persistent input latch __in is state too: a poke-once-and-hold input must
  // survive a restart (the testbench may not re-poke it after the checkpoint cycle).
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  _r[_p + \"__in.", io.field, "\"] = __in.", io.field, ".to_pyrope();\n"));
    }
  }
  fout->append("}\n");

  // ---- load_state: set each flop/reg from the map IF PRESENT (a missing key
  // keeps the reset value -> cross-version reload tolerance); memories from the
  // hex file if present; recurse into sub-instances. ----
  fout->append("void ", mod,
               "::load_state(const std::string& _p, const std::map<std::string, std::string>& _r, const std::string& _dir) {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"", s, "\"); _it != _r.end()) ", s, " = Slop<", f.bits,
                                ">::from_pyrope(_it->second);\n"));
    }
    fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"", f.member, "\"); _it != _r.end()) ", f.member, " = Slop<",
                              f.bits, ">::from_pyrope(_it->second);\n"));
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  hlop::ckpt::read_mem_hex(_dir + \"/\" + _p + \"", m.member, ".hex\", ", m.member, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"", m.member, "_q", p.rdidx, "\"); _it != _r.end()) ",
                                  m.member, "_q", p.rdidx, " = Slop<", m.bits, ">::from_pyrope(_it->second);\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".load_state(_p + \"", s.inst, ".\", _r, _dir);\n"));
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  if (auto _it = _r.find(_p + \"__in.", io.field, "\"); _it != _r.end()) __in.", io.field,
                                " = Slop<", io.bits, ">::from_pyrope(_it->second);\n"));
    }
  }
  fout->append("}\n");

  // ---- design_hash: FNV-fold of every member name+width (+ mem size + sub
  // callee + recursion). Stamped into meta.json; a mismatch on load is a WARNING
  // (editable checkpoints are cross-version on purpose), never a hard reject. ----
  fout->append("std::uint64_t ", mod, "::design_hash() const {\n");
  fout->append("  std::uint64_t _h = hlop::ckpt::kFnvOffset;\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", s, "\"), ", f.bits, ");\n"));
    }
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", f.member, "\"), ", f.bits, ");\n"));
  }
  for (const auto& m : mems) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", m.member,
                              "\"), ", m.bits, "), ", m.size, ");\n"));
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", m.member, "_q", p.rdidx, "\"), ",
                                  m.bits, ");\n"));
      }
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a(_h, \"", s.inst, "\"); _h = hlop::ckpt::fnv1a_u64(_h, ", s.inst,
                              ".design_hash());\n"));
  }
  // Interface (every I/O port name+width+direction) so a port change warns.
  for (const auto& io : ios) {
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a_u64(hlop::ckpt::fnv1a(_h, \"", io.field,
                              "\"), ", io.bits, "), ", io.is_input ? 1 : 2, ");\n"));
  }
  // A coarse logic fingerprint: the cell count (computed at codegen). It is not a
  // full structural hash, but it makes a logic-only change (added/removed cells —
  // same state layout) flip the hash, so a stale checkpoint still warns.
  {
    size_t _ncells = 0;
    for ([[maybe_unused]] auto node : g->fast_class()) {
      ++_ncells;
    }
    fout->append(absl::StrCat("  _h = hlop::ckpt::fnv1a_u64(_h, ", _ncells, ");\n"));
  }
  fout->append("  return _h;\n}\n");

  // ---- describe_signals / probe_signals: the observable scalar state by
  // hierarchical name (flops, pipe stages, sync-read regs, inputs; whole memories
  // and combinational outputs are excluded). describe_* lists name+bits+kind for
  // --list-signals; probe_* reads the current values for --probe / --break-when. ----
  fout->append("void ", mod, "::describe_signals(const std::string& _p, std::vector<hlop::ckpt::Signal>& _v) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _v.push_back({_p + \"", s, "\", ", f.bits, ", \"pipe\"});\n"));
    }
    fout->append(absl::StrCat("  _v.push_back({_p + \"", f.member, "\", ", f.bits, ", \"flop\"});\n"));
  }
  for (const auto& m : mems) {
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _v.push_back({_p + \"", m.member, "_q", p.rdidx, "\", ", m.bits, ", \"memrd\"});\n"));
      }
    }
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  _v.push_back({_p + \"__in.", io.field, "\", ", io.bits, ", \"input\"});\n"));
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".describe_signals(_p + \"", s.inst, ".\", _v);\n"));
  }
  fout->append("}\n");

  fout->append("void ", mod, "::probe_signals(const std::string& _p, std::map<std::string, long>& _m) const {\n");
  for (const auto& f : flops) {
    for (const auto& s : f.stages) {
      fout->append(absl::StrCat("  _m[_p + \"", s, "\"] = ", s, ".to_i64_low();\n"));
    }
    fout->append(absl::StrCat("  _m[_p + \"", f.member, "\"] = ", f.member, ".to_i64_low();\n"));
  }
  for (const auto& m : mems) {
    for (const auto& p : m.ports) {
      if (p.rd && m.type == 1) {
        fout->append(absl::StrCat("  _m[_p + \"", m.member, "_q", p.rdidx, "\"] = ", m.member, "_q", p.rdidx,
                                  ".to_i64_low();\n"));
      }
    }
  }
  for (const auto& io : ios) {
    if (io.is_input) {
      fout->append(absl::StrCat("  _m[_p + \"__in.", io.field, "\"] = __in.", io.field, ".to_i64_low();\n"));
    }
  }
  for (const auto& s : subs) {
    fout->append(absl::StrCat("  ", s.inst, ".probe_signals(_p + \"", s.inst, ".\", _m);\n"));
  }
  fout->append("}\n");
}
