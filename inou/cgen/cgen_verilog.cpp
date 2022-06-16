// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cell.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"

Cgen_verilog::Cgen_verilog(bool _verbose, std::string_view _odir) : verbose(_verbose), odir(_odir), nrunning(0) {
  if (reserved_keyword.empty()) {
    std::lock_guard<std::mutex> guard(lgs_mutex);

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
  }
}

std::string Cgen_verilog::get_wire_or_const(const Node_pin &dpin) const {
  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it != pin2var.end())
    return var_it->second;

  if (dpin.is_type_const())
    return dpin.get_type_const().to_verilog();

  return get_scaped_name(dpin.get_wire_name());
}

std::string Cgen_verilog::get_scaped_name(std::string_view name) {  // follow verilog name and add \ when needed

  std::string res_name;
  if (name.front() == '%') {
    if (name[1] == '.') {
      res_name = name.substr(2);
    } else {
      res_name = name.substr(1);
    }
  } else if (reserved_keyword.contains(name)) {
    return absl::StrCat("\\", name, " ");
  } else {
    res_name = name;
  }

  for (auto i = 0u; i < res_name.size(); ++i) {
    auto ch = res_name[i];
    if (!std::isalnum(ch) && ch != '_')
      return absl::StrCat("\\", res_name, " ");
  }

  return res_name;
}

std::string Cgen_verilog::get_append_to_name(std::string_view name, std::string_view ext) const {
  if (name.front() == '\\') {
    return absl::StrCat("\\", ext, name.substr(1, name.size() - 1), " ");
  }

  return absl::StrCat(ext, name);
}

std::string Cgen_verilog::get_expression(const Node_pin &dpin) const {
  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it != pin2var.end())
    return var_it->second;

  const auto expr_it = pin2expr.find(dpin.get_compact_class());
  I(expr_it != pin2expr.end());
  if (expr_it->second.needs_parenthesis) {
    return absl::StrCat("(", expr_it->second.var, ")");
  }

  return expr_it->second.var;
}

std::string Cgen_verilog::add_expression(std::string_view txt_seq, std::string_view txt_op, Node_pin &dpin) const {
  auto expr = get_expression(dpin);

  if (txt_seq.empty())
    return expr;

  return absl::StrCat(txt_seq, " ", txt_op, " ", expr);
}

void Cgen_verilog::process_flop(std::shared_ptr<File_output> fout, Node &node) {
  auto dpin_d = node.get_sink_pin("din").get_driver_pin();
  auto dpin_q = node.get_driver_pin();

  auto       pin_name  = dpin_q.get_wire_name();
  const auto name_next = get_scaped_name(absl::StrCat("___next_", pin_name));

  if (dpin_d.is_invalid()) {
    fout->append("  ", name_next, " = 'hx; // disconnected flop\n");
  } else {
    fout->append("  ", name_next, " = ", get_expression(dpin_d), ";\n");
  }
}

