// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "absl/strings/str_cat.h"

#include "cgen_verilog.hpp"
#include "pass.hpp"
#include "cell.hpp"
#include "lgraph.hpp"
#include "lgedgeiter.hpp"
#include "mmap_gc.hpp"

Cgen_verilog::Cgen_verilog(bool _verbose, std::string_view _odir): verbose(_verbose), odir(_odir) {
}

std::string Cgen_verilog::get_append_to_name(const std::string &name, std::string_view ext) const {
  std::string name_next;
  if (name[0] == '\\') {
    name_next = name.substr(0,name.size()-1) + std::string(ext);
  }else{
    name_next = name + std::string(ext);
  }

  return name_next;
}

std::string Cgen_verilog::get_expression(Node_pin &dpin) const {

  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it != pin2var.end())
    return var_it->second;

  const auto expr_it = pin2expr.find(dpin.get_compact_class());
  I(expr_it != pin2expr.end());
  for(auto ch:expr_it->second) {
    if (!std::isalnum(ch) && !std::isspace(ch))
      return absl::StrCat( "(", expr_it->second, ")");
  }

  return expr_it->second;
}

void Cgen_verilog::add_expression(std::string &txt_seq, std::string_view txt_op, Node_pin &dpin) const {
  auto expr = get_expression(dpin);;

  if (txt_seq.empty())
    txt_seq = expr;
  else
    absl::StrAppend(&txt_seq, " ", txt_op, " ", expr);
}

