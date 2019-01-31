//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "iassert.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

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

  if (dst.find_first_of('.') == std::string::npos) {
    Pass::error("dst:{} should point to a wire after a module", dst);
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

  std::string_view src_wname     = src.substr(src.find_last_of('.'));
  std::string_view src_hierarchy = src.substr(0,src.find_last_of('.'));
  std::string_view dst_wname     = dst.substr(dst.find_last_of('.'));
  std::string_view dst_hierarchy = dst.substr(0,dst.find_last_of('.'));

  std::string_view dst_parent    = "";
  if (dst_hierarchy.find_last_of('.') != std::string::npos)
    dst_parent = dst.substr(0,dst_hierarchy.find_last_of('.'));

  Lg_type_id src_lgid = 0;
  Lg_type_id dst_lgid = 0;
  Lg_type_id dst_parent_lgid = 0;

  const auto hier = g->get_hierarchy();
  for(auto &[name,lgid]:hier) {
    if (name == src_hierarchy) {
      src_lgid = lgid;
    }else if (name == dst_hierarchy) {
      dst_lgid = lgid;
    }else if (name == dst_parent) {
      dst_parent_lgid = lgid;
    }
  }

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
  }

}
