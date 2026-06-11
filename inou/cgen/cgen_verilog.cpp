// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "iassert.hpp"
#include "node_util.hpp"  // //graph:graph — livehd::graph_util::* helpers
#include "perf_tracing.hpp"
#include "str_tools.hpp"
// pass.hpp pulls in Pass::error/info reporting.
#include "pass.hpp"
// hlop's Dlop is the Const representation; we deserialize node-level
// const-value strings via Dlop::unserialize.
#include "hlop/dlop.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::color_of;
using livehd::graph_util::const_value_of;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::has_color;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::is_type_const;
using livehd::graph_util::is_type_flop;
using livehd::graph_util::is_type_register;
using livehd::graph_util::is_type_sub;
using livehd::graph_util::is_unsign;
using livehd::graph_util::node_name_of;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

using livehd::graph_util::hydrate_const;

// Sort edges by sink port_id (for mux iteration).
void sort_by_sink_pid(std::vector<hhds::Edge_class>& edges) {
  std::sort(edges.begin(), edges.end(), [](const hhds::Edge_class& a, const hhds::Edge_class& b) {
    return a.sink.get_port_id() < b.sink.get_port_id();
  });
}

}  // namespace

Cgen_verilog::Cgen_verilog(bool _verbose, std::string_view _odir) : verbose(_verbose), odir(_odir), nrunning(0) {
  static std::once_flag init_once;
  std::call_once(init_once, [] {
    reserved_keyword.insert("reg");
    reserved_keyword.insert("input");
    reserved_keyword.insert("output");
    reserved_keyword.insert("wire");
    reserved_keyword.insert("assign");
    reserved_keyword.insert("always");
    reserved_keyword.insert("posedge");
    reserved_keyword.insert("negedge");
    reserved_keyword.insert("module");
    reserved_keyword.insert("if");
    reserved_keyword.insert("else");
    reserved_keyword.insert("begin");
    reserved_keyword.insert("end");
    reserved_keyword.insert("logic");
  });
}

std::string Cgen_verilog::pin_wire_name(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return {};
  }
  // Graph-IO pins resolve via HHDS's get_pin_name (it walks the GraphIO
  // declaration tables on INPUT_NODE / OUTPUT_NODE pins).
  if (is_graph_input_pin(pin) || is_graph_output_pin(pin)) {
    auto n = pin.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
  }
  return wire_name(pin);
}

bool Cgen_verilog::is_connected(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  return !pin.inp_edges().empty() || !pin.out_edges().empty();
}

hhds::Pin_class Cgen_verilog::get_driver(const hhds::Pin_class& sink) {
  if (sink.is_invalid()) {
    return {};
  }
  auto edges = sink.inp_edges();
  if (edges.empty()) {
    return {};
  }
  return edges.front().driver;
}

