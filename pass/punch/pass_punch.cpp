//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "iassert.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgwirenames.hpp"

#include "pass_punch.hpp"

void setup_pass_punch() {
  Pass_punch p;
  p.setup();
}

void Pass_punch::setup() {
  Eprp_method m1("pass.punch", "punch wires between modules", &Pass_punch::work);

  m1.add_label_required("src", "source module:net name to tap. E.g: a_module_name:a_instance_name.b_instance_name->a_wire_name");
  m1.add_label_required("dst", "destination module:net name to connect. E.g: a_module_name:c_instance_name.d_instance_name->b_wire_name");

  register_pass(m1);
}

Pass_punch::Pass_punch()
    : Pass("punch") {
}

Pass_punch::Pass_punch(std::string_view _src, std::string_view _dst)
    : Pass("punch") {
  bool ok_src = src_hierarchy.set_hierarchy(_src);
  bool ok_dst = dst_hierarchy.set_hierarchy(_dst);

  if (!ok_src || !ok_dst) {
    Pass::error("Looks like hierarchy syntax is wrong. See this: e.g: a_module_name:a_instance_name.b_instance_name->a_wire_name");
    return;
  }
}

void Pass_punch::work(Eprp_var &var) {

  auto src = var.get("src");
  auto dst = var.get("dst");

  Pass_punch pass(src,dst);

  for(const auto g : var.lgs) {
    pass.punch(g, src, dst);
  }
}

