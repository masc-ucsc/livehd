//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_punch.hpp"

#include "iassert.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_punch() { Pass_punch::setup(); }
Pass_punch::Pass_punch(const Eprp_var &var) : Pass("pass.punch", var) {}

void Pass_punch::setup() {
  Eprp_method m1("pass.punch", "punch wires between modules", &Pass_punch::work);

  m1.add_label_required("src", "source module:net name to tap. E.g: a_module_name:a_instance_name.b_instance_name->a_wire_name");
  m1.add_label_required("dst",
                        "destination module:net name to connect. E.g: a_module_name:c_instance_name.d_instance_name->b_wire_name");

  register_pass(m1);
}

#if 1
void Pass_punch::work(Eprp_var &var) {
  I(false);  // FIXME:
}
#else
Pass_punch::Pass_punch(LGraph *top, std::string_view _src, std::string_view _dst) : Pass("punch") {
  uint16_t ok_src = src_hierarchy.set_hierarchy(top, _src);
  uint16_t ok_dst = dst_hierarchy.set_hierarchy(top, _dst);

  if (ok_src == 1 || ok_dst == 1) {
    Pass::error("Looks like hierarchy syntax is wrong. See this: e.g: a_module_name:a_instance_name.b_instance_name->a_wire_name");
    return;
  }

  if (ok_src == 2 || ok_dst == 2) {
    Pass::error("Provided hierarchy does not exist, check your src and dst arguments");
    return;
  }
}

void Pass_punch::work(Eprp_var &var) {
  auto src = var.get("src");
  auto dst = var.get("dst");

  for (const auto g : var.lgs) {
    Pass_punch pass(g, src, dst);
    pass.punch(g, src, dst);
  }
}