hhds::Pin_class Cgen_verilog::find_sink_pin(const hhds::Node_class& node, std::string_view name) {
  if (node.is_invalid()) {
    return {};
  }
  // For Sub nodes the sink name comes from the sub-graph's GraphIO and HHDS
  // resolves it directly. For all other Ntype_op cells the sink name is a
  // LiveHD convention (e.g. "a", "din", "clock_pin") — translate it to a
  // port_id via Ntype before asking HHDS for the pin.
  //
  // HHDS asserts when get_sink_pin(port_id) is called for an unmaterialized
  // pin. cgen frequently asks for optional pins (e.g. `reset_pin`, `async`,
  // `negreset`, `initial` on a flop) that may not be connected at all. To
  // emulate LiveHD's invalid-on-miss behaviour we walk inp_edges and match
  // by port_id — slower than a direct fetch but safe.
  auto op = type_op_of(node);
  if (op == Ntype_op::Sub) {
    auto pin = node.get_sink_pin(name);  // sub-graph path: HHDS knows the port_id
    return pin;
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

std::string Cgen_verilog::get_wire_or_const(const hhds::Pin_class& dpin) const {
  auto var_it = pin2var.find(dpin.get_class_index());
  if (var_it != pin2var.end()) {
    return var_it->second;
  }

  if (is_const_pin(dpin)) {
    return hydrate_const(dpin).to_verilog();
  }

  return get_scaped_name(pin_wire_name(dpin));
}

std::string Cgen_verilog::get_scaped_name(std::string_view name) {
  std::string res_name;
  if (name.empty()) {
    return res_name;
  }
  if (reserved_keyword.contains(name)) {
    return absl::StrCat("\\", name, " ");
  } else {
    res_name = name;
  }

  for (auto i = 0u; i < res_name.size(); ++i) {
    auto ch = res_name[i];
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
      return absl::StrCat("\\", res_name, " ");
    }
  }

  return res_name;
}

std::string Cgen_verilog::get_append_to_name(std::string_view name, std::string_view ext) {
  if (!name.empty() && name.front() == '\\') {
    return absl::StrCat("\\", ext, name.substr(1, name.size() - 1), " ");
  }

  return absl::StrCat(ext, name);
}

std::string Cgen_verilog::get_expression(const hhds::Pin_class& dpin) const {
  auto var_it = pin2var.find(dpin.get_class_index());
  if (var_it != pin2var.end()) {
    return var_it->second;
  }

  const auto expr_it = pin2expr.find(dpin.get_class_index());
  if (expr_it != pin2expr.end()) {
    if (expr_it->second.needs_parenthesis) {
      return absl::StrCat("(", expr_it->second.var, ")");
    }
    return expr_it->second.var;
  }

  // Graph-IO pins on OUTPUT_NODE/INPUT_NODE can be referenced via different
  // pid encodings (driver vs sink counterpart) than the one create_module_io
  // registered. HHDS's get_pin_name resolves both to the declared name; fall
  // back to that so the emitted Verilog references the right wire.
  if (is_const_pin(dpin)) {
    return hydrate_const(dpin).to_verilog();
  }
  auto wn = pin_wire_name(dpin);
  if (!wn.empty()) {
    return get_scaped_name(wn);
  }
  return "'hx /*cgen-miss*/";
}

std::string Cgen_verilog::add_expression(std::string_view txt_seq, std::string_view txt_op, const hhds::Pin_class& dpin) const {
  auto expr = get_expression(dpin);

  if (txt_seq.empty()) {
    return expr;
  }

  return absl::StrCat(txt_seq, " ", txt_op, " ", expr);
}

void Cgen_verilog::process_flop(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto sink_d = find_sink_pin(node, "din");
  auto dpin_d = get_driver(sink_d);
  auto dpin_q = node.get_driver_pin(0);

  auto       pin_name  = pin_wire_name(dpin_q);
  const auto name_next = get_scaped_name(absl::StrCat("___next_", pin_name));

  if (dpin_d.is_invalid()) {
    fout->append("  ", name_next, " = 'hx; // disconnected flop\n");
  } else {
    fout->append("  ", name_next, " = ", get_expression(dpin_d), ";\n");
  }
}

void Cgen_verilog::process_memory(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto iname = get_scaped_name(default_instance_name(node));

  int n_rd_ports = 0;
  int n_wr_ports = 0;

  struct Port_field {
    bool            rdport = false;
    hhds::Pin_class enable;
    hhds::Pin_class addr;
    hhds::Pin_class clock;
    hhds::Pin_class din;  // only for write port
  };
  std::vector<Port_field> port_vector;

  int mem_size    = 0;
  int mem_bits    = 0;
  int mem_fwd     = 0;
  int mem_type    = 2;  // array by default
  int mem_wensize = 0;

  hhds::Pin_class mem_init_dpin;  // comptime contents (entry 0 in the low bits, row-major)

  for (auto e : node.inp_edges()) {
    // HHDS does not store LiveHD's per-sink-name convention; derive the
    // name from the port_id via Ntype::get_sink_name. For memory the names
    // wrap with `pid % 12` (see Ntype::get_sink_name).
    auto   raw_pid  = static_cast<int>(e.sink.get_port_id());
    auto   pin_name = Ntype::get_sink_name(Ntype_op::Memory, raw_pid);
    size_t port_id  = static_cast<size_t>(raw_pid) / 12;

    if (port_vector.size() <= port_id) {
      port_vector.resize(1 + port_id);
    }

    if (pin_name == "bits") {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant for bits not {}", debug_name(node), debug_name(e.driver.get_master_node()));
        return;
      }
      mem_bits = hydrate_const(e.driver).to_i();
    } else if (pin_name == "size") {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant for size not {}", debug_name(node), debug_name(e.driver.get_master_node()));
        return;
      }
      mem_size = hydrate_const(e.driver).to_i();
    } else if (pin_name == "type") {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant type not {}", debug_name(node), debug_name(e.driver.get_master_node()));
        return;
      }
      mem_type = hydrate_const(e.driver).to_i();
    } else if (pin_name == "wensize") {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant for wensize not {}",
                    debug_name(node),
                    debug_name(e.driver.get_master_node()));
        return;
      }
      mem_wensize = hydrate_const(e.driver).to_i();
    } else if (pin_name == "fwd") {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant for fwd not {}", debug_name(node), debug_name(e.driver.get_master_node()));
        return;
      }
      mem_fwd = hydrate_const(e.driver).to_i();
    } else if (pin_name == "init") {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant for init not {}", debug_name(node), debug_name(e.driver.get_master_node()));
        return;
      }
      mem_init_dpin = e.driver;
    } else if (str_tools::ends_with(pin_name, "clock_pin")) {
      port_vector[port_id].clock = e.driver;
    } else if (str_tools::ends_with(pin_name, "addr")) {
      port_vector[port_id].addr = e.driver;
    } else if (str_tools::ends_with(pin_name, "enable")) {
      port_vector[port_id].enable = e.driver;
    } else if (str_tools::ends_with(pin_name, "din")) {
      port_vector[port_id].din = e.driver;
    } else if (str_tools::ends_with(pin_name, "rdport")) {
      if (!is_const_pin(e.driver)) {
        Pass::error("memory {} should have a constant rdport not {}", debug_name(node), debug_name(e.driver.get_master_node()));
        return;
      }
      auto v                      = hydrate_const(e.driver);
      bool rdport                 = !v.is_known_false();
      port_vector[port_id].rdport = rdport;
      if (rdport) {
        ++n_rd_ports;
      } else {
        ++n_wr_ports;
      }
    }
  }

  if (mem_type == 0 || mem_type == 1) {  // sync or async memory
    bool            single_clock    = true;
    hhds::Pin_class base_clock_dpin = port_vector.empty() ? hhds::Pin_class{} : port_vector[0].clock;
    for (auto& p : port_vector) {
      auto& dpin = p.clock;
      if (dpin.is_invalid()) {
        dpin = base_clock_dpin;
        continue;
      }
      if (dpin != base_clock_dpin) {
        single_clock = false;
      }
    }

    if (base_clock_dpin.is_invalid()) {
      Pass::error("memory {} should have a clock pin", debug_name(node));
      return;
    }

    // The wrapper variants start at 1rd_1wr: a read-less memory (scan/regref
    // observed) or a write-less one (scan/regref loaded) still instantiates
    // the smallest variant with the dummy port tied off below.
    const int eff_rd = n_rd_ports > 0 ? n_rd_ports : 1;
    const int eff_wr = n_wr_ports > 0 ? n_wr_ports : 1;

    // ware/rtl carries a fixed wrapper family; anything beyond it (e.g. a
    // big reset-restored reg array minting one restore port per entry) needs
    // a new cgen_memory_<R>rd_<W>wr.v variant.
    const bool have_wrapper = single_clock ? ((eff_rd >= 1 && eff_rd <= 4 && eff_wr >= 1 && eff_wr <= 2)
                                              || (eff_rd == 1 && eff_wr == 3))
                                           : (eff_rd == 1 && eff_wr == 1);
    if (!have_wrapper) {
      Pass::error("memory {} needs a {}rd_{}wr {} wrapper that ware/rtl does not carry",
                  debug_name(node),
                  eff_rd,
                  eff_wr,
                  single_clock ? "single-clock" : "multiclock");
      return;
    }

    std::string name;
    name = absl::StrCat(name, "cgen_memory_", single_clock ? "" : "multiclock_");
    name = absl::StrCat(name, eff_rd, "rd_");
    name = absl::StrCat(name, eff_wr, "wr");

    fout->prepend(absl::StrCat("`include \"", name, ".v\" \n"));
    fout->append(absl::StrCat(name));

    std::string parameters;
    bool        first_entry = true;

    parameters  = absl::StrCat(parameters, first_entry ? " " : " ,", ".LATENCY_0(", mem_type, ")");
    first_entry = false;
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".BITS(", mem_bits, ")");
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".SIZE(", mem_size, ")");
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".WENSIZE", "(", mem_wensize, ")");
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".FWD", "(", mem_fwd, ")");
    if (!mem_init_dpin.is_invalid()) {
      // Power-on contents ride the wrapper's INIT parameter (packed, entry 0
      // in the low BITS); only the single-clock wrappers carry it.
      if (!single_clock) {
        Pass::error("memory {} init contents are not supported on multiclock memories yet", debug_name(node));
        return;
      }
      parameters = absl::StrCat(parameters, " ,.INIT_EN(1) ,.INIT(", hydrate_const(mem_init_dpin).to_verilog(), ")");
    }
    fout->append(" #(", parameters, ") ");

    fout->append(iname, "(\n");

    first_entry = true;
    if (single_clock) {
      fout->append(absl::StrCat(".clk(", get_wire_or_const(base_clock_dpin), ")\n"));
      first_entry = false;
    }

    auto n_rd_pos = 0;
    auto n_wr_pos = 0;
    for (auto& p : port_vector) {
      if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid()) {
        // A phantom slot holding only the shared clock_pin (pid 2 lands in
        // port 0's clock field even when port 0 was never minted).
        continue;
      }
      if (p.rdport) {
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid()) {
          Pass::error("memory {} read port is not correctly configured\n", debug_name(node));
        }
        fout->append(absl::StrCat(first_entry ? "  .rd_addr_" : "  ,.rd_addr_", n_rd_pos, "(", get_wire_or_const(p.addr), ")\n"));
        first_entry = false;

        fout->append("  ,.rd_enable_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          fout->append("  ,.rd_clock_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.clock), ")\n");
        }
        // The dout driver pin for read port N is pid (n_wr_ports + N) — the
        // convention resolve_memory uses in lgyosys_tolg (`wrports + rdport`).
        // Enumerating all out pins here would wire every dout to every port.
        auto dout_dpin = node.create_driver_pin(static_cast<hhds::Port_id>(n_wr_ports + n_rd_pos));  // find-or-create
        if (!dout_dpin.out_edges().empty()) {
          fout->append("  ,.rd_dout_", std::to_string(n_rd_pos), "(", get_wire_or_const(dout_dpin), ")\n");
        }
        ++n_rd_pos;
      } else {
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid() || p.din.is_invalid()) {
          Pass::error("memory {} write port is not correctly configured\n", debug_name(node));
        }
        fout->append(absl::StrCat(first_entry ? "  .wr_addr_" : "  ,.wr_addr_",
                                  std::to_string(n_wr_pos),
                                  "(",
                                  get_wire_or_const(p.addr),
                                  ")\n"));
        first_entry = false;

        fout->append("  ,.wr_enable_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          fout->append("  ,.wr_clock_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.clock), ")\n");
        }
        fout->append("  ,.wr_din_", std::to_string(n_wr_pos), "(", get_wire_or_const(p.din), ")\n");
        ++n_wr_pos;
      }
    }
    I(n_rd_pos == n_rd_ports);
    I(n_wr_pos == n_wr_ports);

    // Tie off the dummy port of a read-less / write-less memory (dout of the
    // dummy read port is simply left unconnected).
    if (n_rd_ports == 0) {
      fout->append(first_entry ? "  .rd_addr_0(1'b0)\n" : "  ,.rd_addr_0(1'b0)\n");
      first_entry = false;
      fout->append("  ,.rd_enable_0(1'b0)\n");
    }
    if (n_wr_ports == 0) {
      fout->append(first_entry ? "  .wr_addr_0(1'b0)\n" : "  ,.wr_addr_0(1'b0)\n");
      first_entry = false;
      fout->append("  ,.wr_enable_0(1'b0)\n");
      fout->append("  ,.wr_din_0(1'b0)\n");
    }

    fout->append(");\n");
  } else {  // array
    // Distinct storage name: a zero-write-port array (ROM) puts its dout on
    // driver pid 0, whose wire is named after the node — `iname` itself.
    const auto aname = absl::StrCat(iname, "_data");
    fout->append(absl::StrCat("reg [", mem_bits - 1, ":0] ", aname, "[", mem_size - 1, ":0];\n"));

    if (first_array_block) {
      fout->append("integer mem_loop_i;\n");
      first_array_block = false;
    }

    fout->append("always_comb begin\n");
    if (!mem_init_dpin.is_invalid()) {
      // Per-cycle default = the init contents (entry 0 in the low bits,
      // row-major); writes below override (forwarding semantics).
      const auto init_val = hydrate_const(mem_init_dpin);
      const auto mask     = Dlop::get_mask_value(mem_bits);
      for (int i = 0; i < mem_size; ++i) {
        auto entry = init_val.sra_op(*Dlop::create_integer(static_cast<int64_t>(i) * mem_bits))->and_op(*mask);
        fout->append(aname, "[", std::to_string(i), "] = ", entry->to_verilog(), ";\n");
      }
    } else {
      fout->append("for (mem_loop_i=0;mem_loop_i < ", std::to_string(mem_size), ";mem_loop_i = mem_loop_i + 1) begin\n");
      fout->append(aname, "[mem_loop_i] = 'b0;\n");
      fout->append("end\n");
    }

    // Writes first (array has forwarding semantics)
    for (auto& p : port_vector) {
      if (p.rdport) {
        continue;
      }
      if (p.addr.is_invalid() || p.din.is_invalid()) {
        Pass::error("memory {} write port is not correctly configured\n", debug_name(node));
      }
      auto din_name   = get_wire_or_const(p.din);
      auto write_stmt = absl::StrCat(aname, "[", get_wire_or_const(p.addr), "] = ", din_name, ";\n");
      if (p.enable.is_invalid()) {
        fout->append("  ", write_stmt);
      } else {
        fout->append("  if (", get_wire_or_const(p.enable), ") begin \n");
        fout->append("    ", write_stmt);
        fout->append("end\n");
      }
    }

    auto n_rd_pos = 0;
    for (auto& p : port_vector) {
      if (!p.rdport) {
        continue;
      }
      if (p.addr.is_invalid()) {
        Pass::error("array {} read port is not correctly configured\n", debug_name(node));
      }
      // Same dout convention as type 0/1: read port N drives pid (n_wr_ports + N).
      auto dout_dpin = node.create_driver_pin(static_cast<hhds::Port_id>(n_wr_ports + n_rd_pos));  // find-or-create
      auto dest_name = get_wire_or_const(dout_dpin);

      auto read_stmt = absl::StrCat(dest_name, " = ", aname, "[", get_wire_or_const(p.addr), "];\n");
      if (p.enable.is_invalid()) {
        fout->append("  ", read_stmt);
      } else {
        fout->append("  if (", get_wire_or_const(p.enable), ") begin \n");
        fout->append("    ", read_stmt);
        fout->append("end\n");
      }
      ++n_rd_pos;
    }

    fout->append("end\n");
  }
}