void Cgen_verilog::process_mux(std::string &buffer, Node &node) {

  auto ordered_inp = node.inp_edges_ordered();
  I(ordered_inp.size()>2); // at least 0 + 1 + 2

  auto sel_expr = get_expression(ordered_inp[0].driver);
  auto dest_var_it = pin2var.find(node.get_driver_pin().get_compact_class());
  I(dest_var_it != pin2var.end());
  auto dest_var = dest_var_it->second;

  auto mux2vec_it = mux2vector.find(node.get_compact_class());
  if (mux2vec_it == mux2vector.end()) {

    if (ordered_inp.size()==2) { // if-else case
      absl::StrAppend(&buffer, "   if (", sel_expr, ") begin\n");
      absl::StrAppend(&buffer, "     ", dest_var, " = ", get_expression(ordered_inp[2].driver) ,";\n");
      absl::StrAppend(&buffer, "   else\n");
      absl::StrAppend(&buffer, "     ", dest_var, " = ", get_expression(ordered_inp[1].driver) ,";\n");
      absl::StrAppend(&buffer, "   endif\n");
    }else{
      absl::StrAppend(&buffer, "   case (", sel_expr, ")\n");
      auto sel_bits = ordered_inp[0].driver.get_bits()-1; // -1 because mux sel is always tposs
      for(auto i=1u;i<ordered_inp.size();++i) {

        absl::StrAppend(&buffer, "     ", sel_bits, "'d", std::to_string(i-1), " : ", dest_var, " = ", get_expression(ordered_inp[1].driver) ,";\n");
      }
      size_t num_cases = 1<<(sel_bits);
      if (num_cases>ordered_inp.size()-1) {
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
    for(auto e:node.inp_edges()) {
      if (e.sink.get_pid() == 0) {
        add_expression(add_seq, "+", e.driver);
      }else{
        add_expression(sub_seq, "-", e.driver);
      }
    }

    if (sub_seq.empty()) {
      final_expr = add_seq;
    }else if (add_seq.empty()) {
      final_expr = absl::StrCat( " -(", sub_seq, ")");
    }else{
      final_expr = absl::StrCat( add_seq, " - (", sub_seq, ")");
    }

  }else if (op == Ntype_op::Ror) {
  }else if (op == Ntype_op::Div) {
  }else if (op == Ntype_op::Not) {
  }else if (op == Ntype_op::Tposs) {
    final_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    //fmt::print("FIXME: mark out as unsigned\n");
  }else if (op == Ntype_op::LT) {
  }else if (op == Ntype_op::GT) {
  }else if (op == Ntype_op::SHL) {
  }else if (op == Ntype_op::SRA) {
    auto val_expr = get_expression(node.get_sink_pin("a").get_driver_pin());
    auto amt_expr = get_expression(node.get_sink_pin("b").get_driver_pin());

    final_expr = absl::StrCat(val_expr, " >> ", amt_expr);
  }else if (op == Ntype_op::Const) {
    final_expr = node.get_type_const().to_verilog();
  }else if (op == Ntype_op::TupKey || op == Ntype_op::TupRef || op == Ntype_op::TupAdd || op==Ntype_op::TupGet || op == Ntype_op::AttrSet || op == Ntype_op::AttrGet) {
    node.dump();
    Pass::error("could not generate verilog unless it is low level Lgraph node:{} is type {}\n", node.debug_name(), Ntype::get_name(op));
    return;
  }else{
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

    for(auto e:node.inp_edges()) {
      add_expression(final_expr, txt_op, e.driver);
    }
  }

  //fmt::print("node:{} expr:{}\n",node.debug_name(), final_expr);
	if (final_expr.empty()) {
		Pass::info("likely issue in node:{} that has no compute value", node.debug_name());
		final_expr = "'hx";
	}

  auto var_it = pin2var.find(dpin.get_compact_class());
  if (var_it == pin2var.end()) {
    pin2expr.emplace(dpin.get_compact_class(), final_expr);
  }else{
    absl::StrAppend(&buffer, "  ", var_it->second, " = ", final_expr , ";\n");
  }
}

void Cgen_verilog::create_module_io(std::string &buffer, LGraph *lg) {

  absl::StrAppend(&buffer, "module ", lg->get_name(), "(\n");

  bool first_arg = true;
  lg->each_sorted_graph_io([&](const Node_pin &pin, Port_ID pos) {
    (void)pos;

    if (!first_arg)
      absl::StrAppend(&buffer, "  ,");
    else
      absl::StrAppend(&buffer, "   ");
    first_arg = false;

    if (pin.is_graph_input()) {
      absl::StrAppend(&buffer, "input ");
    }else{
      absl::StrAppend(&buffer, "output reg ");
    }

    const auto name = get_scaped_name(pin.get_name());
    const auto bits = pin.get_bits();

    if (bits>1) {
      absl::StrAppend(&buffer, "[", bits-1, ":0] ", name , "\n");
    }else{
      absl::StrAppend(&buffer, name , "\n");
    }

    pin2var.emplace(pin.get_compact_class(), name);
  });

  absl::StrAppend(&buffer, ");\n");
}

void Cgen_verilog::create_combinational(std::string &buffer, LGraph *lg) {

  for(auto node:lg->forward()) {
    auto op = node.get_type_op();
    if (Ntype::is_multi_driver(op)) {
      absl::StrAppend(&buffer, "FIXME:", node.debug_name());
      continue;
    }

    if (!node.has_outputs())
      continue;

    if (op == Ntype_op::Mux) {
      process_mux(buffer, node);
    }else{
      process_simple_node(buffer, node);
    }
  }
}

void Cgen_verilog::create_outputs(std::string &buffer, LGraph *lg) {
  lg->each_graph_output([&](const Node_pin &dpin) {
    const auto name = get_scaped_name(dpin.get_name());
    auto out_dpin = dpin.change_to_sink_from_graph_out_driver().get_driver_pin();
    absl::StrAppend(&buffer, "  ", name, " = ", get_expression(out_dpin), ";\n");
  });
}

void Cgen_verilog::create_registers(std::string &buffer, LGraph *lg) {

  for(auto node:lg->fast()) {
    if (!node.is_type_flop())
      continue;

    auto dpin = node.get_driver_pin();

    absl::StrAppend(&buffer, "always @(posedge ) begin\n");

    const auto name      = get_scaped_name(dpin.get_name());
    const auto name_next = get_scaped_name(std::string(dpin.get_name()) + "_next");
    absl::StrAppend(&buffer, name_next, " <= ", name, ";\n");

    absl::StrAppend(&buffer, "end\n");
  }
}

void Cgen_verilog::create_locals(std::string &buffer, LGraph *lg) {

  // IDEA: This pass can create "sub-blocks in lg". Two blocks can process in
  // parallel, if there is not backward edge crossing blocks. Edges that read
  // pin2var are OK, edges that go to pin2expr (future passes) are not OK.

  for(auto node:lg->fast()) {
    auto op = node.get_type_op();
    if (Ntype::is_multi_driver(op))
      continue;

    auto n_out = node.get_num_out_edges();
    if (n_out==0)
      continue;

    auto dpin = node.get_driver_pin();

    std::string name;
    if (!dpin.has_name()) {
      if (op == Ntype_op::Mux || node.is_type_flop()) {
        name = dpin.debug_name();
      }else if (n_out<2) {
        continue; // It would be nice to pick a reasonable name
      }

      name = dpin.debug_name();
    }else{
      name = get_scaped_name(dpin.get_name());
    }

    if (op == Ntype_op::Mux) {
      // mux needs name, but it can also has a vector to avoid ifs
      if (node.get_num_inp_edges()>3 && false) {

        auto name_sel = get_append_to_name(name, "_sel");

        // FIXME: mux2vector.emplace(node.get_compact_class(), name_sel);

        absl::StrAppend(&buffer, "reg [", node.get_driver_pin().get_bits()-1, ":0] ", name_sel , ";\n");
      }
    }else if (!node.is_type_flop()) {
      if (node.has_name() && node.get_name()[0] != '_')
        continue;

      if (n_out<2)
        continue;
    }

    pin2var.emplace(dpin.get_compact_class(), name);
    const auto bits = dpin.get_bits();

    if (bits>1) {
      absl::StrAppend(&buffer, "reg [", bits-1, ":0] ", name , ";\n");
    }else{
      absl::StrAppend(&buffer, "reg ", name , ";\n");
    }

    if (node.is_type_flop()) {
      auto name_next = get_append_to_name(name, "_next");

      if (bits>1) {
        absl::StrAppend(&buffer, "reg [", bits-1, ":0] ", name_next , ";\n");
      }else{
        absl::StrAppend(&buffer, "reg ", name_next , ";\n");
      }
    }
  }
}

std::tuple<std::string, int> Cgen_verilog::setup_file(LGraph *lg) const {
  std::string filename;
  if (odir.empty()) {
    filename = absl::StrCat(lg->get_name(), ".v");
  }else{
    filename = absl::StrCat(odir, "/", lg->get_name(), ".v");
  }

  int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd<0) {
    mmap_lib::mmap_gc::try_collect_fd(); // maybe they are all used by LG?
    fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }
  if (fd<0) {
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

void Cgen_verilog::do_from_lgraph(LGraph *lg) {
  (void) verbose;

  pin2var.clear();
  pin2expr.clear();
  mux2vector.clear();

  auto [filename,fd] = setup_file(lg);
  I(fd>=0);

  {
    std::string buffer;
    create_module_io(buffer, lg);  // pin2var adds
    append_to_file(filename, fd, buffer);
  }

  {
    std::string buffer;
    create_locals(buffer, lg); // pin2var & mux2vector adds
    append_to_file(filename, fd, buffer);
  }

  {
    std::string buffer;
    create_combinational(buffer, lg); // pin2expr adds, reads pin2var & mux2vector
    if (!buffer.empty()) {
      absl::StrAppend(&buffer, "end\n");

      append_to_file(filename, fd, "always @(*) begin\n");
      append_to_file(filename, fd, buffer);
    }
  }

  {
    std::string buffer;
    create_outputs(buffer, lg); // reads pin2expr
    if (!buffer.empty()) {
      absl::StrAppend(&buffer, "end\n");

      append_to_file(filename, fd, "always @(*) begin\n");
      append_to_file(filename, fd, buffer);
    }
  }

  {
    std::string buffer;
    create_registers(buffer, lg); // reads pin2var
    append_to_file(filename, fd, buffer);
  }

  append_to_file(filename, fd, "endmodule\n");
}