void Cgen_verilog::process_memory(std::shared_ptr<File_output> fout, Node &node) {
  auto iname = get_scaped_name(node.default_instance_name());

  int n_rd_ports = 0;
  int n_wr_ports = 0;

  // WARNING: The read_vector must be computed first, to have know if the
  // addr/din/... are read/writes ahead (traverse out of order)

  struct Port_field {
    bool     rdport;
    Node_pin enable;
    Node_pin addr;
    Node_pin clock;
    Node_pin din;  // only for write port
  };
  std::vector<Port_field> port_vector;  // first == read, second == port_id

  int mem_size    = 0;
  int mem_bits    = 0;
  int mem_fwd     = 0;
  int mem_type    = 2;  // array by default
  int mem_wensize = 0;

  for (auto e : node.inp_edges()) {
    auto pin_name = e.sink.get_pin_name();

    size_t port_id = e.sink.get_pid() / 11;

    if (port_vector.size() <= port_id)
      port_vector.resize(1 + port_id);

    if (pin_name == "bits") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for bits not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      mem_bits = e.driver.get_type_const().to_i();
    } else if (pin_name == "size") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for size not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      mem_size = e.driver.get_type_const().to_i();
    } else if (pin_name == "type") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant type not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      mem_type = e.driver.get_type_const().to_i();
    } else if (pin_name == "wensize") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for wensize not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      mem_wensize = e.driver.get_type_const().to_i();
    } else if (pin_name == "fwd") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for fwd not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      mem_fwd = e.driver.get_type_const().to_i();
    } else if (str_tools::ends_with(pin_name, "clock")) {
      port_vector[port_id].clock = e.driver;
    } else if (str_tools::ends_with(pin_name, "addr")) {
      port_vector[port_id].addr = e.driver;
    } else if (str_tools::ends_with(pin_name, "enable")) {
      port_vector[port_id].enable = e.driver;
    } else if (str_tools::ends_with(pin_name, "din")) {
      port_vector[port_id].din = e.driver;
    } else if (str_tools::ends_with(pin_name, "rdport")) {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant rdport not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      bool rdport                 = !e.driver.get_type_const().is_known_false();
      port_vector[port_id].rdport = rdport;

      if (rdport) {
        ++n_rd_ports;
      } else {
        ++n_wr_ports;
      }
    }
  }

  if (mem_type == 0 || mem_type == 1) {  // sync or async memory
    bool     single_clock    = true;
    Node_pin base_clock_dpin = port_vector[0].clock;
    {
      for (auto &p : port_vector) {
        auto &dpin = p.clock;
        if (dpin.is_invalid()) {
          dpin = base_clock_dpin;
          continue;
        }
        if (dpin != base_clock_dpin)
          single_clock = false;
      }
    }

    if (base_clock_dpin.is_invalid()) {
      Pass::error("memory {} should have a clock pin", node.debug_name());
      return;
    }
    // create name
    fout->append(absl::StrCat("cgen_memory_", single_clock ? "" : "multiclock_"));
    fout->append(absl::StrCat(n_rd_ports, "rd_"));
    fout->append(absl::StrCat(n_wr_ports, "wr "));

    // parameters
    std::string parameters;
    bool        first_entry = true;

    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".LATENCY_0(", mem_type, ")");
    first_entry = false;
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".BITS(", mem_bits, ")");
    first_entry = false;
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".SIZE(", mem_size, ")");
    first_entry = false;
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".WENSIZE", "(", mem_wensize, ")");
    first_entry = false;
    parameters  = absl::StrCat(parameters, first_entry ? "" : " ,", ".FWD", "(", mem_fwd, ")");
    first_entry = false;
    fout->append(" #(", parameters, ") ");

    // instance name
    fout->append(iname, "(\n");

    first_entry = true;
    if (single_clock) {
      fout->append(absl::StrCat(".clock(", get_wire_or_const(base_clock_dpin), ")\n"));
      first_entry = false;
    }

    {
      auto n_rd_pos = 0;
      auto n_wr_pos = 0;
      auto n_pos    = 0;
      for (auto &p : port_vector) {
        if (p.rdport) {
          if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid()) {
            node.dump();
            Pass::error("memory {} read port is not correctly configured\n", node.debug_name());
          }
          fout->append(absl::StrCat(first_entry ? "  .rd_addr_" : "  ,.rd_addr_", n_rd_pos, "(", get_wire_or_const(p.addr), ")\n"));
          first_entry = false;

          fout->append("  ,.rd_enable_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.enable), ")\n");
          if (!single_clock) {
            fout->append("  ,.rd_clock_", std::to_string(n_rd_pos), "(", get_wire_or_const(p.clock), ")\n");
          }
          auto dout_dpin = node.setup_driver_pin_raw(n_pos);  // rd data out
          fout->append("  ,.rd_dout_", std::to_string(n_rd_pos), "(", get_wire_or_const(dout_dpin), ")\n");
          ++n_rd_pos;
        } else {
          if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid() || p.din.is_invalid()) {
            node.dump();
            Pass::error("memory {} write port is not correctly configured\n", node.debug_name());
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
        ++n_pos;
      }
      I(n_rd_pos == n_rd_ports);
      I(n_wr_pos == n_wr_ports);
    }

    fout->append(");\n");
  } else {  // array
    fout->append(absl::StrCat("reg [", mem_bits - 1, ":0] ", iname, "[", mem_size - 1, ":0];\n"));

    if (first_array_block) {
      fout->append("integer mem_loop_i;\n");
      first_array_block = false;
    }

    fout->append("always_comb begin\n");

    fout->append("for (mem_loop_i=0;mem_loop_i < ", std::to_string(mem_size), ";mem_loop_i = mem_loop_i + 1) begin\n");
    fout->append(iname, "[mem_loop_i] = 'b0;\n");
    fout->append("end\n");

    {
      // ARRAY has forwarding, so writes first
      for (auto &p : port_vector) {
        if (p.rdport)
          continue;

        if (p.addr.is_invalid() || p.din.is_invalid()) {
          node.dump();
          Pass::error("memory {} write port is not correctly configured\n", node.debug_name());
        }

        auto din_name = get_wire_or_const(p.din);

        auto write_stmt = absl::StrCat(iname, "[", get_wire_or_const(p.addr), "] = ", din_name, ";\n");
        if (p.enable.is_invalid()) {
          fout->append("  ", write_stmt);
        } else {
          fout->append("  if (", get_wire_or_const(p.enable), ") begin \n");
          fout->append("    ", write_stmt);
          fout->append("end\n");
        }
      }

      auto n_pos = 0;
      for (auto &p : port_vector) {
        if (!p.rdport) {
          ++n_pos;
          continue;
        }

        if (p.addr.is_invalid()) {
          node.dump();
          Pass::error("array {} read port is not correctly configured\n", node.debug_name());
        }
        auto dout_dpin = node.setup_driver_pin_raw(n_pos);  // rd data out
        auto dest_name = get_wire_or_const(dout_dpin);

        auto read_stmt = absl::StrCat(dest_name, " = ", iname, "[", get_wire_or_const(p.addr), "];\n");
        if (p.enable.is_invalid()) {
          fout->append("  ", read_stmt);
        } else {
          fout->append("  if (", get_wire_or_const(p.enable), ") begin \n");
          fout->append("    ", read_stmt);
          fout->append("end\n");
        }
        ++n_pos;
      }
    }

    fout->append("end\n");
  }
}

