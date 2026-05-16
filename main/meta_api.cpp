//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "meta_api.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "main_api.hpp"
#include "thread_pool.hpp"

void Meta_api::save(Eprp_var& var) {
  // The hhds::GraphLibrary singleton is keyed by path; we save it directly.
  // The legacy `path` argument is not supplied to lgraph.save in current
  // scripts; default to `lgdb` to match historical behaviour.
  auto        path_arg = var.get("path");
  std::string save_path{path_arg};
  if (save_path.empty()) {
    save_path = "lgdb";
  }

  livehd::Hhds_graph_library::save(save_path);
}

void Meta_api::match(Eprp_var& var) {
  auto path  = var.get("path");
  auto match = var.get("match");

  auto& lib = livehd::Hhds_graph_library::instance(path);

  // Walk all live graphs and push the ones matching the regex into var.graphs.
  // `match` may be empty (match all) or a full regex.
  std::regex re;
  bool       has_re = !match.empty();
  if (has_re) {
    try {
      re = std::regex(std::string{match});
    } catch (const std::regex_error&) {
      Main_api::error(
          "invalid match:{} regex. It is a FULL regex unlike bash. To test, try: `ls path | grep -E \"match\"`", match);
      return;
    }
  }

  hhds::Gid cap = static_cast<hhds::Gid>(lib.capacity());
  for (hhds::Gid id = 1; id < cap; ++id) {
    if (!lib.has_graph(id)) {
      continue;
    }
    auto g = lib.get_graph(id);
    if (!g) {
      continue;
    }
    auto gio = g->get_io();
    if (!gio) {
      continue;
    }
    std::string name{gio->get_name()};
    if (has_re && !std::regex_match(name, re)) {
      continue;
    }
    var.add(g);
  }
}

// Eprp labels propagate through `|>` pipes, so a `file:foo` left in the var
// by an upstream stage would silently steer the next lnast.{print,dump,read}
// to that same path. Each of these helpers consumes its own `file` label
// after using it so the label does not leak across the pipe.

void Meta_api::lnastdump(Eprp_var& var) {
  auto file = std::string{var.get("file")};
  var.delete_label("file");
  if (file.empty()) {
    std::print("lnast.dump lnast:\n");
    for (const auto& l : var.lnasts) {
      std::print("  {}\n", l->get_top_module_name());
      l->dump(std::cout);
    }
  } else {
    std::ofstream ofs(file);
    if (!ofs.is_open()) {
      Main_api::error("lnast.dump could not open {} for writing", file);
      return;
    }
    for (const auto& l : var.lnasts) {
      l->dump(ofs);
    }
  }
}

void Meta_api::lnastprint(Eprp_var& var) {
  auto file = std::string{var.get("file")};
  var.delete_label("file");
  if (file.empty()) {
    std::print("lnast.print lnast:\n");
    for (const auto& l : var.lnasts) {
      std::print("  {}\n", l->get_top_module_name());
      l->print(std::cout);
    }
  } else {
    std::ofstream ofs(file);
    if (!ofs.is_open()) {
      Main_api::error("lnast.print could not open {} for writing", file);
      return;
    }
    for (const auto& l : var.lnasts) {
      l->print(ofs);
    }
  }
}

void Meta_api::lnastread(Eprp_var& var) {
  auto file = std::string{var.get("file")};
  var.delete_label("file");
  if (file.empty()) {
    Main_api::error("lnast.read requires file:<path>");
    return;
  }
  std::ifstream ifs(file);
  if (!ifs.is_open()) {
    Main_api::error("lnast.read could not open {} for reading", file);
    return;
  }
  for (auto& lnast : Lnast::read_all(ifs)) {
    if (lnast) {
      var.add(lnast);
    }
  }
}

void Meta_api::setup(Eprp& eprp) {
  Eprp_method m1b("lgraph.save", "save an lgraph", &Meta_api::save);
  m1b.add_label_optional("path", "lgraph path", "lgdb");
  m1b.add_label_optional("hier", "save all the subgraphs too", "false");
  eprp.register_method(m1b);

  Eprp_method m4a("lnast.dump", "round-trippable LNAST text dump (read with lnast.read)", &Meta_api::lnastdump);
  m4a.add_label_optional("file", "file to write the dump to (default: stdout)", "");
  Eprp_method m4c("lnast.print", "pretty (human-only) LNAST tree print", &Meta_api::lnastprint);
  m4c.add_label_optional("file", "file to write the pretty print to (default: stdout)", "");
  Eprp_method m4d("lnast.read", "load an LNAST from a file produced by lnast.dump", &Meta_api::lnastread);
  m4d.add_label_required("file", "file produced by lnast.dump");

  eprp.register_method(m4a);
  eprp.register_method(m4c);
  eprp.register_method(m4d);

  Eprp_method m7("lgraph.match", "open many lgraphs (match regex)", &Meta_api::match);
  m7.add_label_optional("path", "lgraph path", "lgdb");
  m7.add_label_optional("match", "quoted string of regex to match . E.g: match:\"\\.v$\" for verilog files.", ".*");
  eprp.register_method(m7);
}
