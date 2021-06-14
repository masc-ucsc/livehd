// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "absl/strings/str_cat.h"
#include "cell.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "mmap_gc.hpp"
#include "pass.hpp"

Cgen_verilog::Cgen_verilog(bool _verbose, std::string_view _odir) : verbose(_verbose), odir(_odir) {

  if (reserved_keyword.empty()) {
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

std::string Cgen_verilog::get_wire_or_const(const Node_pin &dpin) {
  if (dpin.is_type_const())
    return dpin.get_type_const().to_verilog();

  return get_scaped_name(dpin.get_wire_name());
}

std::string Cgen_verilog::get_scaped_name(std::string_view name) {
  if (name[0] == '%') {
    if (name[1] == '.') {
      name = name.substr(2);
    }else{
      name = name.substr(1);
    }
  }

  if (reserved_keyword.contains(name)) {
    return absl::StrCat("\\", name , " ");
  }

  for (auto ch : name) {
    if (!std::isalnum(ch) && ch != '_')
      return absl::StrCat("\\", name , " ");
  }

  return std::string{name};
}

std::string Cgen_verilog::get_append_to_name(const std::string &name, std::string_view ext) const {
  if (name[0] == '\\') {
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
  for (auto ch : expr_it->second) {
    if (!std::isalnum(ch) && !std::isspace(ch))
      return absl::StrCat("(", expr_it->second, ")");
  }

  return expr_it->second;
}

void Cgen_verilog::add_expression(std::string &txt_seq, std::string_view txt_op, Node_pin &dpin) const {
  auto expr = get_expression(dpin);
  ;

  if (txt_seq.empty())
    txt_seq = expr;
  else
    absl::StrAppend(&txt_seq, " ", txt_op, " ", expr);
}

void Cgen_verilog::process_flop(std::string &buffer, Node &node) {
  auto dpin_d = node.get_sink_pin("din").get_driver_pin();
  auto dpin_q = node.get_driver_pin();

  std::string pin_name  = dpin_q.get_wire_name();
  const auto  name_next = get_scaped_name(absl::StrCat("___next_",std::string(pin_name)));

  if (dpin_d.is_invalid()) {
    absl::StrAppend(&buffer, "  ", name_next, " = 'hx; // disconnected flop\n");
  } else {
    absl::StrAppend(&buffer, "  ", name_next, " = ", get_expression(dpin_d), ";\n");
  }
}

void Cgen_verilog::process_memory(std::string &buffer, Node &node) {
  (void)buffer;

  auto iname = get_scaped_name(node.default_instance_name());

  int n_rd_ports=0;
  int n_wr_ports=0;

  // WARNING: The read_vector must be computed first, to have know if the
  // addr/din/... are read/writes ahead (traverse out of order)

  struct Port_field {
    bool rdport;
    Node_pin enable;
    Node_pin addr;
    Node_pin clock;
    Node_pin din; // only for write port
  };
  std::vector<Port_field> port_vector; // first == read, second == port_id

  std::string parameters;

  bool first_entry=true;
  for(auto e:node.inp_edges()) {
    auto pin_name = e.sink.get_pin_name();

    size_t port_id = e.sink.get_pid() / 11;

    if (port_vector.size()<=port_id)
      port_vector.resize(1+port_id);

    if (pin_name == "bits") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for bits not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      absl::StrAppend(&parameters, first_entry?"":" ,", ".BITS(", v, ")");
      first_entry=false;
    }else if (pin_name == "size") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for size not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      absl::StrAppend(&parameters, first_entry?"":" ,",".SIZE(", v, ")");
      first_entry=false;
    }else if (pin_name == "latency") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for latency not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      absl::StrAppend(&parameters, first_entry?"":" ,",".LATENCY_", std::to_string(port_id), "(", v, ")");
      first_entry=false;
    }else if (pin_name == "wensize") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for wensize not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      absl::StrAppend(&parameters, first_entry?"":" ,",".WENSIZE", "(", v, ")");
      first_entry=false;
    }else if (pin_name == "fwd") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant for fwd not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      auto v = e.driver.get_type_const().to_verilog();
      absl::StrAppend(&parameters, first_entry?"":" ,",".FWD", "(", v, ")");
      first_entry=false;
    }else if (pin_name == "clock") {
      port_vector[port_id].clock = e.driver;
    }else if (pin_name == "addr") {
      port_vector[port_id].addr  = e.driver;
    }else if (pin_name == "enable") {
      port_vector[port_id].enable  = e.driver;
    }else if (pin_name == "din") {
      port_vector[port_id].din  = e.driver;
    }else if (pin_name == "rdport") {
      if (!e.driver.is_type_const()) {
        Pass::error("memory {} should have a constant rdport not {}", node.debug_name(), e.driver.get_node().debug_name());
        return;
      }
      bool rdport = !e.driver.get_type_const().is_false();
      port_vector[port_id].rdport = rdport;

      if (rdport) {
        ++n_rd_ports;
      }else{
        ++n_wr_ports;
      }
    }
  }

  bool single_clock = true;
  Node_pin base_clock_dpin = port_vector[0].clock;
  {
    for(auto &p:port_vector) {
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

  absl::StrAppend(&buffer
      ,"cgen_memory_", single_clock? "":"multiclock_", std::to_string(n_rd_ports), "rd_", std::to_string(n_wr_ports), "wr " // name
      ," #(", parameters, ") ", iname, "(\n"); // rest

  first_entry=true;
  if (single_clock) {
    absl::StrAppend(&buffer, ".clock(", get_wire_or_const(base_clock_dpin), ")\n");
    first_entry = false;
  }

  {
    auto n_rd_pos = 0;
    auto n_wr_pos = 0;
    auto n_pos = 0;
    for(auto &p:port_vector) {
      if (p.rdport) {
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid()) {
          node.dump();
          Pass::error("memory {} read port is not correctly configured\n", node.debug_name());
        }
        absl::StrAppend(&buffer, first_entry?"":",",".rd_addr_"  , std::to_string(n_rd_pos), "(", get_wire_or_const(p.addr)  , ")\n");
        first_entry=false;

        auto id = std::to_string(n_rd_pos);

        absl::StrAppend(&buffer, ",.rd_enable_", id, "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          absl::StrAppend(&buffer, ",.rd_clock_", id, "(", get_wire_or_const(p.clock), ")\n");
        }
        auto dout_dpin = node.setup_driver_pin_raw(n_pos); // rd data out
        absl::StrAppend(&buffer, ",.rd_dout_", id, "(", get_wire_or_const(dout_dpin), ")\n");
        ++n_rd_pos;
      }else{
        if (p.addr.is_invalid() || p.enable.is_invalid() || p.clock.is_invalid() || p.din.is_invalid()) {
          node.dump();
          Pass::error("memory {} write port is not correctly configured\n", node.debug_name());
        }
        absl::StrAppend(&buffer, first_entry?"":",", ".wr_addr_"  , std::to_string(n_wr_pos), "(", get_wire_or_const(p.addr), ")\n");
        first_entry=false;

        auto id = std::to_string(n_wr_pos);

        absl::StrAppend(&buffer, ",.wr_enable_", id, "(", get_wire_or_const(p.enable), ")\n");
        if (!single_clock) {
          absl::StrAppend(&buffer, ",.wr_clock_", id, "(", get_wire_or_const(p.clock), ")\n");
        }
        absl::StrAppend(&buffer, ",.wr_din_"   , id, "(", get_wire_or_const(p.din), ")\n");
        ++n_wr_pos;
      }
      ++n_pos;
    }
    I(n_rd_pos == n_rd_ports);
    I(n_wr_pos == n_wr_ports);
  }

  absl::StrAppend(&buffer, ");\n");
}