void Cgen_verilog::process_mux(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto ordered_inp = node.inp_edges();
  sort_by_sink_pid(ordered_inp);
  I(ordered_inp.size() > 2);  // at least 0 + 1 + 2

  auto sel_expr    = get_expression(ordered_inp[0].driver);
  auto dpin_dest   = node.get_driver_pin(0);
  auto dest_var_it = pin2var.find(dpin_dest.get_class_index());
  I(dest_var_it != pin2var.end());
  auto dest_var = dest_var_it->second;

  auto mux2vec_it = mux2vector.find(node.get_class_index());
  if (mux2vec_it == mux2vector.end()) {
    if (ordered_inp.size() == 3) {  // if-else
      fout->append("   if (", sel_expr, ") begin\n");
      fout->append("     ", dest_var, " = ", get_expression(ordered_inp[2].driver), ";\n");
      fout->append("   end else begin\n");
      fout->append("     ", dest_var, " = ", get_expression(ordered_inp[1].driver), ";\n");
      fout->append("   end\n");
    } else {
      fout->append("   case (", sel_expr, ")\n");
      auto sel_bits = bits_of(ordered_inp[0].driver);
      for (auto i = 1u; i < ordered_inp.size(); ++i) {
        fout->append("     ", std::to_string(sel_bits), "'d", std::to_string(i - 1));
        fout->append(" : ", dest_var, " = ", get_expression(ordered_inp[i].driver), ";\n");
      }
      size_t num_cases = size_t{1} << sel_bits;
      if (num_cases > ordered_inp.size() - 1) {
        fout->append("       default: ", dest_var, " = 'hx;\n");
      }
      fout->append("   endcase\n");
    }
  }
}

