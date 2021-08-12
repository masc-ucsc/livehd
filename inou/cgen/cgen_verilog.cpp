// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cell.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "mmap_gc.hpp"
#include "pass.hpp"

Cgen_verilog::Cgen_verilog(bool _verbose, const mmap_lib::str _odir) : verbose(_verbose), odir(_odir), nrunning(0) {
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

mmap_lib::str Cgen_verilog::get_wire_or_const(const Node_pin &dpin) {
  if (dpin.is_type_const())
    return dpin.get_type_const().to_verilog();

  return get_scaped_name(dpin.get_wire_name());
}

mmap_lib::str Cgen_verilog::get_scaped_name(const mmap_lib::str name) { // follow verilog name and add \ when needed

  mmap_lib::str res_name;
  if (name.front() == '%') {
    if (name[1] == '.') {
      res_name = name.substr(2);
    } else {
      res_name = name.substr(1);
    }
  }else if (reserved_keyword.contains(name)) {
    return mmap_lib::str::concat("\\", name, " ");
  }else{
    res_name = name;
  }

  for (auto i=0u; i < res_name.size() ; ++i) {
    auto ch = res_name[i];
    if (!std::isalnum(ch) && ch != '_')
      return mmap_lib::str::concat("\\", res_name, " ");
  }

  return res_name;
}

mmap_lib::str Cgen_verilog::get_append_to_name(const mmap_lib::str name, const mmap_lib::str ext) const {
  if (name.front() == '\\') {
    return mmap_lib::str::concat("\\", ext, name.substr(1, name.size() - 1), " ");
  }

  return mmap_lib::str::concat(ext, name);
}

mmap_lib::str Cgen_verilog::get_expression(const Node_pin &dpin) const {
  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it != pin2var.end())
    return var_it->second;

  const auto expr_it = pin2expr.find(dpin.get_compact_class());
  I(expr_it != pin2expr.end());
  if (expr_it->second.needs_parenthesis) {
    return mmap_lib::str::concat("(", expr_it->second.var, ")");
  }

  return expr_it->second.var;
}

mmap_lib::str Cgen_verilog::add_expression(const mmap_lib::str txt_seq, const mmap_lib::str txt_op, Node_pin &dpin) const {
  auto expr = get_expression(dpin);

  if (txt_seq.empty())
    return expr;

  return mmap_lib::str::concat(txt_seq, " ", txt_op, " ", expr);
}