void Cgen_verilog::process_mux(std::shared_ptr<File_output> fout, Node &node) {
  auto ordered_inp = node.inp_edges_ordered();
  I(ordered_inp.size() > 2);  // at least 0 + 1 + 2

  auto sel_expr    = get_expression(ordered_inp[0].driver);
  auto dest_var_it = pin2var.find(node.get_driver_pin().get_compact_class());
  I(dest_var_it != pin2var.end());
  auto dest_var = dest_var_it->second;

  auto mux2vec_it = mux2vector.find(node.get_compact_class());
  if (mux2vec_it == mux2vector.end()) {
    if (ordered_inp.size() == 3) {  // if-else case
      fout->append("   if (", sel_expr, ") begin\n");
      fout->append("     ", dest_var, " = ", get_expression(ordered_inp[2].driver), ";\n");
      fout->append("   end else begin\n");
      fout->append("     ", dest_var, " = ", get_expression(ordered_inp[1].driver), ";\n");
      fout->append("   end\n");
    } else {
      fout->append("   case (", sel_expr, ")\n");
      auto sel_bits = ordered_inp[0].driver.get_bits();
      for (auto i = 1u; i < ordered_inp.size(); ++i) {
        fout->append("     ", std::to_string(sel_bits), "'d", std::to_string(i - 1));
        fout->append(" : ", dest_var, " = ", get_expression(ordered_inp[i].driver), ";\n");
      }
      size_t num_cases = 1 << (sel_bits);
      if (num_cases > ordered_inp.size() - 1) {
        fout->append("       default: ", dest_var, " = 'hx;\n");
      }
      fout->append("   endcase\n");
    }
  }
}

