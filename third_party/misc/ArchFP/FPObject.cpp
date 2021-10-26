#include "FPObject.hpp"

#include "ann_place.hpp"
#include "cell.hpp"
#include "helpers.hpp"
#include "lgraph.hpp"

FPObject::FPObject()
    : refCount(0)
    , x(0.0)
    , y(0.0)
    , width(0.0)
    , height(0.0)
    , area(0.0)
    , type(Ntype_op::Invalid)
    , name(Ntype::get_name(Ntype_op::Invalid).to_s())
    , hint(UnknownHint)
    , count(1) {}

void FPObject::setSize(double widthArg, double heightArg) {
  width  = widthArg;
  height = heightArg;
  area   = width * height;
}

void FPObject::setLocation(double xArg, double yArg) {
  x = xArg;
  y = yArg;
}

void FPObject::outputHotSpotLayout(std::ostream& o, double startX, double startY) {
  o << getUniqueName() << "\t" << getWidth() / 1000 << "\t" << getHeight() / 1000 << "\t" << calcX(startX) / 1000 << "\t"
    << calcY(startY) / 1000 << "\n";
}

mmap_lib::str FPObject::getUniqueName() const {
  if (name == " " || name == "")
    return name;

  return mmap_lib::str::concat(name, std::to_string(Name2Count(name.to_s())));
}

double FPObject::calcX(double startX) const {
  if (xReflect) {
    return xLeft[xMirrorDepth - 1] - (startX + x - xRight[xMirrorDepth - 1] + width);
  } else
    return startX + x;
}

double FPObject::calcY(double startY) const {
  if (yReflect) {
    return yTop[yMirrorDepth - 1] - (startY + y - yBottom[yMirrorDepth - 1] + height);
  } else
    return startY + y;
}

unsigned int FPObject::findNode(Node_tree& tree, Tree_index tidx, double cX, double cY) {
  Ntype_op t = getType();

  unsigned int sub_count;

  if (Ntype::is_synthesizable(t)) {  // leaf node - current hier structure is fine
    sub_count = outputLgraphLayout(tree, tidx, cX, cY);
  } else if (t == Ntype_op::Sub) {  // Sub node - parameters need to be adjusted

    bool found = false;

    Tree_index child_idx = tree.get_first_child(tidx);
    while (child_idx != tree.invalid_index()) {
      auto child = tree.ref_data(child_idx);

      if (!child->is_type_sub_present() || child->has_place()) {
        child_idx = tree.get_sibling_next(child_idx);
        continue;
      }

      Lgraph* child_lg = Lgraph::open(tree.get_root_lg()->get_path(), child->get_type_sub());

      if (child_lg->get_name() != getName()) {
        child_idx = tree.get_sibling_next(child_idx);
        continue;
      }

      // fmt::print("assigning child subnode {} to parent hier ({}, {})\n", child->debug_name(), child->get_hidx().level,
      // child->get_hidx().pos);

      // write placement information to subnode as well
      Ann_place p(calcX(cX), calcY(cY), getWidth(), getHeight());

      child->set_place(p);
      child->set_instance_name(getUniqueName());

      sub_count = outputLgraphLayout(tree, child_idx, cX, cY);
      found     = true;

      break;
    }

    assert(found);
  } else if (t == Ntype_op::Invalid) {  // specific kind of layout - current hier structure is fine
    sub_count = outputLgraphLayout(tree, tidx, cX, cY);
  } else {
    assert(false);
  }

  return sub_count;
}

unsigned int FPObject::outputLgraphLayout(Node_tree& tree, Tree_index tidx, double startX, double startY) {
  bool found = false;

  // Tree_index child_idx = tree.get_first_child(tidx);
  Tree_index child_idx = tree.get_last_free(tidx, getType());
  while (child_idx != tree.invalid_index()) {
    Node* child = tree.ref_data(child_idx);

    // fmt::print("testing child node {} with parent hier ({}, {})\n", child->debug_name(), child->get_hidx().level,
    // child->get_hidx().pos);
    if (child->get_type_op() != getType() || child->has_place()) {
      child_idx = tree.get_sibling_next(child_idx);
      continue;
    }

    found = true;
    if (verbose) {
      fmt::print("assigning child node {} to parent hier ({})\n",
                 child->debug_name(),
                 child->get_hidx());
    }

    Ann_place p(calcX(startX), calcY(startY), getWidth(), getHeight());
    child->set_place(p);
    child->set_instance_name(getUniqueName());

    tree.set_last_free(tidx, getType(), child_idx);

    break;
  }

  assert(found);

  return 1;
}