void Cgen_verilog::process_flop(std::shared_ptr<File_output> fout, Node &node) {
  auto dpin_d = node.get_sink_pin("din").get_driver_pin();
  auto dpin_q = node.get_driver_pin();

  auto pin_name  = dpin_q.get_wire_name();
  const auto  name_next = get_scaped_name(mmap_lib::str::concat("___next_", pin_name));

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

  mmap_lib::str parameters;

  bool first_entry = true;
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
      auto v = e.driver.get_type_const().to_verilog();
      parameters = mmap_lib::str::concat(parameters, first_entry ? "" : " ,", ".BITS(", v, ")");
      first_entry = false;
    } else if (pin_name == "size") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for size not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      parameters = mmap_lib::str::concat(parameters, first_entry ? "" : " ,", ".SIZE(", v, ")");
      first_entry = false;
    } else if (pin_name == "latency") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for latency not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      parameters = mmap_lib::str::concat(parameters, first_entry ? "" : " ,", ".LATENCY_", port_id, "(", v, ")");
      first_entry = false;
    } else if (pin_name == "wensize") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for wensize not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      parameters = mmap_lib::str::concat(parameters, first_entry ? "" : " ,", ".WENSIZE", "(", v, ")");
      first_entry = false;
    } else if (pin_name == "fwd") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for fwd not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      parameters = mmap_lib::str::concat(parameters, first_entry ? "" : " ,", ".FWD", "(", v, ")");
      first_entry = false;
    } else if (pin_name == "clock") {
      port_vector[port_id].clock = e.driver;
    } else if (pin_name == "addr") {
      port_vector[port_id].addr = e.driver;
    } else if (pin_name == "enable") {
      port_vector[port_id].enable = e.driver;
    } else if (pin_name == "din") {
      port_vector[port_id].din = e.driver;
    } else if (pin_name == "rdport") {
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
  fout->append(mmap_lib::str::concat("cgen_memory_", single_clock ? "" : "multiclock_"));
  fout->append(mmap_lib::str::concat(n_rd_ports, "rd_"));
  fout->append(mmap_lib::str::concat(n_wr_ports, "wr "));

  // paremeters
  fout->append(" #(",parameters,") ");

  // instance name
  fout->append(iname, "(\n");

  first_entry = true;
  if (single_clock) {
    fout->append(mmap_lib::str::concat(".clock(", get_wire_or_const(base_clock_dpin), ")\n"));
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
        fout->append(mmap_lib::str::concat(first_entry ? "  .rd_addr_" : "  ,.rd_addr_",
                     n_rd_pos,
                     "(",
                     get_wire_or_const(p.addr),
                     ")\n"));
        first_entry = false;

        fout->append("  ,.rd_enable_", n_rd_pos, "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          fout->append("  ,.rd_clock_", n_rd_pos, "(", get_wire_or_const(p.clock), ")\n");
        }
        auto dout_dpin = node.setup_driver_pin_raw(n_pos);  // rd data out
        fout->append("  ,.rd_dout_", n_rd_pos, "(", get_wire_or_const(dout_dpin), ")\n");
        ++n_rd_pos;
      } else {
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid() || p.din.is_invalid()) {
          node.dump();
          Pass::error("memory {} write port is not correctly configured\n", node.debug_name());
        }
        fout->append(mmap_lib::str::concat(first_entry ? "  .wr_addr_" : "  ,.wr_addr_",
                     n_wr_pos,
                     "(",
                     get_wire_or_const(p.addr),
                     ")\n"));

        first_entry = false;

        fout->append("  ,.wr_enable_", n_wr_pos, "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          fout->append("  ,.wr_clock_", n_wr_pos, "(", get_wire_or_const(p.clock), ")\n");
        }
        fout->append("  ,.wr_din_", n_wr_pos, "(", get_wire_or_const(p.din), ")\n");
        ++n_wr_pos;
      }
      ++n_pos;
    }
    I(n_rd_pos == n_rd_ports);
    I(n_wr_pos == n_wr_ports);
  }

  fout->append(");\n");
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
        fout->append("     ",
                     sel_bits,
                     "'d",
                     mmap_lib::str(i - 1));
        fout->append(" : ",
                     dest_var,
                     " = ",
                     get_expression(ordered_inp[i].driver),
                     ";\n");
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

  mmap_lib::str final_expr;

  if (op == Ntype_op::Sum) {
    mmap_lib::str add_seq;
    mmap_lib::str sub_seq;
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
      final_expr = mmap_lib::str::concat(" -(", sub_seq, ")");
    } else {
      final_expr = mmap_lib::str::concat(add_seq, " - (", sub_seq, ")");
    }

  } else if (op == Ntype_op::Ror) {
    auto inp_edges = node.inp_edges();
    if (inp_edges.size() == 1) {
      auto expr  = get_expression(inp_edges[0].driver);
      final_expr = mmap_lib::str::concat("|", expr);
    } else {
      auto expr  = get_expression(inp_edges[0].driver);
      final_expr = mmap_lib::str::concat("|{", expr);
      for (auto i = 1u; i < inp_edges.size(); ++i) {
        final_expr = mmap_lib::str::concat(final_expr, " | ", get_expression(inp_edges[i].driver));
      }
      final_expr = mmap_lib::str::concat(final_expr, "}");
    }
  } else if (op == Ntype_op::Div) {
    auto lhs   = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto rhs   = get_expression(node.get_sink_pin("b").get_driver_pin());
    final_expr = mmap_lib::str::concat(lhs, "/", rhs);

  } else if (op == Ntype_op::Not) {
    auto lhs   = get_expression(node.get_sink_pin("a").get_driver_pin());
    final_expr = mmap_lib::str::concat("~", lhs);

  } else if (op == Ntype_op::Set_mask) {
    auto a_dpin     = node.get_sink_pin("a").get_driver_pin();
    auto a          = get_expression(a_dpin);

    auto mask_dpin  = node.get_sink_pin("mask").get_driver_pin();
    I(mask_dpin.is_type_const());  // Do we want to support this? (not easy)

    auto mask_v = mask_dpin.get_type_const();
    I(!mask_v.has_unknowns());

    if (mask_v == Lconst(0)) {  // nothing to do
      final_expr = a;
    } else {
      auto [range_begin, range_end] = mask_v.get_mask_range();
      auto a_bits = a_dpin.get_bits();

      if (range_end>static_cast<int>(a_bits))
        range_end = a_bits;

      auto value_dpin = node.get_sink_pin("value").get_driver_pin();
      auto value      = get_expression(value_dpin);

      // fmt::print("a_bits:{} mask:{} minr:{} maxr:{}\n", a_bits, mask_v.to_pyrope(), range_begin, range_end);

      if (range_begin>static_cast<int>(a_bits)) {
        final_expr = a;
      }else if (range_begin<0 || range_end<0) { // no continuous range
        mmap_lib::str sel;
        for(auto i=0u;i<a_bits;++i) {
          if (mask_v.and_op(Lconst(1)<<i).is_known_false()) { // use a
            if (sel.empty())
              sel = mmap_lib::str::concat( a, "[", i ,"]");
            else
              sel = mmap_lib::str::concat(sel, ",", a, "[", i ,"]");
          }else{ // use value
            if (sel.empty())
              sel = mmap_lib::str::concat( value, "[", i ,"]");
            else
              sel = mmap_lib::str::concat(sel, ",", value, "[", i ,"]");
          }
        }
        final_expr = mmap_lib::str::concat("{", sel, "}");
      }else{
        mmap_lib::str a_replaced;
        if ((range_end-1) == range_begin) {
          a_replaced = mmap_lib::str::concat(value, "[", range_begin, "]");
        }else{
          a_replaced = mmap_lib::str::concat(value, "[", range_end - 1, ":", range_begin, "]");
        }
        mmap_lib::str a_high;
        mmap_lib::str a_low;

        Lconst a_val; // only if const
        bool a_is_const = a_dpin.is_type_const();
        if (a_is_const) {
          a_val = a_dpin.get_type_const();
        }

        if (range_end< static_cast<int>(a_bits)) {
          if (a_is_const) {
            // auto v = a_val.get_mask_op(Lconst::get_mask_value(a_bits, range_end));
            auto v = a_val.rsh_op(range_end);
            a_high = mmap_lib::str::concat(v.to_verilog(),",");
          }else if (static_cast<int>(a_bits)==(range_end+1)) {
            a_high = mmap_lib::str::concat(a, "[", range_end, "],");
          }else{
            a_high = mmap_lib::str::concat(a, "[", a_bits-1, ":", range_end, "],");
          }
        }

        if (range_begin>0) {
          if (a_is_const) {
            auto v = a_val.get_mask_op(Lconst::get_mask_value(range_begin));
            a_low = mmap_lib::str::concat(",", v.to_verilog());
          }else if (range_begin==1) {
            a_low = mmap_lib::str::concat(",", a, "[0]");
          }else{
            a_low = mmap_lib::str::concat(",", a, "[", range_begin-1, ":0]");
          }
        }

        final_expr = mmap_lib::str::concat("{", a_high, a_replaced, a_low, "}");
      }
    }
  } else if (op == Ntype_op::Get_mask) {
    auto mask_dpin = node.get_sink_pin("mask").get_driver_pin();
    I(mask_dpin.is_type_const());

    auto mask_v = mask_dpin.get_type_const();
    I(!mask_v.has_unknowns());

    auto a_dpin = node  .get_sink_pin("a").get_driver_pin()      ;
    auto a_bits = a_dpin                  .get_bits()            ;
    auto a      =                          get_expression(a_dpin);

    auto [range_begin, range_end] = mask_v.get_mask_range();

    if (range_begin<0 || range_end<0) {
      mmap_lib::str sel;
      auto max_bits = std::max(mask_v.get_bits(), a_bits);
      for(auto i=0u;i<max_bits;++i) {
        if (mask_v.and_op(Lconst(1)<<i).is_known_false())
          continue;

        if (sel.empty())
          sel = mmap_lib::str::concat( a, "[", i ,"]");
        else
          sel = mmap_lib::str::concat(sel, ",", a, "[", i ,"]");
      }
      final_expr = mmap_lib::str::concat("{", sel, "}");
    }else{
      if (range_begin>static_cast<int>(a_bits)) {
        final_expr = mmap_lib::str::concat("{", range_end-range_begin, "{", a , "[", a_bits-1 ,"]}}");
      }else if (range_end>static_cast<int>(a_bits)) {
        auto top = mmap_lib::str::concat("{", range_end-a_bits, "{", a , "[", a_bits-1 ,"]}");
        final_expr = mmap_lib::str::concat(top, ",", a, "[", a_bits-1, ":", range_begin, "]}");
      }else{
        final_expr = mmap_lib::str::concat(a, "[", range_end-1, ":", range_begin, "]");
      }
    }
  } else if (op == Ntype_op::Sext) {
    auto lhs      = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto pos_node = node.get_sink_pin("b").get_driver_node();
    if (pos_node.is_type_const()) {
      auto lpos = pos_node.get_type_const();
      if (lpos.is_i()) {
        final_expr = mmap_lib::str::concat(lhs, "[", lpos.to_i() - 1, ":0]");
      }
    }
    if (final_expr.empty()) {
      auto bits     = node.get_sink_pin("b").get_driver_pin().get_bits();
      auto pos_expr = get_expression(node.get_sink_pin("b").get_driver_pin());

      final_expr = mmap_lib::str::concat(lhs, "& ((1'sh", bits, " << ", pos_expr, ")-1)");
    }
  } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
    std::vector<mmap_lib::str> lhs;
    std::vector<mmap_lib::str> rhs;
    for (const auto &e : node.inp_edges()) {
      if (e.sink.get_pin_name() == "A") {
        lhs.emplace_back(get_expression(e.driver));
      } else {
        rhs.emplace_back(get_expression(e.driver));
      }
    }
    mmap_lib::str cmp;
    if (op == Ntype_op::GT) {
      cmp = " > ";
    } else {
      cmp = " < ";
    }
    for (const auto &l : lhs) {
      for (const auto &r : rhs) {
        if (final_expr.empty()) {
          final_expr = mmap_lib::str::concat(l, cmp, r);
        } else {
          final_expr = mmap_lib::str::concat(final_expr, " && ", l, cmp, r);
        }
      }
    }
  } else if (op == Ntype_op::SHL) {
    auto        val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    mmap_lib::str onehot;
    bool        first = true;
    for (auto &amt_dpin : node.get_sink_pin("B").inp_drivers()) {
      auto amt_expr = get_expression(amt_dpin);
      onehot = mmap_lib::str::concat(onehot, first ? "(" : " | (", val_expr, " << ", amt_expr, ")");
      first = false;
    }
    final_expr = onehot;
  } else if (op == Ntype_op::SRA) {
    auto val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto amt_expr = get_expression(node.get_sink_pin("b").get_driver_pin());

    // TODO: If val_expr min>=0, then >> is OK
    final_expr = mmap_lib::str::concat(val_expr, " >>> ", amt_expr);
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
    mmap_lib::str txt_op;
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
      fout->append("[", bits - 1, ":0] ", name, "\n");
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
  lg->each_local_sub_fast([fout](Node &node, Lg_type_id lgid) {
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
        fout->append(mmap_lib::str::concat(first_entry ? "" : ",", ".", io_pin.name, "(", get_wire_or_const(dpin), ")\n"));
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

    mmap_lib::str pin_name = dpin.get_wire_name();

    // FIXME: HERE if flop is output, do not create flop
    const auto name      = get_scaped_name(pin_name);
    const auto name_next = get_scaped_name(mmap_lib::str::concat("___next_", pin_name));

    mmap_lib::str edge = "posedge";
    if (node.get_sink_pin("posclk").is_connected()) {
      auto v = node.get_sink_pin("posclk").get_driver_pin().get_type_const().to_i() != 0;
      if (!v) {
        edge = "negedge";
      }
    }
    mmap_lib::str clock = get_scaped_name(node.get_sink_pin("clock").get_wire_name());

    mmap_lib::str reset_async;
    mmap_lib::str reset;
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
        if (node.get_sink_pin("negreset").is_connected()) {
          negreset = node.get_sink_pin("negreset").get_driver_pin().get_type_const().to_i() != 0;
        }

        if (node.get_sink_pin("async").is_connected()) {
          auto v = node.get_sink_pin("async").get_driver_pin().get_type_const().to_i() != 0;
          if (v) {
            reset_async = mmap_lib::str::concat(negreset ? "or negedge " : "or posedge ", reset);
          }
        }

        reset = get_scaped_name(node.get_sink_pin("reset").get_wire_name());
      }
    }

    mmap_lib::str reset_initial = "'h0";
    if (node.get_sink_pin("initial").is_connected()) {
      reset_initial = node.get_sink_pin("initial").get_driver_pin().get_type_const().to_verilog();
    }

    fout->append("always @(", edge, " ", clock, reset_async, " ) begin\n");

    if (reset.empty()) {
      fout->append(name, " <= ", name_next, ";\n");
    }else{
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

void Cgen_verilog::add_to_pin2var(std::shared_ptr<File_output> fout, Node_pin &dpin, const mmap_lib::str name, bool out_unsigned) {
  if (dpin.is_type_const()) {
    return;  // No point for constants
  }

  auto [it, replaced] = pin2var.insert({dpin.get_compact_class(), name});
  if (!replaced)
    return;

  int bits = dpin.get_bits();

  mmap_lib::str reg_str;
  if (out_unsigned) {
    reg_str = "reg ";
  } else {
    reg_str = "reg signed ";
  }

  --bits;  //[0:0] is 1 bit already
  if (out_unsigned)
    --bits;

  if (bits <= 0) {
    fout->append(reg_str, name, ";\n");
  } else {
    fout->append(reg_str, "[", bits, ":0] ", name, ";\n");
  }

  if (dpin.is_type_flop()) {
    auto name_next = get_append_to_name(name, "___next_");

    if (bits <= 0) {
      fout->append(reg_str, name_next, ";\n");
    } else {
      fout->append(reg_str, "[", bits, ":0] ", name_next, ";\n");
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

    mmap_lib::str name = get_scaped_name(dpin.get_wire_name());

    bool out_unsigned = dpin.is_unsign();

    if (op == Ntype_op::Mux) {
      // mux needs name, but it can also has a vector to avoid ifs
      if (node.get_num_inp_edges() > 3 && false) {
        auto name_sel = get_append_to_name(name, "___sel_");

        fout->append("reg signed [", node.get_driver_pin().get_bits() - 1, ":0] ", name_sel, ";\n");
      }
    } else if (op == Ntype_op::Sext) {
      auto b_dpin = node.get_sink_pin("b").get_driver_pin();
      if (!b_dpin.is_invalid() && b_dpin.is_type_const()) {
        auto        dpin2         = node.get_sink_pin("a").get_driver_pin();
        mmap_lib::str name2         = get_scaped_name(dpin2.get_wire_name());
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
      name         = get_scaped_name(mmap_lib::str::concat("___unsign_", node.get_sink_pin("a").get_wire_name()));
      out_unsigned = true;  // Get_mask uses a variable to converts/removes sign in a cleaner way
      {
        // Force the "a" pin in get_mask to be a variable (yosys fails otherwise)
        auto dpin2 = node.get_sink_pin("a").get_driver_pin();
        if (!pin2var.contains(dpin2.get_compact_class())) {
          auto name2 = get_scaped_name(dpin2.get_wire_name());
          add_to_pin2var(fout, dpin2, name2, false);
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

  // nrunning!=0 -> incorrect multithread API. Create a Cgen_verilog per thread
  // instance, and then call one do_from_lgraph at a time per object
  assert(nrunning == 0);
  ++nrunning;

  (void)verbose;

  pin2var   .clear();
  pin2expr  .clear();
  mux2vector.clear();

  mmap_lib::str filename;
  if (odir.empty()) {
    filename = mmap_lib::str::concat(           lg->get_name(), ".v");
  } else {
    filename = mmap_lib::str::concat(odir, "/", lg->get_name(), ".v");
  }

  auto fout = std::make_shared<File_output>(filename);

  fout->append("module ", get_scaped_name(lg->get_name()), "(\n");

  create_module_io     (fout, lg);  // pin2var adds

  create_locals        (fout, lg);  // pin2expr, pin2var & mux2vector adds
  create_memories      (fout, lg);  // no local access
  create_subs          (fout, lg);  // no local access

  create_combinational (fout, lg);  // pin2expr adds, reads pin2var & mux2vector
  create_outputs       (fout, lg);  // reads pin2expr
  create_registers     (fout, lg);  // reads pin2var

  fout->append("endmodule\n");

  --nrunning;
}