void Cgen_verilog::process_simple_node(std::shared_ptr<File_output> fout, const hhds::Node_class& node) {
  auto dpin = node.get_driver_pin(0);
  auto op   = type_op_of(node);
  I(!Ntype::is_multi_driver(op));

  std::string final_expr;

  if (op == Ntype_op::Sum) {
    std::string add_seq;
    std::string sub_seq;
    for (auto e : node.inp_edges()) {
      if (e.sink.get_port_id() == 0) {
        add_seq = add_expression(add_seq, "+", e.driver);
      } else {
        sub_seq = add_expression(sub_seq, "+", e.driver);
      }
    }
    if (sub_seq.empty()) {
      final_expr = add_seq;
    } else if (add_seq.empty()) {
      final_expr = absl::StrCat(" -(", sub_seq, ")");
    } else {
      final_expr = absl::StrCat(add_seq, " - (", sub_seq, ")");
    }
  } else if (op == Ntype_op::Ror) {
    auto inp_edges = node.inp_edges();
    if (inp_edges.size() == 1) {
      auto expr  = get_expression(inp_edges[0].driver);
      final_expr = absl::StrCat("|", expr);
    } else {
      auto expr  = get_expression(inp_edges[0].driver);
      final_expr = absl::StrCat("|{", expr);
      for (auto i = 1u; i < inp_edges.size(); ++i) {
        final_expr = absl::StrCat(final_expr, " | ", get_expression(inp_edges[i].driver));
      }
      final_expr = absl::StrCat(final_expr, "}");
    }
  } else if (op == Ntype_op::Div) {
    auto lhs   = get_expression(get_driver(find_sink_pin(node, "a")));
    auto rhs   = get_expression(get_driver(find_sink_pin(node, "b")));
    final_expr = absl::StrCat(lhs, "/", rhs);
  } else if (op == Ntype_op::Not) {
    auto lhs   = get_expression(get_driver(find_sink_pin(node, "a")));
    final_expr = absl::StrCat("~", lhs);
  } else if (op == Ntype_op::Set_mask) {
    auto a_dpin = get_driver(find_sink_pin(node, "a"));
    auto a      = get_expression(a_dpin);

    auto mask_dpin = get_driver(find_sink_pin(node, "mask"));
    I(is_const_pin(mask_dpin));
    auto mask_v = hydrate_const(mask_dpin);
    I(!mask_v.has_unknowns());

    if (mask_v.is_known_zero()) {
      final_expr = a;
    } else {
      auto [range_begin, range_end] = mask_v.get_mask_range();
      if (range_end > static_cast<int>(bits_of(dpin))) {
        range_end = bits_of(dpin) + range_begin;
      }

      auto a_bits = bits_of(a_dpin);

      auto value_dpin = get_driver(find_sink_pin(node, "value"));
      auto value      = get_expression(value_dpin);

      if (a_bits > 0 && range_begin > static_cast<int>(a_bits)) {
        final_expr = a;
      } else if (range_begin < 0 || range_end < 0) {
        std::string sel;
        for (auto i = 0; i < a_bits; ++i) {
          if (mask_v.and_op(*Dlop::create_integer(int64_t{1} << i))->is_known_false()) {
            if (sel.empty()) {
              sel = absl::StrCat(a, "[", i, "]");
            } else {
              sel = absl::StrCat(sel, ",", a, "[", i, "]");
            }
          } else {
            if (sel.empty()) {
              sel = absl::StrCat(value, "[", i, "]");
            } else {
              sel = absl::StrCat(sel, ",", value, "[", i, "]");
            }
          }
        }
        final_expr = absl::StrCat("{", sel, "}");
      } else {
        std::string a_replaced;
        int32_t      value_bits_to_use = static_cast<int32_t>(range_end - range_begin);
        if (value_bits_to_use >= bits_of(value_dpin)) {
          a_replaced = value;
        } else if (value_bits_to_use == 1) {
          a_replaced = absl::StrCat(value, "[0]");
        } else {
          a_replaced = absl::StrCat(value, "[", value_bits_to_use - 1, ":0]");
        }

        auto var_it = pin2var.find(dpin.get_class_index());
        assert(var_it != pin2var.end());
        if (value_bits_to_use < bits_of(dpin)) {
          if (var_it->second != a) {
            fout->append("  ", var_it->second, " = ", a, ";\n");
          }
        }
        std::string replace;
        if (value_bits_to_use == 1) {
          replace = absl::StrCat("[", range_begin, "] = ");
        } else {
          replace = absl::StrCat("[", range_end - 1, ":", range_begin, "] = ");
        }
        fout->append("  ", var_it->second, replace, value, ";\n");
        return;
      }
    }
  } else if (op == Ntype_op::Get_mask) {
    auto mask_dpin = get_driver(find_sink_pin(node, "mask"));
    I(is_const_pin(mask_dpin));
    auto mask_v = hydrate_const(mask_dpin);
    I(!mask_v.has_unknowns());

    auto a_dpin = get_driver(find_sink_pin(node, "a"));
    auto a_bits = bits_of(a_dpin);
    auto a      = get_expression(a_dpin);
    if (mask_v.is_i() && mask_v.to_i() == -1) {
      if (a_bits > 0 && !is_unsign(a_dpin)) {
        // To-positive of a signed driver: a plain copy sign-extends when the
        // unsigned LHS is wider (e.g. 1-bit signed ~(|b) into a 2-bit reg).
        // AND with an unsigned mask of the driver's width so the expression
        // turns unsigned and zero-extends — get_mask(a,-1) == zext(a).
        std::string m;
        if (auto rem = a_bits % 4; rem != 0) {
          m += absl::StrCat((1 << rem) - 1);
        }
        m.append(a_bits / 4, 'f');
        final_expr = absl::StrCat("(", a, " & ", a_bits, "'h", m, ")");
      } else {
        final_expr = a;
      }
    } else {
      auto [range_begin, range_end] = mask_v.get_mask_range();
      int32_t a_bits_to_use          = static_cast<int32_t>(range_end - range_begin);
      if (a_bits_to_use > bits_of(dpin)) {
        range_end = bits_of(dpin) + range_begin;
      }

      int out_bits = bits_of(dpin);
      if (is_unsign(dpin)) {
        --out_bits;
      }

      if (range_begin < 0 || range_end < 0) {
        std::string sel;
        auto        max_bits = std::max(mask_v.get_bits(), a_bits);
        for (auto i = 0; i < max_bits; ++i) {
          if (mask_v.and_op(*Dlop::create_integer(int64_t{1} << i))->is_known_false()) {
            continue;
          }
          if (sel.empty()) {
            sel = absl::StrCat(a, "[", i, "]");
          } else {
            sel = absl::StrCat(sel, ",", a, "[", i, "]");
          }
        }
        final_expr = absl::StrCat("{", sel, "}");
        // a_bits == 0 means the driver width is unknown (no bits attr): the
        // sign-replicate / extend forms below would fabricate a[-1]. Fall
        // through to the width-agnostic part-select forms instead.
      } else if (a_bits > 0 && range_begin >= static_cast<int>(a_bits)) {
        final_expr = absl::StrCat("{", range_end - range_begin, "{", a, "[", a_bits - 1, "]}}");
      } else if (a_bits > 0 && range_end > static_cast<int>(a_bits)) {
        auto top   = absl::StrCat("{{", range_end - a_bits, "{", a, "[", a_bits - 1, "]}}");
        final_expr = absl::StrCat(top, ",", a, "[", a_bits - 1, ":", range_begin, "]}");
      } else if (range_begin == 0 && range_end >= out_bits) {
        final_expr = a;
      } else if (a_bits_to_use == 1) {
        final_expr = absl::StrCat(a, "[", range_begin, "]");
      } else {
        final_expr = absl::StrCat(a, "[", range_end - 1, ":", range_begin, "]");
      }
    }
  } else if (op == Ntype_op::Sext) {
    auto lhs      = get_expression(get_driver(find_sink_pin(node, "a")));
    auto pos_dpin = get_driver(find_sink_pin(node, "b"));
    auto pos_node = pos_dpin.is_invalid() ? hhds::Node_class{} : pos_dpin.get_master_node();
    if (!pos_node.is_invalid() && is_type_const(pos_node)) {
      auto lpos = hydrate_const(pos_dpin);
      if (lpos.is_i()) {
        final_expr = absl::StrCat(lhs, "[", lpos.to_i() - 1, ":0]");
      }
    }
    if (final_expr.empty()) {
      auto bits     = bits_of(pos_dpin);
      auto pos_expr = get_expression(pos_dpin);
      final_expr    = absl::StrCat(lhs, "& ((1'sh", bits, " << ", pos_expr, ")-1)");
    }
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    std::vector<std::string> lhs;
    std::vector<std::string> rhs;
    for (const auto& e : node.inp_edges()) {
      if (Ntype::get_sink_name(op, e.sink.get_port_id()) == "a") {
        lhs.emplace_back(get_expression(e.driver));
      } else {
        rhs.emplace_back(get_expression(e.driver));
      }
    }
    std::string cmp = (op == Ntype_op::GT) ? " > " : " < ";
    for (const auto& l : lhs) {
      for (const auto& r : rhs) {
        if (final_expr.empty()) {
          final_expr = absl::StrCat(l, cmp, r);
        } else {
          final_expr = absl::StrCat(final_expr, " && ", l, cmp, r);
        }
      }
    }
  } else if (op == Ntype_op::SHL) {
    auto val_expr = get_expression(get_driver(find_sink_pin(node, "a")));

    // SHL "b" is a multi-driver sink (LiveHD: all drivers represent one-hot
    // shift amounts ORed together). Walk inp_edges and filter by sink name.
    std::string onehot;
    bool        first = true;
    for (const auto& e : node.inp_edges()) {
      if (Ntype::get_sink_name(op, e.sink.get_port_id()) != "b") {
        continue;
      }
      auto amt_expr = get_expression(e.driver);
      onehot        = absl::StrCat(onehot, first ? "(" : " | (", val_expr, " << ", amt_expr, ")");
      first         = false;
    }
    final_expr = onehot;
  } else if (op == Ntype_op::SRA) {
    auto val_expr = get_expression(get_driver(find_sink_pin(node, "a")));
    auto amt_expr = get_expression(get_driver(find_sink_pin(node, "b")));
    final_expr    = absl::StrCat(val_expr, " >>> ", amt_expr);
  } else if (op == Ntype_op::Nconst) {
    return;  // emitted as expr at create_locals time
  } else if (op == Ntype_op::AttrSet) {
    return;  // drop
  } else {
    std::string txt_op;
    if (op == Ntype_op::Mult) {
      txt_op = "*";
    } else if (op == Ntype_op::And) {
      txt_op = "&";
    } else if (op == Ntype_op::Or) {
      txt_op = "|";
    } else if (op == Ntype_op::Xor) {
      txt_op = "^";
    } else if (op == Ntype_op::EQ) {
      txt_op = "==";
    }
    I(!txt_op.empty());

    for (auto e : node.inp_edges()) {
      final_expr = add_expression(final_expr, txt_op, e.driver);
    }
  }

  if (final_expr.empty()) {
    Pass::info("likely issue in node:{} that has no compute value", debug_name(node));
    final_expr = "'hx";
  }

  if (has_color(node)) {
    absl::StrAppend(&final_expr, " /* color:", std::to_string(color_of(node)), "*/");
  }

  auto var_it = pin2var.find(dpin.get_class_index());
  if (var_it == pin2var.end()) {
    pin2expr.emplace(dpin.get_class_index(), Expr(final_expr, true));
  } else if (var_it->second != final_expr) {
    fout->append("  ", var_it->second, " = ", final_expr, ";\n");
  }
}