void Pass_punch::punch(LGraph *g, std::string_view src, std::string_view dst) {
  LGBench b("pass.punch");

  /////////////////////////////////////
  // find the level of common hierarchy between src and dst
  /////////////////////////////////////
  fmt::print("Searching for common hierarchy...\n");
  bool common_hier_found = true;
  uint16_t common_hier_depth = 0;
  if (this->src_hierarchy.get_module_name() != this->dst_hierarchy.get_module_name()) {
    common_hier_found = false;
  } else {
    while (this->src_hierarchy.get_inst_num() > common_hier_depth &&
           this->dst_hierarchy.get_inst_num() > common_hier_depth) {
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
  std::string_view src_h        = this->src_hierarchy.get_hierarchy();
  std::string_view dst_h        = this->dst_hierarchy.get_hierarchy();
  std::string_view dst_h_parent = this->dst_hierarchy.get_hierarchy_upto(this->dst_hierarchy.get_inst_num() - 1);
  std::string_view common       = this->src_hierarchy.get_hierarchy_upto(common_hier_depth);

  Lg_type_id src_lgid = 0;
  Lg_type_id dst_lgid = 0;
  Lg_type_id dst_parent_lgid = 0;
  Lg_type_id common_lgid = 0;

  const auto hier = g->get_hierarchy();
  for(auto &[name,lgid]:hier) {
    if (name == src_h) {
      src_lgid = lgid;
    } else if (name == dst_h) {
      dst_lgid = lgid;
    } else if (name == dst_h_parent) {
      dst_parent_lgid = lgid;
    } else if (name == common) {
      common_lgid = lgid;
    }
  }

  // sanity checks
  if (src_lgid == 0) {
    Pass::warn("pass.punch no src found");
    return;
  }
  if (dst_lgid == 0) {
    Pass::warn("pass.punch no dst found");
    return;
  }
  fmt::print("src_lgid:        {}\n",src_lgid);
  fmt::print("dst_lgid:        {}\n",dst_lgid);
  fmt::print("dst_lgid_parent: {}\n",dst_parent_lgid);
  fmt::print("dst_lgid:        {}\n\n",common_lgid);

  auto *src_g = LGraph::open(g->get_path(), src_lgid);
  if (src_g == 0) {
    Pass::error("pass.punch could not open path:{} lgid:{} src:{}",g->get_path(), src_lgid, this->src_hierarchy.get_hierarchy());
    return;
  }

  auto *dst_g = LGraph::open(g->get_path(), dst_lgid);
  if (dst_g == 0) {
    Pass::error("pass.punch could not open path:{} lgid:{} dst:{}",g->get_path(), dst_lgid, this->dst_hierarchy.get_hierarchy());
    return;
  }


  /////////////////////////////////////
  // Let the punching begin!
  /////////////////////////////////////
  // add output port to the inner module to wire out 
  std::string_view target_wire_name = this->src_hierarchy.get_wire_name();
  std::string output_wire_name = fmt::format("{}_punch", target_wire_name);
  bool output_added = add_output(src_g, target_wire_name, output_wire_name);

  if (output_added) {
    fmt::print("Output port added...\n");
    src_g->sync();
  }
//  dst_g->sync();
/*

  LGraph *dst_g = 0;

  fmt::print(" src:{} MATCH name:{} lgid:{}\n", src, src_g->get_name(), src_lgid);
  if (dst_lgid) {

    fmt::print(" dst:{} MATCH name:{} lgid:{}\n", dst, dst_g->get_name(), dst_lgid);
  }else{
    LGraph *parent = LGraph::open(g->get_path(), dst_parent_lgid);

    fmt::print(" dst:{} does not exist, creating instance at parent:{} name:{} lgid:{}\n",dst, dst_parent, parent->get_name(), dst_parent_lgid);

    dst_lgid = dst_parent_lgid;
  }

  // FIXME: instead of potato, use the full name of the path
  // dst:foo.bar.xxx.yy.dd
  // potato = foo_bar_xxx_yy_dd
  //
  // FIXME: instead of itrack and potato, use the two last files>
  // dst:punching.cover.c
  // itrack = cover
  // potato = cover_c
  //
  // FIXME:
  // Connect the hierarchy. Not done at the moment

  bool ok;
  auto *lgo = LGraph::open(g->get_path(), src_lgid);
  ok = add_output(lgo, src_wname,"potato");
  if (!ok) {
    Pass::error("pass.punch add wire:{} as output:{} in lgraph:{}",src_wname, "potato", g->get_name());
    return;
  }
  auto *lgi = LGraph::open(g->get_path(), dst_lgid);
  ok = add_input(lgi, dst_wname, "potato");
  if (!ok) {
    Pass::error("pass.punch add wire:{} as output:{} in lgraph:{}",src_wname, "potato", g->get_name());
    return;
  }

  add_dest_instance(lgi, "tracker", "itrack", "potato");

  lgo->sync();
  lgi->sync();*/
}

bool Pass_punch::add_output(LGraph *g, std::string_view wname, std::string_view output) {
  I(g);
  I(!wname.empty());
  I(!output.empty());

  if (!g->has_wirename(wname))
    return false;

  if (g->has_wirename(output))
    return false;

  auto dpin         = g->get_node(g->get_node_id(wname)).get_driver_pin();
  auto wname_bits   = g->get_bits(dpin);
  auto wname_offset = g->get_offset(dpin);

  auto spin = g->add_graph_output(output, wname_bits, wname_offset);
  g->add_edge(dpin, spin);

  fmt::print("Adding output:{} from wire:{} to lgraph:{} (dpin {}:{}) (spin {}:{})\n"
  , output, wname, g->get_name(), dpin.get_idx(),dpin.get_pid(), spin.get_idx(),spin.get_pid());

  return true;
}

bool Pass_punch::add_input(LGraph *g, std::string_view wname, std::string_view input) {

  I(g);
  I(!input.empty());

  if (g->has_wirename(input))
    return false;

#if 0
FIXME: This makes no sense. It check if there is an input, and adds the input if already exists???
  fmt::print("Adding input:{} lgraph:{}\n",input, g->get_name());
  auto ipin = g->get_graph_input(input);
  auto wname_idx    = g->get_node_id(wname);
  auto wname_bits   = g->get_bits(wname_idx);
  auto wname_offset = g->get_offset(ipin);

  g->add_graph_input(input, wname_bits, wname_offset);
  // g->add_edge(idx, wname_idx);
#else
  I(0);
#endif

  return true;
}

bool Pass_punch::add_dest_instance(LGraph *g, std::string_view type, std::string_view instance, std::string_view wname) {

  if (!g->has_wirename(wname))
    return false;
#if 0
FIXME: This code also seems bad
  auto wname_idx    = g->get_node_id(wname);
  auto wname_bits   = g->get_bits(wname_idx);
  auto wname_offset = g->get_offset(wname_idx);

  auto *ins_g = LGraph::open(g->get_path(), type);
  if (ins_g==0) {
    ins_g = LGraph::create(g->get_path(), type, "pass.punch");
  }

  if (ins_g->is_graph_output(wname))
    return false;
  if (!ins_g->is_graph_input(wname)) {
    if (ins_g->has_wirename(wname))
      return false;

    ins_g->add_graph_input(wname, wname_bits, wname_offset);
  }

  Port_ID ins_input_pid = ins_g->get_graph_input(wname).get_pid();

  Index_ID ins_idx;
  if (g->has_instance_name(instance)) {
    ins_idx = g->get_node_from_instance_name(instance);
  }else{
    ins_idx = g->create_node().get_nid();
    g->node_subgraph_set(ins_idx, ins_g->lg_id());
    g->set_node_instance_name(ins_idx, instance);
  }

//  I(g->get_node(wname_idx).get_nid() == wname_idx); // FIXME: only master root for the moment

  g->add_edge(g->get_node(wname_idx).setup_driver_pin(0), g->get_node(ins_idx).setup_sink_pin(ins_input_pid), wname_bits);

  ins_g->close();
#else
  I(0);
#endif

  return true;
}