void Pass_punch::punch(LGraph *g, std::string_view src, std::string_view dst) {
  Lbench b("pass.punch");

  /////////////////////////////////////
  // find the level of common hierarchy between src and dst
  /////////////////////////////////////
  fmt::print("Searching for common hierarchy...\n");
  bool     common_hier_found = true;
  uint16_t common_hier_depth = 0;
  if (this->src_hierarchy.get_module_name() != this->dst_hierarchy.get_module_name()) {
    common_hier_found = false;
  } else {
    while (this->src_hierarchy.get_inst_num() > common_hier_depth && this->dst_hierarchy.get_inst_num() > common_hier_depth) {
      std::string_view src = this->src_hierarchy.get_hierarchy_upto(common_hier_depth);
      std::string_view dst = this->dst_hierarchy.get_hierarchy_upto(common_hier_depth);
      if (src != dst) {
        break;
      }
      common_hier_depth++;
    }
  }

  if (!common_hier_found) {
    Pass::error("pass.punch could not find common hierarchy");
    return;
  }
  fmt::print("Common hierarchy found...\n\n");

  /////////////////////////////////////
  // get subgraph ids
  /////////////////////////////////////
  fmt::print("Trying to get subgraphs of source and destination...\n");
  std::string src_h = this->src_hierarchy.get_hierarchy();
  std::string dst_h = this->dst_hierarchy.get_hierarchy();

  fmt::print("src_h = {}\n", src_h);
  fmt::print("dst_h = {}\n", dst_h);

  Lg_type_id src_lgid = this->src_hierarchy.get_lgid(src_h);
  Lg_type_id dst_lgid = this->dst_hierarchy.get_lgid(dst_h);
  ;

  // sanity checks
  if (src_lgid == 0) {
    Pass::warn("pass.punch no src found");
    return;
  }
  if (dst_lgid == 0) {
    Pass::warn("pass.punch no dst found");
    return;
  }
  fmt::print("src_lgid:        {}\n", src_lgid);
  fmt::print("dst_lgid:        {}\n", dst_lgid);

  auto *src_g = LGraph::open(g->get_path(), src_lgid);
  if (src_g == 0) {
    Pass::error("pass.punch could not open path:{} lgid:{} src:{}", g->get_path(), src_lgid, this->src_hierarchy.get_hierarchy());
    return;
  }

  auto *dst_g = LGraph::open(g->get_path(), dst_lgid);
  if (dst_g == 0) {
    Pass::error("pass.punch could not open path:{} lgid:{} dst:{}", g->get_path(), dst_lgid, this->dst_hierarchy.get_hierarchy());
    return;
  }

  /////////////////////////////////////
  // Let the punching begin!
  /////////////////////////////////////
  // add output port to the inner-most module to get the wire out
  std::string_view target_wire_name = this->src_hierarchy.get_wire_name();
  std::string      output_wire_name = fmt::format("{}_punch", target_wire_name);
  this->add_output(src_g, target_wire_name, output_wire_name);
  fmt::print("{}\n", src_g->get_name());

  src_g->sync();
  g->sync();
  /*

    // punch from src module upto common hierarchy
    uint16_t current_depth = src_hierarchy.get_hierarchy_depth() - 1;
    while (current_depth > common_hier_depth) {
      current_depth--;
      // get subgraph
      Lg_type_id current_lgid = this->src_hierarchy.get_lgid(current_depth);
      LGraph* current_g = LGraph::open(g->get_path(), current_lgid);

      //
      std::string current_name = fmt::format("out_{}_{}", output_wire_name, current_depth);
      auto sub_g_id = current_g->get_node_from_instance_name(this->src_hierarchy.get_instance(current_depth));
      auto sub_g_node = current_g->get_node(sub_g_id);
      auto pin = sub_g_node.setup_driver_pin(output_wire_name);
      add_output(current_g, pin, current_name);
      current_g->sync();
      break;
    }*/
}
void Pass_punch::add_output(LGraph *g, std::string_view wname, std::string_view output) {
  I(g);
  I(!wname.empty());
  I(!output.empty());
  I(!g->is_graph_input(output));
  I(!g->is_graph_output(output));

  bool     done = false;
  Node_pin dpin;
  uint32_t bits;
  uint32_t offset;
  for (const auto &nid : g->forward()) {
    if (done) {
      break;
    }

    auto node = Node(g, 0, Node::Compact(nid));

    for (const auto &edge : node.inp_edges()) {
      if (edge.driver.has_name()) {
        std::string_view this_wname = edge.driver.get_name();
        if (this_wname == wname) {
          fmt::print("wire found: {} {}\n", wname, output);
          done   = true;
          dpin   = edge.driver;
          bits   = edge.get_bits();
          offset = dpin.get_offset();
          break;
        }
      }
    }
  }

  I(done);
  auto spin = g->add_graph_output(output, bits, offset);
  g->add_edge(dpin, spin);
}

#if 0
void Pass_punch::add_output(LGraph *g, Node_pin dpin, std::string output) {
  I(g);
  I(!output.empty());
  I(!g->has_wirename(output));
  I(!g->is_graph_input(output));
  I(!g->is_graph_output(output));

  auto bits   = g->get_bits(dpin);
  auto offset = g->get_offset(dpin);

  auto spin = g->add_graph_output(output, 1, 0);
  g->add_edge(dpin, spin);
}

bool Pass_punch::add_input(LGraph *g, std::string_view wname, std::string_view input) {

  I(g);
  I(!input.empty());

  if (g->has_wirename(input) || g->is_graph_input(input) || g->is_graph_output(input))
    return false;

  fmt::print("Adding input:{} lgraph:{}\n",input, g->get_name());
  auto wname_idx    = g->get_node_id(wname);
  auto wname_bits   = g->get_bits(wname_idx);
  auto wname_offset = g->get_offset(wname_idx);

  g->add_graph_input(input, wname_bits, wname_offset);
  // g->add_edge(idx, wname_idx);

  return true;
}
#endif
#endif