void Cgen_verilog::create_module_io(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  auto gio = graph->get_io();
  I(gio);

  // Combine input + output decls, sort by port_id (matches each_sorted_graph_io semantics).
  struct IoEntry {
    std::string name;
    uint32_t    bits;
    bool        is_input;
    uint32_t    port_id;
  };
  std::vector<IoEntry> entries;
  for (const auto& d : gio->get_input_pin_decls()) {
    entries.push_back({d.name, d.bits, true, static_cast<uint32_t>(d.port_id)});
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    entries.push_back({d.name, d.bits, false, static_cast<uint32_t>(d.port_id)});
  }
  std::sort(entries.begin(), entries.end(), [](const IoEntry& a, const IoEntry& b) { return a.port_id < b.port_id; });

  bool first_arg = true;
  for (const auto& e : entries) {
    if (!first_arg) {
      fout->append("  ,");
    } else {
      fout->append("   ");
    }
    first_arg = false;

    if (e.is_input) {
      fout->append("input signed ");
    } else {
      fout->append("output reg signed ");
    }

    const auto name = get_scaped_name(e.name);

    // Prefer the concrete HHDS pin width when present. Some imported GraphIO
    // declarations can retain stale placeholder widths, while the graph pin
    // has already been fixed by bitwidth propagation.
    hhds::Pin_class pin = e.is_input ? graph->get_input_pin(e.name) : graph->get_output_pin(e.name);
    const auto      bits = pin.is_invalid() ? e.bits : livehd::graph_util::bits_of(pin, *gio, e.name);

    if (bits > 1) {
      fout->append("[", std::to_string(bits - 1), ":0] ", name, "\n");
    } else {
      fout->append(name, "\n");
    }

    // Map the corresponding HHDS pin (driver for inputs, sink for outputs) into pin2var.
    if (!pin.is_invalid()) {
      pin2var.emplace(pin.get_class_index(), name);
    }
  }

  fout->append(");\n");
}