void Cgen_verilog::process_mux(std::string &buffer, Node &node) {
  auto ordered_inp = node.inp_edges_ordered();
  I(ordered_inp.size() > 2);  // at least 0 + 1 + 2

  auto sel_expr    = get_expression(ordered_inp[0].driver);
  auto dest_var_it = pin2var.find(node.get_driver_pin().get_compact_class());
  I(dest_var_it != pin2var.end());
  auto dest_var = dest_var_it->second;

  auto mux2vec_it = mux2vector.find(node.get_compact_class());
  if (mux2vec_it == mux2vector.end()) {
    if (ordered_inp.size() == 3) {  // if-else case
      absl::StrAppend(&buffer, "   if (", sel_expr, ") begin\n");
      absl::StrAppend(&buffer, "     ", dest_var, " = ", get_expression(ordered_inp[2].driver), ";\n");
      absl::StrAppend(&buffer, "   end else begin\n");
      absl::StrAppend(&buffer, "     ", dest_var, " = ", get_expression(ordered_inp[1].driver), ";\n");
      absl::StrAppend(&buffer, "   end\n");
    } else {
      absl::StrAppend(&buffer, "   case (", sel_expr, ")\n");
      auto sel_bits = ordered_inp[0].driver.get_bits();
      for (auto i = 1u; i < ordered_inp.size(); ++i) {
        absl::StrAppend(&buffer,
                        "     ",
                        sel_bits,
                        "'d",
                        std::to_string(i - 1),
                        " : ",
                        dest_var,
                        " = ",
                        get_expression(ordered_inp[i].driver),
                        ";\n");
      }
      size_t num_cases = 1 << (sel_bits);
      if (num_cases > ordered_inp.size() - 1) {
        absl::StrAppend(&buffer, "       default: ", dest_var, " = 'hx;\n");
      }
      absl::StrAppend(&buffer, "   endcase\n");
    }
  }
}

