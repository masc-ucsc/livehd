//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "analyzefp.hpp"

#include <limits>

#include "ann_place.hpp"

void Pass_fplan_analyzefp::setup() {
  auto a = Eprp_method("pass.fplan.analyzefp",
                       "return information about a given floorplan within a livehd hierarchy",
                       &Pass_fplan_analyzefp::pass);

  a.add_label_required("top", "top level module in floorplan");
  a.add_label_required("nodes", "modules to analyze");

  a.add_label_optional("regularity", "determine the amount of regularity in a module", "false");
  a.add_label_optional("hpwl", "determine the half-perimeter wire length of a module", "false");
  a.add_label_optional("all", "run all available kinds of analysis on a module", "false");

  // TODO: not yet implemented
  a.add_label_optional("ar", "required aspect ratio of subnode(s)", "1.0");

  a.add_label_optional("path",
                       "lgdb directory to analyze",
                       "lgdb");  // can't pass lgraphs because lgraph names are the same per instance

  register_pass(a);
}

void Pass_fplan_analyzefp::print_area(const Node_tree& nt, const Tree_index& tidx) const {
  const Ann_place& p = nt.get_data(tidx).get_place();
  fmt::print("x: {:.3f} mm, y: {:.3f} mm, width: {:.3f} mm, height: {:.3f} mm, area: {:.3f} mm², ar {:.3f}",
             p.get_x(),
             p.get_y(),
             p.get_width(),
             p.get_height(),
             p.get_width() * p.get_height(),
             p.get_width() / p.get_height());
}

void Pass_fplan_analyzefp::print_children(const Node_tree& nt, const Tree_index& tidx) const {
  for (auto child_idx : nt.children(tidx)) {
    auto child = nt.get_data(child_idx);

    if (child_idx != nt.get_last_child(tidx)) {
      fmt::print(" ├─ ");
    } else {
      fmt::print(" └─ ");
    }
    fmt::print("node {}\t", child.get_name());

    print_area(nt, child_idx);
    fmt::print("\n");
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
      error("unrecognized character while parsing node list!");
    }
  }

  if (names.size() == 0) {
    error("no nodes were passed!");
  }

  Node_tree nt(root);

  for (auto name : names) {
    if (name == var.get("top")) {
      // TODO: fix this!
      fmt::print("no support for top level modules right now.\n");
      continue;
    }

    bool found = false;

    // can't just open an lgraph node, as different floorplan instances have different names

    for (const auto& index : nt.depth_preorder()) {  // preorder because higher level nodes are probably going to be analyzed more
                                                     // often than leaf nodes
      if (index == nt.get_root()) {
        continue;  // skip root for now
      }

      const auto& n = nt.get_data(index);

      I(!n.is_invalid());

      if (!n.has_name()) {
        error("floorplanner has not been run!");
      }

      if (n.get_name() == name) {
        found = true;

        if (var.has_label("ar") || var.has_label("ar")) {
          float ar = std::stof(var.get("ar").data());
          I(false); // TODO: write this
          fmt::print("set aspect ratio of module {} to {}.\n", n.get_name(), ar);
          break;
        }

        fmt::print("module {}\t", n.get_name());

        if (!n.has_place()) {
          fmt::print("(no area information)\n");
          break;
        } else {
          print_area(nt, index);
          if (n.is_type_sub_present()) {
            unsigned int counter = 0;
            for (auto child_idx : nt.children(index)) {
              (void)child_idx;
              counter++;
            }

            fmt::print(", {} components\n", counter);
            print_children(nt, index);
          }

          fmt::print("\n");
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
      error(errstr);
    }
  }
}

void Pass_fplan_analyzefp::pass(Eprp_var& var) { Pass_fplan_analyzefp a(var); }