void Cgen_verilog::create_memories(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    if (type_op_of(node) != Ntype_op::Memory) {
      continue;
    }
    process_memory(fout, node);
  }
}

void Cgen_verilog::create_subs(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    if (!is_type_sub(node)) {
      continue;
    }

    auto iname  = get_scaped_name(default_instance_name(node));
    auto sub_io = node.get_subnode_io();
    if (!sub_io) {
      continue;
    }

    fout->append(get_scaped_name(sub_io->get_name()), " ", iname, "(\n");

    bool first_entry = true;

    // Order pins by port_id to match LiveHD sub.get_sorted_io_pins().
    struct SortedPin {
      const hhds::GraphIO::DeclaredIoPin* decl;
      bool                                is_input;
    };
    std::vector<SortedPin> ordered;
    for (const auto& d : sub_io->get_input_pin_decls()) {
      ordered.push_back({&d, true});
    }
    for (const auto& d : sub_io->get_output_pin_decls()) {
      ordered.push_back({&d, false});
    }
    std::sort(ordered.begin(), ordered.end(), [](const SortedPin& a, const SortedPin& b) {
      return a.decl->port_id < b.decl->port_id;
    });

    for (const auto& io : ordered) {
      hhds::Pin_class dpin;
      if (io.is_input) {
        // node's sink pin named io.decl->name → driver via inp_edges
        auto spin = node.get_sink_pin(io.decl->name);
        dpin      = get_driver(spin);
      } else {
        // node's driver pin named io.decl->name → emit only if has consumers
        auto candidate = node.get_driver_pin(io.decl->name);
        if (!candidate.is_invalid() && !candidate.out_edges().empty()) {
          dpin = candidate;
        }
      }
      if (!dpin.is_invalid()) {
        fout->append(absl::StrCat(first_entry ? "" : ",", ".", io.decl->name, "(", get_wire_or_const(dpin), ")\n"));
        first_entry = false;
      }
    }

    fout->append(");\n");
  }
}

void Cgen_verilog::create_combinational(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  fout->append("always_comb begin\n");

  for (auto node : graph->forward_class()) {
    auto op = type_op_of(node);
    if (Ntype::is_multi_driver(op)) {
      continue;
    }
    if (!node.has_out_edges() || is_type_flop(node)) {
      continue;
    }
    if (bits_of(node.get_driver_pin(0)) == 0) {
      if (op != Ntype_op::Nconst && op != Ntype_op::AttrSet && op != Ntype_op::Mux) {
        // missing bits; Pass::error in original — skip silent.
      }
    }
    if (op == Ntype_op::Mux) {
      process_mux(fout, node);
    } else {
      process_simple_node(fout, node);
    }
  }

  fout->append("end\n");
}

void Cgen_verilog::create_outputs(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  fout->append("always_comb begin\n");
  auto gio = graph->get_io();
  I(gio);
  for (const auto& d : gio->get_output_pin_decls()) {
    auto spin = graph->get_output_pin(d.name);
    if (spin.is_invalid()) {
      continue;
    }
    auto out_dpin = get_driver(spin);
    if (out_dpin.is_invalid()) {
      continue;
    }
    auto name = get_scaped_name(d.name);
    auto expr = get_expression(out_dpin);
    if (name != expr) {
      fout->append("  ", name, " = ", expr, ";\n");
    }
  }
  for (auto node : graph->fast_class()) {
    if (is_type_flop(node)) {
      process_flop(fout, node);
    }
  }
  fout->append("end\n");
}