void Cgen_verilog::process_simple_node(std::string &buffer, Node &node) {
  auto dpin = node.get_driver_pin();
  auto op   = node.get_type_op();
  I(!Ntype::is_multi_driver(op));

  std::string final_expr;

  if (op == Ntype_op::Sum) {
    std::string add_seq;
    std::string sub_seq;
    for (auto e : node.inp_edges()) {
      if (e.sink.get_pid() == 0) {
        add_expression(add_seq, "+", e.driver);
      } else {
        add_expression(sub_seq, "+", e.driver);
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
        absl::StrAppend(&final_expr, " | ", get_expression(inp_edges[i].driver));
      }
      absl::StrAppend(&final_expr, "}");
    }
  } else if (op == Ntype_op::Div) {
    auto lhs   = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto rhs   = get_expression(node.get_sink_pin("b").get_driver_pin());
    final_expr = absl::StrCat(lhs, "/", rhs);

  } else if (op == Ntype_op::Not) {
    auto lhs   = get_expression(node.get_sink_pin("a").get_driver_pin());
    final_expr = absl::StrCat("~", lhs);

  } else if (op == Ntype_op::Get_mask) {
#if 0
    const std::string src = get_expression(node.get_sink_pin("a").get_driver_pin());
    final_expr = absl::StrCat("{1'b0,", src, "}");
#else
    auto a         = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto mask_dpin = node.get_sink_pin("mask").get_driver_pin();
    if (mask_dpin.is_type_const()) {
      auto v = mask_dpin.get_type_const();
      if (v == Lconst(-1)) {
        final_expr = a;
      } else if (v.is_mask()) {
        auto ubits = v.get_bits() - 1;  // -1 because it is a positive
        final_expr = absl::StrCat(a, "[", ubits - 1, ":0]");
      } else {
        bool clean = false;
        auto trail_zeroes=v.get_trailing_zeroes();
        if (trail_zeroes>0) {
          auto v2 = v.rsh_op(trail_zeroes);
          if (v2.is_mask()) {
            clean = true;
            auto ubits = v.get_bits() - 1;  // -1 because it is a positive
            final_expr = absl::StrCat(a, "[", ubits - 1, ":", trail_zeroes, "]");
          }
        }
        if (!clean) {
          I(false);  // FIXME: implement the more complicated cases (check lconst::get_mask_op)
        }
      }
    } else {
      I(false);  // FIXME: implement this
    }
#endif
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

      final_expr = absl::StrCat(lhs, "& ((1'sh", std::to_string(bits), " << ", pos_expr, ")-1)");
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
          absl::StrAppend(&final_expr, " && ", l, cmp, r);
        }
      }
    }
  } else if (op == Ntype_op::SHL) {
    auto val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    std::string onehot;
    bool first=true;
    for(auto &amt_dpin:node.get_sink_pin("B").inp_drivers()) {
      auto amt_expr = get_expression(amt_dpin);
      absl::StrAppend(&onehot, first?"(":" | (", val_expr, " << ", amt_expr, ")");
      first = false;
    }
    final_expr = onehot;
  } else if (op == Ntype_op::SRA) {
    auto val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto amt_expr = get_expression(node.get_sink_pin("b").get_driver_pin());

    // TODO: If val_expr min>=0, then >> is OK
    final_expr = absl::StrCat(val_expr, " >>> ", amt_expr);
  } else if (op == Ntype_op::Const) {
    // final_expr = node.get_type_const().to_verilog();
    return; // Done before at create_locals
  } else if (op == Ntype_op::TupAdd || op == Ntype_op::TupGet || op == Ntype_op::AttrSet || op == Ntype_op::AttrGet) {
    node.dump();
    Pass::error("could not generate verilog unless it is low level Lgraph node:{} is type {}\n",
                node.debug_name(),
                Ntype::get_name(op));
    return;
  } else {
    std::string_view txt_op;
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
      add_expression(final_expr, txt_op, e.driver);
    }
  }

  // fmt::print("node:{} expr:{}\n",node.debug_name(), final_expr);
  if (final_expr.empty()) {
    Pass::info("likely issue in node:{} that has no compute value", node.debug_name());
    final_expr = "'hx";
  }

  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it == pin2var.end()) {
    pin2expr.emplace(dpin.get_compact_class(), final_expr);
  } else {
    absl::StrAppend(&buffer, "  ", var_it->second, " = ", final_expr, ";\n");
  }
}

