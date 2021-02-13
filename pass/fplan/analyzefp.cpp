//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "analyzefp.hpp"

#include <limits>

#include "ann_place.hpp"

void Pass_fplan_analyzefp::setup() {
  auto a = Eprp_method("pass.fplan.analyzefp",
                       "return information about a given floorplan within a livehd hierarchy",
                       &Pass_fplan_analyzefp::pass);

  a.add_label_optional("regularity", "determine the amount of regularity in the design", "false");
  a.add_label_optional("hpwl", "not implemented", "false");
  a.add_label_optional("all", "run all available kinds of analysis on the floorplan", "false");

  a.add_label_required("top", "top level module in floorplan");
  a.add_label_required("nodes", "modules to analyze");

  register_pass(a);
}

void Pass_fplan_analyzefp::print_area(const Node_tree& nt, const Tree_index& tidx) const {
  const float  fmax  = std::numeric_limits<float>::max();
  float        min_x = fmax, max_x = 0.0f, min_y = fmax, max_y = 0.0f;
  unsigned int counter = 0;

  for (auto child_idx : nt.children(tidx)) {
    auto child = nt.get_data(child_idx);
    if (!child.has_place()) {
      fmt::print("(no area information)\n");
      return;
    }

    const Ann_place& p = child.get_place();
    if (p.get_pos_x() < min_x) {
      min_x = p.get_pos_x();
    }

    if (p.get_pos_y() < min_y) {
      min_y = p.get_pos_y();
    }

    if (p.get_pos_x() + p.get_len_x() > max_x) {
      max_x = p.get_pos_x() + p.get_len_x();
    }

    if (p.get_pos_y() + p.get_len_y() > max_y) {
      max_y = p.get_pos_y() + p.get_len_y();
    }

    counter++;
  }

  const Ann_place& p = nt.get_data(tidx).get_place();
  const double     x = p.get_pos_x();
  const double     y = p.get_pos_y();

  const float area = (max_x - min_x) * (max_y - min_y);
  fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f} mm², height: {:.3f} mm², area: {:.3f} mm², {} unique components\n",
             x,
             y,
             max_x,
             max_y,
             area,
             counter);
}

void Pass_fplan_analyzefp::print_children(const Node_tree& nt, const Tree_index& tidx) const {
  for (auto child_idx : nt.children(tidx)) {
    auto child = nt.get_data(child_idx);

    fmt::print(" ├─ node {}\t", child.get_name());

    if (!child.has_place()) {
      fmt::print("(no area information)\n");
      continue;
    }

    const Ann_place& p = child.get_place();
    const double     x = p.get_pos_x();
    const double     y = p.get_pos_y();
    const double     w = p.get_len_x();
    const double     h = p.get_len_y();

    fmt::print("x: {:.3f}, y: {:.3f}, width: {:.3f} mm², height: {:.3f} mm², area: {:.3f} mm²\n", x, y, w, h, w * h);
  }
}

Pass_fplan_analyzefp::Pass_fplan_analyzefp(const Eprp_var& var) : Pass("pass.fplan", var) {
  LGraph* root = LGraph::open(path, var.get("top"));

  std::vector<std::string_view> names;

  auto   nodestr = var.get("nodes");
  size_t starti  = 0;
  size_t len     = 0;

  for (size_t i = 0; i < nodestr.size(); i++) {
    const char c = nodestr.at(i);
    len++;

    if (i == nodestr.size() - 1) {
      names.push_back(nodestr.substr(starti, len));
    } else if (c == ',' || isblank(c)) {
      names.push_back(nodestr.substr(starti, len - 1));
      starti += len;
      len = 0;
    } else if (isalnum(c) || c == '_') {
      ;
    } else {
      throw std::invalid_argument("unrecognized character while parsing node list!");
    }
  }

  if (names.size() == 0) {
    throw std::invalid_argument("no nodes were passed!");
  }

  const Node_tree nt(root);

  for (auto name : names) {
    if (name == var.get("top")) {
      // TODO: fix this!
      fmt::print("no support for top level modules right now.\n");
    }

    bool found = false;

    for (const auto& index : nt.depth_preorder()) {  // going preorder because higher level nodes probably going to be analyzed more
                                                     // often than leaf nodes
      if (index == nt.get_root()) {
        continue;  // skip root for now
      }

      const auto& n = nt.get_data(index);

      I(!n.is_invalid());

      if (!n.has_name()) {
        throw std::runtime_error("floorplanner has not been run!");
      }

      fmt::print("testing {} against {}\n", n.get_name(), name);

      if (n.get_name() == name) {
        found = true;
        fmt::print("module {}\t", n.get_name());

        if (!n.has_place()) {
          fmt::print("(no area information)");
          break;
        } else {
          print_area(nt, index);
          if (n.is_type_sub_present()) {
            print_children(nt, index);
          }
        }

        if (var.get("hpwl") == "true" || var.get("all") == "true") {
          // TODO
        }

        // computing the "regularity" of a hierarchical design using a method presented in the HiReg paper:
        // regularity = 1 - (area only counting instances of a given LGraph once) / (area counting all instances of an LGraph)
        if (var.get("regularity") == "true" || var.get("all") == "true") {
          // come up with some metric for regularity - HiReg has one, but it requires a hierarchy DAG which is annoying to generate.
        }

        break;
      }
    }

    if (!found) {
      std::string errstr = "cannot locate module ";
      errstr += name.data();
      errstr += "!";
      throw std::runtime_error(errstr);
    }
  }
}

void Pass_fplan_analyzefp::pass(Eprp_var& var) {
  if (var.lgs.size() != 0) {
    // TODO: not providing lgraphs breaks things...?
  }

  Pass_fplan_analyzefp a(var);
}