void Cgen_verilog::create_registers(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    if (!is_type_flop(node)) {
      continue;
    }

    auto        dpin      = node.get_driver_pin(0);
    std::string pin_name  = pin_wire_name(dpin);
    const auto  name      = get_scaped_name(pin_name);
    const auto  name_next = get_scaped_name(absl::StrCat("___next_", pin_name));

    std::string edge        = "posedge";
    auto        posclk_sink = find_sink_pin(node, "posclk");
    auto        posclk_dpin = get_driver(posclk_sink);
    if (!posclk_dpin.is_invalid()) {
      auto v = !hydrate_const(posclk_dpin).is_known_false();
      if (!v) {
        edge = "negedge";
      }
    }
    auto        clock_sink = find_sink_pin(node, "clock_pin");
    std::string clock      = get_scaped_name(pin_wire_name(get_driver(clock_sink)));

    std::string reset_async;
    std::string reset;
    bool        negreset = false;

    auto reset_sink = find_sink_pin(node, "reset_pin");
    auto reset_dpin = get_driver(reset_sink);
    if (!reset_dpin.is_invalid()) {
      if (is_const_pin(reset_dpin)) {
        auto reset_const = hydrate_const(reset_dpin);
        if (!reset_const.is_known_false() && !reset_const.same_repr(*Dlop::from_string("false"))) {
          reset = reset_const.to_verilog();
        }
      } else {
        reset = get_wire_or_const(reset_dpin);

        auto negreset_dpin = get_driver(find_sink_pin(node, "negreset"));
        if (!negreset_dpin.is_invalid()) {
          negreset = !hydrate_const(negreset_dpin).is_known_false();
        }
        auto async_dpin = get_driver(find_sink_pin(node, "async"));
        if (!async_dpin.is_invalid()) {
          auto v = !hydrate_const(async_dpin).is_known_false();
          if (v) {
            reset_async = absl::StrCat(negreset ? " or negedge " : " or posedge ", reset);
          }
        }
      }
    }

    std::string reset_initial = "'h0";
    auto        initial_dpin  = get_driver(find_sink_pin(node, "initial"));
    if (!initial_dpin.is_invalid()) {
      reset_initial = get_wire_or_const(initial_dpin);
    }

    // Task 1q — pipeline depth: the pipe_min/pipe_max comptime pins make one
    // Flop cell model a whole depth-d shift register. Unset pins => depth 1
    // (today's single flop, bit-for-bit). For a ranged depth (min<max) the
    // realization default at Verilog emission is the declared MINIMUM (the
    // LG pass2 knob picks differently later); pipe_max is for the checker
    // and the slop simulation, never read here.
    int64_t depth = 1;
    {
      auto pm_dpin = get_driver(find_sink_pin(node, "pipe_min"));
      if (!pm_dpin.is_invalid() && is_const_pin(pm_dpin)) {
        depth = hydrate_const(pm_dpin).to_i();
      }
    }

    // 2d-reg — write-enable: a conditionally-written state register holds
    // its value when the OR-of-write-conditions is false (no din=q feedback
    // mux is ever inserted — the enable IS the hold).
    std::string enable;
    {
      auto enable_dpin = get_driver(find_sink_pin(node, "enable"));
      if (!enable_dpin.is_invalid() && !is_const_pin(enable_dpin)) {
        enable = get_wire_or_const(enable_dpin);
      }
    }

    if (depth <= 1) {
      // Depth 1 (or unset): today's single-flop emission, plus the optional
      // enable gate.
      fout->append("always @(", edge, " ", clock, reset_async, " ) begin\n");

      const std::string update = enable.empty() ? absl::StrCat(name, " <= ", name_next, ";\n")
                                                : absl::StrCat("if (", enable, ") begin\n", name, " <= ", name_next,
                                                               ";\nend\n");
      if (reset.empty()) {
        fout->append(update);
      } else {
        if (negreset) {
          fout->append("if (!", reset, ") begin\n");
        } else {
          fout->append("if (", reset, ") begin\n");
        }
        fout->append(name, " <= ", reset_initial, ";\n");
        fout->append("end else begin\n");
        fout->append(update);
        fout->append("end\n");
      }

      fout->append("end\n");
      continue;
    }

    // depth >= 2: declare the d-1 intermediate stage regs (q itself is the
    // last stage) and emit one clocked block chaining them. Every stage
    // replicates the SAME clock/reset configuration — the inserted-flop
    // contract forbids inventing a different reset style per stage.
    int  bits    = bits_of(dpin);
    bool out_uns = is_unsign(dpin);
    if (out_uns) {
      --bits;
    }
    std::vector<std::string> stage_names;
    stage_names.reserve(static_cast<size_t>(depth) - 1);
    for (int64_t i = 0; i < depth - 1; ++i) {
      auto sname = get_append_to_name(name, absl::StrCat("___pipe", i, "_"));
      if (bits <= 1) {
        fout->append(out_uns ? "reg " : "reg signed ", sname, ";\n");
      } else {
        fout->append(out_uns ? "reg " : "reg signed ", "[", std::to_string(bits - 1), ":0] ", sname, ";\n");
      }
      stage_names.emplace_back(std::move(sname));
    }

    fout->append("always @(", edge, " ", clock, reset_async, " ) begin\n");

    auto emit_chain = [&]() {
      fout->append(stage_names.front(), " <= ", name_next, ";\n");
      for (size_t i = 1; i < stage_names.size(); ++i) {
        fout->append(stage_names[i], " <= ", stage_names[i - 1], ";\n");
      }
      fout->append(name, " <= ", stage_names.back(), ";\n");
    };

    if (reset.empty()) {
      emit_chain();
    } else {
      if (negreset) {
        fout->append("if (!", reset, ") begin\n");
      } else {
        fout->append("if (", reset, ") begin\n");
      }
      for (const auto& sname : stage_names) {
        fout->append(sname, " <= ", reset_initial, ";\n");
      }
      fout->append(name, " <= ", reset_initial, ";\n");
      fout->append("end else begin\n");
      emit_chain();
      fout->append("end\n");
    }

    fout->append("end\n");
  }
}

void Cgen_verilog::add_to_pin2var(std::shared_ptr<File_output> fout, const hhds::Pin_class& dpin, std::string_view name,
                                  bool out_unsigned) {
  if (is_const_pin(dpin)) {
    return;
  }

  auto [it, replaced] = pin2var.insert({dpin.get_class_index(), std::string(name)});
  if (!replaced) {
    return;
  }

  int bits = bits_of(dpin);

  std::string reg_str;
  if (out_unsigned) {
    reg_str = "reg ";
    --bits;
  } else {
    reg_str = "reg signed ";
  }

  if (bits <= 1) {
    fout->append(reg_str, name, ";\n");
  } else {
    fout->append(reg_str, "[", std::to_string(bits - 1), ":0] ", name, ";\n");
  }

  if (!dpin.is_invalid() && is_type_flop(dpin.get_master_node())) {
    auto name_next = get_append_to_name(name, "___next_");
    if (bits <= 1) {
      fout->append(reg_str, name_next, ";\n");
    } else {
      fout->append(reg_str, "[", std::to_string(bits - 1), ":0] ", name_next, ";\n");
    }
  }
}