void Cgen_verilog::process_simple_node(std::shared_ptr<File_output> fout, Node &node) {
  auto dpin = node.get_driver_pin();
  auto op   = node.get_type_op();
  I(!Ntype::is_multi_driver(op));

  std::string final_expr;

  if (op == Ntype_op::Sum) {
    std::string add_seq;
    std::string sub_seq;
    for (auto e : node.inp_edges()) {
      if (e.sink.get_pid() == 0) {
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
    auto lhs   = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto rhs   = get_expression(node.get_sink_pin("b").get_driver_pin());
    final_expr = absl::StrCat(lhs, "/", rhs);

  } else if (op == Ntype_op::Not) {
    auto lhs   = get_expression(node.get_sink_pin("a").get_driver_pin());
    final_expr = absl::StrCat("~", lhs);

  } else if (op == Ntype_op::Set_mask) {
    auto a_dpin = node.get_sink_pin("a").get_driver_pin();
    auto a      = get_expression(a_dpin);

    auto mask_dpin = node.get_sink_pin("mask").get_driver_pin();
    I(mask_dpin.is_type_const());  // Do we want to support this? (not easy)

    auto mask_v = mask_dpin.get_type_const();
    I(!mask_v.has_unknowns());

    if (mask_v == Lconst(0)) {  // nothing to do
      final_expr = a;
    } else {
      auto [range_begin, range_end] = mask_v.get_mask_range();
      if (range_end > static_cast<int>(dpin.get_bits()))
        range_end = dpin.get_bits() + range_begin;

      auto a_bits = a_dpin.get_bits();

      auto value_dpin = node.get_sink_pin("value").get_driver_pin();
      auto value      = get_expression(value_dpin);

      // fmt::print("a_bits:{} mask:{} minr:{} maxr:{}\n", a_bits, mask_v.to_pyrope(), range_begin, range_end);

      if (range_begin > static_cast<int>(a_bits)) {
        final_expr = a;
      } else if (range_begin < 0 || range_end < 0) {  // no continuous range
        std::string sel;
        for (auto i = 0; i < a_bits; ++i) {
          if (mask_v.and_op(Lconst(1) << i).is_known_false()) {  // use a
            if (sel.empty())
              sel = absl::StrCat(a, "[", i, "]");
            else
              sel = absl::StrCat(sel, ",", a, "[", i, "]");
          } else {  // use value
            if (sel.empty())
              sel = absl::StrCat(value, "[", i, "]");
            else
              sel = absl::StrCat(sel, ",", value, "[", i, "]");
          }
        }
        final_expr = absl::StrCat("{", sel, "}");
      } else {
        std::string a_replaced;
        Bits_t      value_bits_to_use = static_cast<Bits_t>(range_end - range_begin);
        if (value_bits_to_use >= value_dpin.get_bits()) {
          a_replaced = value;
        } else if (value_bits_to_use == 1) {
          a_replaced = absl::StrCat(value, "[0]");
        } else {
          a_replaced = absl::StrCat(value, "[", value_bits_to_use - 1, ":0]");
        }

        auto var_it = pin2var.find(dpin.get_compact_class());
        assert(var_it != pin2var.end());
        if (value_bits_to_use < dpin.get_bits()) {
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
        return;  // special case, multiple statements

#if 0
        std::string a_high;
        std::string a_low;

        Lconst a_val; // only if const
        bool a_is_const = a_dpin.is_type_const();
        if (a_is_const) {
          a_val = a_dpin.get_type_const();
        }

        if (range_end< static_cast<int>(a_bits)) {
          if (a_is_const) {
            // auto v = a_val.get_mask_op(Lconst::get_mask_value(a_bits, range_end));
            auto v = a_val.rsh_op(range_end);
            a_high = absl::StrCat(v.to_verilog(),",");
          }else if (static_cast<int>(a_bits)==(range_end+1)) {
            a_high = absl::StrCat(a, "[", range_end, "],");
          }else{
            a_high = absl::StrCat(a, "[", a_bits-1, ":", range_end, "],");
          }
        }

        if (range_begin>0) {
          if (a_is_const) {
            auto v = a_val.get_mask_op(Lconst::get_mask_value(range_begin));
            a_low = absl::StrCat(",", v.to_verilog());
          }else if (range_begin==1) {
            a_low = absl::StrCat(",", a, "[0]");
          }else{
            a_low = absl::StrCat(",", a, "[", range_begin-1, ":0]");
          }
        }

        final_expr = absl::StrCat("{", a_high, a_replaced, a_low, "}");
#endif
      }
    }
  } else if (op == Ntype_op::Get_mask) {
    auto mask_dpin = node.get_sink_pin("mask").get_driver_pin();
    I(mask_dpin.is_type_const());

    auto mask_v = mask_dpin.get_type_const();
    I(!mask_v.has_unknowns());

    auto a_dpin = node.get_sink_pin("a").get_driver_pin();
    auto a_bits = a_dpin.get_bits();
    auto a      = get_expression(a_dpin);

    auto [range_begin, range_end] = mask_v.get_mask_range();
    Bits_t a_bits_to_use          = static_cast<Bits_t>(range_end - range_begin);
    if (a_bits_to_use > dpin.get_bits())
      range_end = dpin.get_bits() + range_begin;

    int out_bits = dpin.get_bits();
    if (dpin.is_unsign())
      --out_bits;

    if (range_begin < 0 || range_end < 0) {
      std::string sel;
      auto        max_bits = std::max(mask_v.get_bits(), a_bits);
      for (auto i = 0; i < max_bits; ++i) {
        if (mask_v.and_op(Lconst(1) << i).is_known_false())
          continue;

        if (sel.empty())
          sel = absl::StrCat(a, "[", i, "]");
        else
          sel = absl::StrCat(sel, ",", a, "[", i, "]");
      }
      final_expr = absl::StrCat("{", sel, "}");
    } else if (range_begin >= static_cast<int>(a_bits)) {
      final_expr = absl::StrCat("{", range_end - range_begin, "{", a, "[", a_bits - 1, "]}}");
    } else if (range_end > static_cast<int>(a_bits)) {
      auto top   = absl::StrCat("{{", range_end - a_bits, "{", a, "[", a_bits - 1, "]}}");
      final_expr = absl::StrCat(top, ",", a, "[", a_bits - 1, ":", range_begin, "]}");
    } else if (range_begin == 0 && range_end >= out_bits) {
      final_expr = a;
    } else if (a_bits_to_use == 1) {
      final_expr = absl::StrCat(a, "[", range_begin, "]");
    } else {
      final_expr = absl::StrCat(a, "[", range_end - 1, ":", range_begin, "]");
    }

  } else if (op == Ntype_op::Sext) {
    auto lhs      = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto pos_node = node.get_sink_pin("b").get_driver_node();
    if (pos_node.is_type_const()) {
      auto lpos = pos_node.get_type_const();
      if (lpos.is_i()) {
        final_expr = absl::StrCat(lhs, "[", lpos.to_i() - 1, ":0]");
      }
    }
    if (final_expr.empty()) {
      auto bits     = node.get_sink_pin("b").get_driver_pin().get_bits();
      auto pos_expr = get_expression(node.get_sink_pin("b").get_driver_pin());

      final_expr = absl::StrCat(lhs, "& ((1'sh", bits, " << ", pos_expr, ")-1)");
    }
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    std::vector<std::string> lhs;
    std::vector<std::string> rhs;
    for (const auto &e : node.inp_edges()) {
      if (e.sink.get_pin_name() == "A") {
        lhs.emplace_back(get_expression(e.driver));
      } else {
        rhs.emplace_back(get_expression(e.driver));
      }
    }
    std::string cmp;
    if (op == Ntype_op::GT) {
      cmp = " > ";
    } else {
      cmp = " < ";
    }
    for (const auto &l : lhs) {
      for (const auto &r : rhs) {
        if (final_expr.empty()) {
          final_expr = absl::StrCat(l, cmp, r);
        } else {
          final_expr = absl::StrCat(final_expr, " && ", l, cmp, r);
        }
      }
    }
  } else if (op == Ntype_op::SHL) {
    auto        val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    std::string onehot;
    bool        first = true;
    for (auto &amt_dpin : node.get_sink_pin("B").inp_drivers()) {
      auto amt_expr = get_expression(amt_dpin);
      onehot        = absl::StrCat(onehot, first ? "(" : " | (", val_expr, " << ", amt_expr, ")");
      first         = false;
    }
    final_expr = onehot;
  } else if (op == Ntype_op::SRA) {
    auto val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto amt_expr = get_expression(node.get_sink_pin("b").get_driver_pin());

    // TODO: If val_expr min>=0, then >> is OK
    final_expr = absl::StrCat(val_expr, " >>> ", amt_expr);
  } else if (op == Ntype_op::Const) {
    // final_expr = node.get_type_const().to_verilog();
    return;  // Done before at create_locals
  } else if (op == Ntype_op::TupAdd || op == Ntype_op::TupGet || op == Ntype_op::AttrSet || op == Ntype_op::AttrGet) {
    node.dump();
    Pass::error("could not generate verilog unless it is low level Lgraph node:{} is type {}\n",
                node.debug_name(),
                Ntype::get_name(op));
    return;
  } else {
    std::string txt_op;
    if (op == Ntype_op::Mult)
      txt_op = "*";
    else if (op == Ntype_op::And)
      txt_op = "&";
    else if (op == Ntype_op::Or)
      txt_op = "|";
    else if (op == Ntype_op::Xor)
      txt_op = "^";
    else if (op == Ntype_op::EQ)
      txt_op = "==";

    I(!txt_op.empty());

    for (auto e : node.inp_edges()) {
      final_expr = add_expression(final_expr, txt_op, e.driver);
    }
  }

  // fmt::print("node:{} expr:{}\n",node.debug_name(), final_expr);
  if (final_expr.empty()) {
    Pass::info("likely issue in node:{} that has no compute value", node.debug_name());
    final_expr = "'hx";
  }

  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it == pin2var.end()) {
    pin2expr.emplace(dpin.get_compact_class(), Expr(final_expr, true));
  } else {
    fout->append("  ", var_it->second, " = ", final_expr, ";\n");
  }
}

void Cgen_verilog::create_module_io(std::shared_ptr<File_output> fout, Lgraph *lg) {
  bool first_arg = true;
  lg->each_sorted_graph_io([&](const Node_pin &pin, Port_ID pos) {
    (void)pos;

    if (!first_arg)
      fout->append("  ,");
    else
      fout->append("   ");
    first_arg = false;

    if (pin.is_graph_input()) {
      fout->append("input signed ");
    } else {
      fout->append("output reg signed ");
    }

    const auto name = get_scaped_name(pin.get_name());
    const auto bits = pin.get_bits();

    if (bits > 1) {
      fout->append("[", std::to_string(bits - 1), ":0] ", name, "\n");
    } else {
      fout->append(name, "\n");
    }

    pin2var.emplace(pin.get_compact_class(), name);
  });

  fout->append(");\n");
}

void Cgen_verilog::create_memories(std::shared_ptr<File_output> fout, Lgraph *lg) {
  for (auto node : lg->fast()) {
    auto op = node.get_type_op();
    if (op != Ntype_op::Memory)
      continue;

    process_memory(fout, node);
  }
}

void Cgen_verilog::create_subs(std::shared_ptr<File_output> fout, Lgraph *lg) {
  lg->each_local_sub_fast([this, fout](Node &node, Lg_type_id lgid) {
    (void)lgid;

    auto        iname = get_scaped_name(node.default_instance_name());
    const auto &sub   = node.get_type_sub_node();

    fout->append(get_scaped_name(sub.get_name()), " ", iname, "(\n");

    bool first_entry = true;
    for (auto &io_pin : sub.get_sorted_io_pins()) {
      Node_pin dpin;
      if (io_pin.is_input()) {
        auto spin = node.get_sink_pin(io_pin.name);
        dpin      = spin.get_driver_pin();
      } else {
        dpin = node.get_driver_pin(io_pin.name);
        if (!dpin.is_connected())
          dpin.invalidate();
      }
      if (!dpin.is_invalid()) {
        fout->append(absl::StrCat(first_entry ? "" : ",", ".", io_pin.name, "(", get_wire_or_const(dpin), ")\n"));
        first_entry = false;
      }
    }

    fout->append(");\n");
  });
}

void Cgen_verilog::create_combinational(std::shared_ptr<File_output> fout, Lgraph *lg) {
  fout->append("always_comb begin\n");

  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    if (Ntype::is_multi_driver(op)) {
      continue;
    }

    if (!node.has_outputs() || node.is_type_flop())
      continue;

    if (node.get_driver_pin().get_bits() == 0) {
      if (!node.is_type_const()) {
        node.dump();
        Pass::error("node:{} does not have bits set. It needs bits to generate correct verilog", node.debug_name());
      }
    }

    // flops added to the last always with outputs
    if (op == Ntype_op::Mux) {
      process_mux(fout, node);
    } else {
      process_simple_node(fout, node);
    }
  }

  fout->append("end\n");
}

void Cgen_verilog::create_outputs(std::shared_ptr<File_output> fout, Lgraph *lg) {
  fout->append("always_comb begin\n");
  lg->each_graph_output([&](const Node_pin &dpin) {
    auto spin = dpin.change_to_sink_from_graph_out_driver();
    if (!spin.is_connected())
      return;

    auto name = get_scaped_name(dpin.get_name());

    auto out_dpin = spin.get_driver_pin();
    fout->append("  ", name, " = ", get_expression(out_dpin), ";\n");
  });

  for (auto node : lg->fast()) {
    if (node.is_type_flop())
      process_flop(fout, node);
  }
  fout->append("end\n");
}

void Cgen_verilog::create_registers(std::shared_ptr<File_output> fout, Lgraph *lg) {
  for (auto node : lg->fast()) {
    if (!node.is_type_flop())
      continue;

    auto dpin = node.get_driver_pin();

    std::string pin_name = dpin.get_wire_name();

    // FIXME: HERE if flop is output, do not create flop
    const auto name      = get_scaped_name(pin_name);
    const auto name_next = get_scaped_name(absl::StrCat("___next_", pin_name));

    std::string edge = "posedge";
    if (node.get_sink_pin("posclk").is_connected()) {
      auto v = node.get_sink_pin("posclk").get_driver_pin().get_type_const().to_i() != 0;
      if (!v) {
        edge = "negedge";
      }
    }
    std::string clock = get_scaped_name(node.get_sink_pin("clock").get_wire_name());

    std::string reset_async;
    std::string reset;
    bool        negreset = false;

    if (node.get_sink_pin("reset").is_connected()) {
      auto reset_dpin = node.get_sink_pin("reset").get_driver_pin();
      if (reset_dpin.is_type_const()) {
        auto reset_const = reset_dpin.get_node().get_type_const();
        if (!reset_const.is_known_false() && reset_const != Lconst::from_string("false")) {
          Pass::info("flop reset pin is hardwired to value:{}. (weird)", reset_const.to_pyrope());
          reset = reset_const.to_verilog();  // hardcoded value???
        }
      } else {
        reset = get_wire_or_const(node.get_sink_pin("reset").get_driver_pin());

        if (node.get_sink_pin("negreset").is_connected()) {
          negreset = node.get_sink_pin("negreset").get_driver_pin().get_type_const().to_i() != 0;
        }

        if (node.get_sink_pin("async").is_connected()) {
          auto v = node.get_sink_pin("async").get_driver_pin().get_type_const().to_i() != 0;
          if (v) {
            reset_async = absl::StrCat(negreset ? " or negedge " : " or posedge ", reset);
          }
        }

      }
    }

    std::string reset_initial = "'h0";
    if (node.get_sink_pin("initial").is_connected()) {
      reset_initial = node.get_sink_pin("initial").get_driver_pin().get_type_const().to_verilog();
    }

    fout->append("always @(", edge, " ", clock, reset_async, " ) begin\n");

    if (reset.empty()) {
      fout->append(name, " <= ", name_next, ";\n");
    } else {
      if (negreset) {
        fout->append("if (!", reset, ") begin\n");
      } else {
        fout->append("if (", reset, ") begin\n");
      }
      fout->append(name, " <= ", reset_initial, ";\n");
      fout->append("end else begin\n");
      fout->append(name, " <= ", name_next, ";\n");
      fout->append("end\n");
    }

    fout->append("end\n");
  }
}

void Cgen_verilog::add_to_pin2var(std::shared_ptr<File_output> fout, Node_pin &dpin, std::string_view name, bool out_unsigned) {
  if (dpin.is_type_const()) {
    return;  // No point for constants
  }

  auto [it, replaced] = pin2var.insert({dpin.get_compact_class(), std::string(name)});
  if (!replaced)
    return;

  int bits = dpin.get_bits();

  std::string reg_str;
  if (out_unsigned) {
    reg_str = "reg ";
  } else {
    reg_str = "reg signed ";
  }

  if (out_unsigned)
    --bits;

  if (bits <= 1) {
    fout->append(reg_str, name, ";\n");
  } else {
    fout->append(reg_str, "[", std::to_string(bits - 1), ":0] ", name, ";\n");
  }

  if (dpin.is_type_flop()) {
    auto name_next = get_append_to_name(name, "___next_");

    if (bits <= 1) {
      fout->append(reg_str, name_next, ";\n");
    } else {
      fout->append(reg_str, "[", std::to_string(bits - 1), ":0] ", name_next, ";\n");
    }
  }
}

void Cgen_verilog::create_locals(std::shared_ptr<File_output> fout, Lgraph *lg) {
  // IDEA: This pass can create "sub-blocks in lg". Two blocks can process in
  // parallel, if there is not backward edge crossing blocks. Edges that read
  // pin2var are OK, edges that go to pin2expr (future passes) are not OK.

  for (auto node : lg->fast()) {
    auto op = node.get_type_op();

    if (Ntype::is_multi_driver(op)) {
      if (op == Ntype_op::Sub || op == Ntype_op::Memory) {
        for (auto &e : node.inp_edges()) {
          auto name2 = get_scaped_name(e.driver.get_wire_name());
          add_to_pin2var(fout, e.driver, name2, e.driver.is_unsign());
        }
        for (auto &dpin2 : node.out_connected_pins()) {
          auto name2 = get_scaped_name(dpin2.get_wire_name());
          add_to_pin2var(fout, dpin2, name2, dpin2.is_unsign());
        }
      }
      continue;
    }
    I(op != Ntype_op::Sub && op != Ntype_op::Memory);

    auto n_out = node.get_num_out_edges();
    if (n_out == 0)
      continue;

    auto dpin = node.get_driver_pin();

    std::string name = get_scaped_name(dpin.get_wire_name());

    bool out_unsigned = dpin.is_unsign();

    if (op == Ntype_op::Mux) {
      // mux needs name, but it can also has a vector to avoid ifs
      if (node.get_num_inp_edges() > 3 && false) {
        auto name_sel = get_append_to_name(name, "___sel_");

        fout->append("reg signed [", std::to_string(node.get_driver_pin().get_bits() - 1), ":0] ", name_sel, ";\n");
      }
    } else if (op == Ntype_op::Sext) {
      auto b_dpin = node.get_sink_pin("b").get_driver_pin();
      if (!b_dpin.is_invalid() && b_dpin.is_type_const()) {
        auto        dpin2         = node.get_sink_pin("a").get_driver_pin();
        std::string name2         = get_scaped_name(dpin2.get_wire_name());
        bool        out_unsigned2 = dpin2.get_type_op() == Ntype_op::Get_mask;
        add_to_pin2var(fout, dpin2, name2, out_unsigned2);
      }
      if (node.has_name() && node.get_name()[0] != '_')
        continue;
    } else if (op == Ntype_op::Set_mask) {
      add_to_pin2var(fout, dpin, name, false);
    } else if (op == Ntype_op::Const) {
      auto final_expr = node.get_type_const().to_verilog();
      pin2expr.emplace(node.get_driver_pin().get_compact_class(), Expr(final_expr, false));
    } else if (op == Ntype_op::Get_mask) {
      auto a_spin = node.get_sink_pin("a");

      name         = get_scaped_name(absl::StrCat(dpin.get_wire_name(), "_u"));
      out_unsigned = true;  // Get_mask uses a variable to converts/removes sign in a cleaner way
      {
        // Force the "a" pin in get_mask to be a variable (yosys fails otherwise)
        auto a_dpin = a_spin.get_driver_pin();
        if (!pin2var.contains(a_dpin.get_compact_class())) {
          auto name2 = get_scaped_name(a_dpin.get_wire_name());
          add_to_pin2var(fout, a_dpin, name2, false);
        }
      }
    } else if (!node.is_type_flop()) {
      if (node.has_name() && node.get_name()[0] != '_')
        continue;

      if (n_out < 2)
        continue;
    }

    add_to_pin2var(fout, dpin, name, out_unsigned);
  }
}

void Cgen_verilog::do_from_lgraph(Lgraph *lg) {
  TRACE_EVENT("inou", "cgen.verilog");
  Lbench b("inou.cgen.verilog");
  // nrunning!=0 -> incorrect multithread API. Create a Cgen_verilog per thread
  // instance, and then call one do_from_lgraph at a time per object
  assert(nrunning == 0);
  ++nrunning;

  (void)verbose;

  pin2var.clear();
  pin2expr.clear();
  mux2vector.clear();
  first_array_block = true;

  std::string filename;
  if (odir.empty()) {
    filename = absl::StrCat(lg->get_name(), ".v");
  } else {
    filename = absl::StrCat(odir, "/", lg->get_name(), ".v");
  }

  auto fout = std::make_shared<File_output>(filename);

  fout->append("module ", get_scaped_name(lg->get_name()), "(\n");

  create_module_io(fout, lg);  // pin2var adds

  create_locals(fout, lg);    // pin2expr, pin2var & mux2vector adds
  create_memories(fout, lg);  // no local access
  create_subs(fout, lg);      // no local access

  create_combinational(fout, lg);  // pin2expr adds, reads pin2var & mux2vector
  create_outputs(fout, lg);        // reads pin2expr
  create_registers(fout, lg);      // reads pin2var

  fout->append("endmodule\n");

  --nrunning;
}