void Cgen_verilog::create_module_io(std::string &buffer, Lgraph *lg) {
  absl::StrAppend(&buffer, "module ", get_scaped_name(lg->get_name()), "(\n");

  bool first_arg = true;
  lg->each_sorted_graph_io([&](const Node_pin &pin, Port_ID pos) {
    (void)pos;

    if (!first_arg)
      absl::StrAppend(&buffer, "  ,");
    else
      absl::StrAppend(&buffer, "   ");
    first_arg = false;

    if (pin.is_graph_input()) {
      absl::StrAppend(&buffer, "input signed ");
    } else {
      absl::StrAppend(&buffer, "output reg signed ");
    }

    const auto name = get_scaped_name(pin.get_name());
    const auto bits = pin.get_bits();

    if (bits > 1) {
      absl::StrAppend(&buffer, "[", bits - 1, ":0] ", name, "\n");
    } else {
      absl::StrAppend(&buffer, name, "\n");
    }

    pin2var.emplace(pin.get_compact_class(), name);
  });

  absl::StrAppend(&buffer, ");\n");
}

void Cgen_verilog::create_memories(std::string &buffer, Lgraph *lg) {
  for(auto node:lg->fast()) {
    auto op = node.get_type_op();
    if (op!=Ntype_op::Memory)
      continue;

    process_memory(buffer, node);
  }
}

void Cgen_verilog::create_subs(std::string &buffer, Lgraph *lg) {
  lg->each_local_sub_fast([&buffer](Node &node, Lg_type_id lgid) {
    (void)lgid;

    auto        iname = get_scaped_name(node.default_instance_name());
    const auto &sub   = node.get_type_sub_node();

    absl::StrAppend(&buffer, get_scaped_name(sub.get_name()), " ", iname, "(\n");

    bool first_entry = true;
    for (auto &io_pin : sub.get_sorted_io_pins()) {
      Node_pin dpin;
      if (io_pin.is_input()) {
        auto spin = node.get_sink_pin(io_pin.name);
        dpin = spin.get_driver_pin();
      }else{
        dpin = node.get_driver_pin(io_pin.name);
        if (!dpin.is_connected())
          dpin.invalidate();
      }
      if (!dpin.is_invalid()) {
        absl::StrAppend(&buffer, first_entry?"":",", ".", io_pin.name, "(", get_wire_or_const(dpin), ")\n");
        first_entry = false;
      }
    }

    absl::StrAppend(&buffer, ");\n");
  });
}

void Cgen_verilog::create_combinational(std::string &buffer, Lgraph *lg) {
  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    if (Ntype::is_multi_driver(op)) {
      continue;
    }

    if (!node.has_outputs() || node.is_type_flop())
      continue;

    // flops added to the last always with outputs
    if (op == Ntype_op::Mux) {
      process_mux(buffer, node);
    } else {
      process_simple_node(buffer, node);
    }
  }
}

void Cgen_verilog::create_outputs(std::string &buffer, Lgraph *lg) {
  lg->each_graph_output([&](const Node_pin &dpin) {
    auto       spin = dpin.change_to_sink_from_graph_out_driver();
    if (!spin.is_connected())
      return;

    auto name = get_scaped_name(dpin.get_name());

    auto out_dpin = spin.get_driver_pin();
    absl::StrAppend(&buffer, "  ", name, " = ", get_expression(out_dpin), ";\n");
  });

  for (auto node : lg->fast()) {
    if (node.is_type_flop())
      process_flop(buffer, node);
  }
}