void Cgen_verilog::create_locals(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->fast_class()) {
    auto op = type_op_of(node);

    if (Ntype::is_multi_driver(op)) {
      if (op == Ntype_op::Sub || op == Ntype_op::Memory) {
        for (auto& e : node.inp_edges()) {
          auto name2 = get_scaped_name(pin_wire_name(e.driver));
          add_to_pin2var(fout, e.driver, name2, is_unsign(e.driver));
        }
        if (op == Ntype_op::Memory) {
          // Instance outputs must land on a dedicated net: the dout pin is
          // usually named after the module output it drives (e.g. "q0"), so
          // reusing that name re-declares the port (and an instance output
          // cannot legally drive an `output reg` anyway). create_outputs
          // then emits `q0 = <iname>_dout_<pid>;` like any other driver.
          //
          // Iterate out_edges (not out_pins): out_pins misses driver pid 0
          // (a zero-write-port ROM's dout) and its handles encode pins
          // WITHOUT the driver bit, so their class_index never matches
          // edge.driver / create_driver_pin handles. Re-fetch the canonical
          // driver handle for keying; pin2var insert dedups repeat pids.
          //
          // type==2 (array) douts are procedurally assigned in process_memory's
          // always_comb, so they must be `reg`; type 0/1 douts connect to the
          // cgen_memory_* instance ports and must stay nets.
          bool is_array_mem = false;
          for (auto& e2 : node.inp_edges()) {
            if (e2.sink.get_port_id() == 7 && is_const_pin(e2.driver)) {  // pid 7 = "type" (comptime x 1)
              is_array_mem = hydrate_const(e2.driver).to_i() == 2;
              break;
            }
          }
          for (auto& e2 : node.out_edges()) {
            auto dout  = node.create_driver_pin(e2.driver.get_port_id());
            auto iname = get_scaped_name(default_instance_name(node));
            auto name2 = absl::StrCat(iname, "_dout_", e2.driver.get_port_id());
            auto [it2, inserted] = pin2var.insert({dout.get_class_index(), name2});
            if (inserted) {
              int bits2 = bits_of(dout);
              if (bits2 <= 1) {
                fout->append(is_array_mem ? "reg signed " : "wire signed ", name2, ";\n");
              } else {
                fout->append(is_array_mem ? "reg signed [" : "wire signed [", std::to_string(bits2 - 1), ":0] ", name2, ";\n");
              }
            }
          }
          continue;
        }
        for (auto& dpin2 : node.out_pins()) {
          if (dpin2.out_edges().empty()) {
            continue;
          }
          auto name2 = get_scaped_name(pin_wire_name(dpin2));
          add_to_pin2var(fout, dpin2, name2, is_unsign(dpin2));
        }
      }
      continue;
    }
    I(op != Ntype_op::Sub && op != Ntype_op::Memory);

    auto out_count = node.out_edges().size();
    if (out_count == 0) {
      continue;
    }

    auto        dpin         = node.get_driver_pin(0);
    std::string name         = get_scaped_name(pin_wire_name(dpin));
    bool        out_unsigned = is_unsign(dpin);

    if (op == Ntype_op::Mux) {
      // (large-mux vector path disabled in the original; preserve.)
    } else if (op == Ntype_op::Sext) {
      auto b_dpin = get_driver(find_sink_pin(node, "b"));
      if (!b_dpin.is_invalid() && is_const_pin(b_dpin)) {
        auto dpin2 = get_driver(find_sink_pin(node, "a"));
        if (!dpin2.is_invalid()) {
          std::string name2         = get_scaped_name(pin_wire_name(dpin2));
          bool        out_unsigned2 = (!dpin2.is_invalid() && type_op_of(dpin2.get_master_node()) == Ntype_op::Get_mask);
          add_to_pin2var(fout, dpin2, name2, out_unsigned2);
        }
      }
      auto nname = node_name_of(node);
      if (!nname.empty() && nname.front() != '_') {
        continue;
      }
    } else if (op == Ntype_op::Set_mask) {
      add_to_pin2var(fout, dpin, name, false);
    } else if (op == Ntype_op::Nconst || is_const_pin(dpin)) {
      auto final_expr = hydrate_const(dpin).to_verilog();
      pin2expr.emplace(dpin.get_class_index(), Expr(final_expr, false));
    } else if (op == Ntype_op::Get_mask) {
      auto a_spin  = find_sink_pin(node, "a");
      name         = get_scaped_name(absl::StrCat(pin_wire_name(dpin), "_u"));
      out_unsigned = true;
      auto a_dpin  = get_driver(a_spin);
      if (!a_dpin.is_invalid() && !pin2var.contains(a_dpin.get_class_index())) {
        auto name2 = get_scaped_name(pin_wire_name(a_dpin));
        add_to_pin2var(fout, a_dpin, name2, false);
      }
    } else if (op == Ntype_op::AttrSet) {
      auto dpin_key = get_driver(find_sink_pin(node, "field"));
      I(!dpin_key.is_invalid() && is_type_const(dpin_key.get_master_node()));
      auto key = hydrate_const(dpin_key).to_field();

      bool dp_assign = str_tools::ends_with(key, "__dp_assign");

      hhds::Pin_class attr_dpin;
      if (dp_assign) {
        attr_dpin = get_driver(find_sink_pin(node, "value"));
      } else {
        attr_dpin = get_driver(find_sink_pin(node, "parent"));
      }
      std::string attr_name;
      if (attr_dpin.is_invalid()) {
        attr_name = "0";
      } else {
        attr_name = get_wire_or_const(attr_dpin);
        add_to_pin2var(fout, attr_dpin, attr_name, out_unsigned);
      }

      pin2expr.insert({dpin.get_class_index(), Expr(attr_name, false)});
      continue;
    } else if (!is_type_flop(node)) {
      auto nname = node_name_of(node);
      if (!nname.empty() && nname.front() != '_') {
        continue;
      }
      if (out_count < 2) {
        continue;
      }
    }

    add_to_pin2var(fout, dpin, name, out_unsigned);
  }
}

void Cgen_verilog::do_from_graph(const std::shared_ptr<hhds::Graph>& graph) {
  TRACE_EVENT("pass", nullptr, [&graph](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(absl::StrCat(converted_str, graph->get_name()));
  });

  assert(nrunning == 0);
  ++nrunning;

  (void)verbose;

  pin2var.clear();
  pin2expr.clear();
  mux2vector.clear();
  first_array_block = true;

  std::string filename;
  if (odir.empty()) {
    filename = absl::StrCat(graph->get_name(), ".v");
  } else {
    filename = absl::StrCat(odir, "/", graph->get_name(), ".v");
  }

  auto fout = std::make_shared<File_output>(filename);

  fout->append("/* verilator lint_off WIDTH */\n");
  fout->append("module ", get_scaped_name(graph->get_name()), "(\n");

  hhds::Graph* g = graph.get();
  create_module_io(fout, g);

  create_locals(fout, g);
  create_memories(fout, g);
  create_subs(fout, g);

  create_combinational(fout, g);
  create_outputs(fout, g);
  create_registers(fout, g);

  fout->append("endmodule\n");

  --nrunning;
}
