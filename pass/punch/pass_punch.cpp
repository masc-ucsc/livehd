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

  m1.add_label_required("src", "source module:net name to tap. E.g: top:i33:i2.wire1");
  m1.add_label_required("dst", "destination module:net name to connect. E.g: mid3:i32:i3.wire3");

  register_pass(m1);
}

Pass_punch::Pass_punch()
    : Pass("punch") {
}

Pass_punch::Pass_punch(std::string_view _src, std::string_view _dst)
    : Pass("punch"),
      src(_src),
      dst(_dst) {
  if (src.find_first_of('.') == std::string::npos) {
    Pass::error("src:{} should point to a wire after a module", src);
    return;
  }
  if (src.find_last_of('.') == src.size()-1) {
    Pass::error("src:{} should point to a valid wire after a module", src);
    return;
  }

  if (dst.find_first_of('.') == std::string::npos) {
    Pass::error("dst:{} should point to a wire after a module", dst);
    return;
  }

  if (dst.find_last_of('.') == dst.size()-1) {
    Pass::error("dst:{} should point to a valid wire after a module", src);
    return;
  }

  if (dst == src) {
    Pass::error("src:{} and dst:{} must be different", src, dst);
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

  std::string_view src_wname     = src.substr(src.find_last_of('.')+1);
  std::string_view src_hierarchy = src.substr(0,src.find_last_of('.'));
  std::string_view dst_wname     = dst.substr(dst.find_last_of('.')+1);
  std::string_view dst_hierarchy = dst.substr(0,dst.find_last_of('.'));

  std::string_view dst_parent    = "";
  if (dst_hierarchy.find_last_of('.') != std::string::npos)
    dst_parent = dst.substr(0,dst_hierarchy.find_last_of('.'));

  Lg_type_id src_lgid = 0;
  Lg_type_id dst_lgid = 0;
  Lg_type_id dst_parent_lgid = 0;

  std::string_view src_hier;
  std::string_view dst_hier;

  const auto hier = g->get_hierarchy();
  for(auto &[name,lgid]:hier) {
    if (name == src_hierarchy) {
      src_lgid = lgid;
      src_hier = name;
    }else if (name == dst_hierarchy) {
      dst_lgid = lgid;
      dst_hier = name;
    }else if (name == dst_parent && dst_lgid == 0) {
      dst_parent_lgid = lgid;
      dst_hier = name;
    }
  }

  std::vector<std::string> vsrc_hier = absl::StrSplit(src_hier,'.');
  std::vector<std::string> vdst_hier = absl::StrSplit(dst_hier,'.');

  fmt::print("src_hier:{} dst_hier:{}\n",src_hier,dst_hier);

  std::string common_hier;
  int min_size = std::min(vsrc_hier.size(),vdst_hier.size());
  for(int i=0;i<min_size;++i) {
    if (vsrc_hier[i] == vdst_hier[i]) {
      if (common_hier.empty())
        common_hier = vsrc_hier[i];
      else
        absl::StrAppend(&common_hier, "." , vsrc_hier[i]);
    }
  }

  auto ch_it = hier.find(common_hier);

  if (ch_it == hier.end()) {
    Pass::error("pass.punch could not find common hierarchy for src:{} dst:{} common:{}",src,dst,common_hier);
    return;
  }

  Lg_type_id common_hier_lgid = ch_it->second;
  fmt::print("common hierarchy {} lgid:{}\n", common_hier, common_hier_lgid);

  if (src_lgid == 0) {
    Pass::warn("pass.punch no src:{} for top:{} match, ignoring punch", src,g->get_name());
    return;
  }
  if (dst_lgid == 0 && dst_parent_lgid == 0) {
    Pass::warn("pass.punch no find (or create at parent) a destination dst:{} for top:{} match, ignoring punch dst_parent:{}", dst,g->get_name(), dst_parent);
    return;
  }

  LGraph *src_g = LGraph::open(g->get_path(), src_lgid);
  if (src_g==0) {
    Pass::error("pass.punch could not open path:{} lgid:{} src:{}",g->get_path(), src_lgid, src);
    return;
  }

  LGraph *dst_g = 0;

  fmt::print(" src:{} MATCH name:{} lgid:{}\n", src, src_g->get_name(), src_lgid);
  if (dst_lgid) {
    dst_g = LGraph::open(g->get_path(), dst_lgid);
    if (dst_g==0) {
      Pass::error("pass.punch could not open path:{} lgid:{} dst:{}",g->get_path(), dst_lgid, dst);
      return;
    }

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
  lgi->sync();
}

bool Pass_punch::add_output(LGraph *g, std::string_view wname, std::string_view output) {

  I(g);
  I(!wname.empty());
  I(!output.empty());

  if (!g->has_wirename(wname))
    return false;

  if (g->has_wirename(output) || g->is_graph_input(output) || g->is_graph_output(output))
    return false;

  fmt::print("Adding output:{} from wire:{} to lgraph:{}\n",output,wname, g->get_name());

  auto wname_idx    = g->get_node_id(wname);
  auto wname_bits   = g->get_bits(wname_idx);
  auto wname_offset = g->get_offset(wname_idx);

  auto idx   = g->add_graph_output(output, 0, wname_bits, wname_offset);
  g->set_node_wirename(idx, output);
  I(idx);
  g->add_edge(idx, wname_idx);

  return true;
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

  auto idx   = g->add_graph_input(input, 0, wname_bits, wname_offset);
  g->set_node_wirename(idx,input);
  I(idx);
  // g->add_edge(idx, wname_idx);

  return true;
}

bool Pass_punch::add_dest_instance(LGraph *g, std::string_view type, std::string_view instance, std::string_view wname) {

  if (!g->has_wirename(wname))
    return false;
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

    auto idx = ins_g->add_graph_input(wname, 0, wname_bits, wname_offset);
    ins_g->set_node_wirename(idx, wname);
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

  g->add_edge(Node_pin(wname_idx,0, false), Node_pin(ins_idx, ins_input_pid, true), wname_bits);

  ins_g->close();

  return true;
}