void Cgen_verilog::create_registers(std::string &buffer, Lgraph *lg) {
  for (auto node : lg->fast()) {
    if (!node.is_type_flop())
      continue;

    auto dpin = node.get_driver_pin();

    std::string pin_name = dpin.get_wire_name();

    // FIXME: HERE if flop is output, do not create flop
    const auto name      = get_scaped_name(pin_name);
    const auto name_next = get_scaped_name(absl::StrCat("___next_", std::string(pin_name)));

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
       if (reset_const != Lconst(0) && reset_const != Lconst("false")) {
         Pass::info("flop reset is hardwired to value:{}. (weird)", reset_const.to_pyrope());
         reset = reset_const.to_verilog(); // hardcoded value???
       }
      }else{
        if (node.get_sink_pin("negreset").is_connected()) {
          negreset = node.get_sink_pin("negreset").get_driver_pin().get_type_const().to_i() != 0;
        }

        if (node.get_sink_pin("async").is_connected()) {
          auto v = node.get_sink_pin("async").get_driver_pin().get_type_const().to_i() != 0;
          if (v) {
            reset_async = absl::StrCat(negreset ? "or negedge " : "or posedge ", reset);
          }
        }

        reset = get_scaped_name(node.get_sink_pin("reset").get_wire_name());
      }
    }

    std::string reset_initial = "'h0";
    if (node.get_sink_pin("initial").is_connected()) {
      reset_initial = node.get_sink_pin("initial").get_driver_pin().get_type_const().to_verilog();
    }

    absl::StrAppend(&buffer, "always @(", edge, " ", clock, reset_async, " ) begin\n");

    if (!reset.empty()) {
      if (negreset) {
        absl::StrAppend(&buffer, "if (!", reset, ") begin\n");
      } else {
        absl::StrAppend(&buffer, "if (", reset, ") begin\n");
      }
      absl::StrAppend(&buffer, name, " <= ", reset_initial, ";\n");
      absl::StrAppend(&buffer, "end else begin\n");
    }

    absl::StrAppend(&buffer, name, " <= ", name_next, ";\n");

    if (!reset.empty()) {
      absl::StrAppend(&buffer, "end\n");
    }

    absl::StrAppend(&buffer, "end\n");
  }
}

void Cgen_verilog::add_to_pin2var(std::string &buffer, Node_pin &dpin, const std::string &name, bool out_unsigned) {
  if (dpin.is_type_const()) {
    return; // No point for constants
  }

  auto [it, replaced] = pin2var.insert({dpin.get_compact_class(), name});
  if (!replaced)
    return;

  int bits = dpin.get_bits();

  std::string reg_str;
  if (out_unsigned) {
    reg_str = "reg ";
  } else {
    reg_str = "reg signed ";
  }

  --bits;  //[0:0] is 1 bit already
  if (out_unsigned)
    --bits;

  if (bits <= 0) {
    absl::StrAppend(&buffer, reg_str, name, ";\n");
  } else {
    absl::StrAppend(&buffer, reg_str, "[", bits, ":0] ", name, ";\n");
  }

  if (dpin.is_type_flop()) {
    auto name_next = get_append_to_name(name, "___next_");

    if (bits <= 0) {
      absl::StrAppend(&buffer, reg_str, name_next, ";\n");
    } else {
      absl::StrAppend(&buffer, reg_str, "[", bits, ":0] ", name_next, ";\n");
    }
  }
}

