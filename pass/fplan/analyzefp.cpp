//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "analyzefp.hpp"

#include <limits>

#include "ann_place.hpp"
#include "floorplan.hpp"
#include "mmap_map.hpp"

void Pass_fplan_analyzefp::setup() {
  auto a = Eprp_method("pass.fplan.analyzefp",
                       "return information about a given floorplan within a livehd hierarchy",
                       &Pass_fplan_analyzefp::pass);

  a.add_label_required("top", "top level module in floorplan");
  a.add_label_required("nodes", "modules to analyze, or \"dump\" to dump node names");

  a.add_label_optional("hint", "set a geographical hint for the specified node", "");
  a.add_label_optional(
      "report",
      "return information about the most recent floorplan, valid options are \"regularity\", \"hpwl\", and \"all\".",
      "");

  a.add_label_optional("path",
                       "lgdb directory to analyze",
                       "lgdb");  // can't pass lgraphs because lgraph names are the same per instance

  register_pass(a);
}

std::string Pass_fplan_analyzefp::safe_name(const Node& n) const {
  return n.has_instance_name() ? std::string(n.get_instance_name()) : std::string(n.default_instance_name());
}

void Pass_fplan_analyzefp::print_area(const Node_tree& nt, const Tree_index& tidx) const {
  const auto& n = nt.get_data(tidx);
  if (!n.has_place()) {
    fmt::print("(no area information)");
    return;
  }

  const Ann_place& p = n.get_place();
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

    const std::string type = child.get_type_op() == Ntype_op::Sub ? "mod" : "node";
    fmt::print("{} {}\t", type, safe_name(child));

    print_area(nt, child_idx);
    fmt::print("\n");
  }
}

Pass_fplan_analyzefp::Pass_fplan_analyzefp(const Eprp_var& var) : Pass("pass.fplan", var) {
  Lgraph* root = Lgraph::open(path, var.get("top"));
  if (root == nullptr) {
    error("cannot find top level lgraph!");
  }

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

  if (names.size() == 1 && names[0] == "dump") {
    nt.dump();
    return;
  }

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
      if (index.is_root()) {
        continue;  // skip root for now
      }

      const auto& n = nt.get_data(index);

      I(!n.is_invalid());

      if ((n.has_instance_name() && n.get_instance_name() == name) || (n.default_instance_name() == name)) {
        found = true;

        mmap_lib::map<Node::Compact, GeographyHint> hint_map(path, "node_hints");

        std::string_view hint = var.get("hint");
        if (hint != "") {
          GeographyHint hint_enum = nameToHint(hint);
          if (hint_enum == InvalidHint) {
            error("invalid hint type!");
          }

          hint_map.set(n.get_compact(), hint_enum);
          const Tree_index parent_idx = nt.get_parent(index);
          const Node& parent = nt.get_data(parent_idx);
          if (!parent_idx.is_root() && !hint_map.has(parent.get_compact())) {
            hint_map.set(parent.get_compact(), UnknownHint);
            fmt::print("setting UnknownHint for parent {} due to child having hint\n", safe_name(parent));
          }
        }

        fmt::print("module {}\t", safe_name(n));

        if (hint_map.has(n.get_compact())) {
          fmt::print("hint: {}, ", hintToName(hint_map.get(n.get_compact())));
        }

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

        std::string_view action = var.get("report");
        if (action == "hpwl" || action == "all") {
          // TODO: write hpwl pass
        }

        // computing the "regularity" of a hierarchical design using a method presented in the HiReg paper:
        // regularity = 1 - (area only counting instances of a given Lgraph once) / (area counting all instances of an Lgraph)
        if (action == "regularity" || action == "all") {
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