void Cgen_verilog::create_locals(std::string &buffer, Lgraph *lg) {
  // IDEA: This pass can create "sub-blocks in lg". Two blocks can process in
  // parallel, if there is not backward edge crossing blocks. Edges that read
  // pin2var are OK, edges that go to pin2expr (future passes) are not OK.

  for (auto node : lg->fast()) {
    auto op = node.get_type_op();

    if (Ntype::is_multi_driver(op)) {
      if (op == Ntype_op::Sub || op == Ntype_op::Memory) {
        for (auto &e : node.inp_edges()) {
          auto name2 = get_scaped_name(e.driver.get_wire_name());
          add_to_pin2var(buffer, e.driver, name2, e.driver.is_unsign());
        }
        for (auto &dpin2 : node.out_connected_pins()) {
          auto name2 = get_scaped_name(dpin2.get_wire_name());
          add_to_pin2var(buffer, dpin2, name2, dpin2.is_unsign());
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

        absl::StrAppend(&buffer, "reg signed [", node.get_driver_pin().get_bits() - 1, ":0] ", name_sel, ";\n");
      }
    } else if (op == Ntype_op::Sext) {
      auto b_dpin = node.get_sink_pin("b").get_driver_pin();
      if (!b_dpin.is_invalid() && b_dpin.is_type_const()) {
        auto        dpin2         = node.get_sink_pin("a").get_driver_pin();
        std::string name2         = get_scaped_name(dpin2.get_wire_name());
        bool        out_unsigned2 = dpin2.get_type_op() == Ntype_op::Get_mask;
        add_to_pin2var(buffer, dpin2, name2, out_unsigned2);
      }
      if (node.has_name() && node.get_name()[0] != '_')
        continue;
    } else if (op == Ntype_op::Const) {
      auto final_expr = node.get_type_const().to_verilog();
      pin2expr.emplace(node.get_driver_pin().get_compact_class(), final_expr);
    } else if (op == Ntype_op::Get_mask) {
      name         = get_scaped_name(absl::StrCat("___unsign_", node.get_sink_pin("a").get_wire_name()));
      out_unsigned = true;  // Get_mask uses a variable to converts/removes sign in a cleaner way
      {
        // Force the "a" pin in get_mask to be a variable (yosys fails otherwise)
        auto dpin2 = node.get_sink_pin("a").get_driver_pin();
        if (!pin2var.contains(dpin2.get_compact_class())) {
          auto name2 = get_scaped_name(dpin2.get_wire_name());
          add_to_pin2var(buffer, dpin2, name2, false);
        }
      }
    } else if (!node.is_type_flop()) {
      if (node.has_name() && node.get_name()[0] != '_')
        continue;

      if (n_out < 2)
        continue;
    } 

    add_to_pin2var(buffer, dpin, name, out_unsigned);
  }
}

std::tuple<std::string, int> Cgen_verilog::setup_file(Lgraph *lg) const {
  std::string filename;
  if (odir.empty()) {
    filename = absl::StrCat(lg->get_name(), ".v");
  } else {
    filename = absl::StrCat(odir, "/", lg->get_name(), ".v");
  }

  int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    mmap_lib::mmap_gc::try_collect_fd();  // maybe they are all used by LG?
    fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }
  if (fd < 0) {
    Pass::error("could not create destination {} verilog file (permissions?)", filename);
    return std::tuple(filename, -1);
  }

  return std::tuple(filename, fd);
}

void Cgen_verilog::append_to_file(const std::string &filename, int fd, const std::string &buffer) const {
  if (buffer.empty())
    return;

  int sz = write(fd, buffer.data(), buffer.size());
  if (sz != static_cast<int>(buffer.size())) {
    Pass::error("could not write to {} verilog file (file size?)", filename);
    return;
  }
}

void Cgen_verilog::do_from_lgraph(Lgraph *lg) {
  (void)verbose;

  pin2var.clear();
  pin2expr.clear();
  mux2vector.clear();

  auto [filename, fd] = setup_file(lg);
  I(fd >= 0);

  {
    std::string buffer;
    create_module_io(buffer, lg);  // pin2var adds
    append_to_file(filename, fd, buffer);
  }

  // BEGIN PARALLEL BLOCK 1
  {
    std::string buffer;
    create_locals(buffer, lg);  // pin2expr, pin2var & mux2vector adds
    append_to_file(filename, fd, buffer);
  }

  {
    std::string buffer;
    create_memories(buffer, lg);  // no local access
    append_to_file(filename, fd, buffer);
  }

  {
    std::string buffer;
    create_subs(buffer, lg);  // no local access
    append_to_file(filename, fd, buffer);
  }
  // END PARALLEL BLOCK 1

  // BEGIN PARALLEL BLOCK 2
  {
    std::string buffer;
    create_combinational(buffer, lg);  // pin2expr adds, reads pin2var & mux2vector
    if (!buffer.empty()) {
      absl::StrAppend(&buffer, "end\n");

      append_to_file(filename, fd, "always_comb begin\n");
      append_to_file(filename, fd, buffer);
    }
  }

  {
    std::string buffer;
    create_outputs(buffer, lg);  // reads pin2expr
    if (!buffer.empty()) {
      absl::StrAppend(&buffer, "end\n");

      append_to_file(filename, fd, "always_comb begin\n");
      append_to_file(filename, fd, buffer);
    }
  }

  {
    std::string buffer;
    create_registers(buffer, lg);  // reads pin2var
    append_to_file(filename, fd, buffer);
  }
  // BEGIN PARALLEL BLOCK 2

  append_to_file(filename, fd, "endmodule\n");
}
