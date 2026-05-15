//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "meta_api.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#include "file_utils.hpp"
#include "graph_library.hpp"
#include "graph_library_singleton.hpp"
#include "lgraph.hpp"
#include "main_api.hpp"
#include "thread_pool.hpp"

void Meta_api::open(Eprp_var& var) {
  auto path = var.get("path");
  auto name = var.get("name");
  auto hier = var.get("hier");
  assert(!name.empty());

  auto* lib = Graph_library::instance(path);
  if (lib == nullptr) {
    Main_api::error("could not open graph library path {}", path);
    return;
  }

  Lgraph* lg = lib->open_lgraph(name);

  if (lg == nullptr) {
    Main_api::warn("lgraph.open could not find {} lgraph in {} path", name, path);
  } else {
    if (lg->is_empty()) {
      Main_api::warn("lgraph.open lgraph {} is empty!", name);
    }
    if (hier != "false" && hier != "0") {
      lg->each_hier_unique_sub_bottom_up([&var](Lgraph* g) { var.add(g); });
    }
    var.add(lg);
  }
}

void Meta_api::save(Eprp_var& var) {
  // Determine the on-disk path from any graph currently in var. The
  // hhds::GraphLibrary singleton is keyed by path; we save it directly.
  // var.graphs are populated by producers (yosys) via Eprp_var::add(shared_ptr<hhds::Graph>);
  // their owning library has the path we need.
  //
  // The legacy `path` argument is not supplied to lgraph.save in current
  // scripts; rely on the first graph's owning library to identify the path.
  auto path_arg = var.get("path");
  std::string save_path{path_arg};
  if (save_path.empty()) {
    save_path = "lgdb";  // historical default
  }

  livehd::Hhds_graph_library::save(save_path);
}

void Meta_api::create(Eprp_var& var) {
  auto path = var.get("path");
  auto name = var.get("name");
  assert(!name.empty());

  auto* lib = Graph_library::instance(path);
  if (lib == nullptr) {
    Main_api::error("lgraph.create could not graph_library in {} path", path);
    return;
  }
  Lgraph* lg = lib->open_lgraph(name);
  if (lg == nullptr) {
    Main_api::error("lgraph.create could not open {} lgraph in {} path", name, path);
    return;
  }

  var.add(lg);
}

void Meta_api::rename(Eprp_var& var) {
  auto path = var.get("path");
  auto name = var.get("name");
  auto dest = var.get("dest");
  assert(!name.empty());

  auto* glibrary = Graph_library::instance(path);
  glibrary->rename_name(name, dest);
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

void Meta_api::stats(Eprp_var& var) {
  for (const auto& lg : var.lgs) {
    assert(lg);
    lg->print_stats();
  }
}

void Meta_api::dump(Eprp_var& var) {
  std::print("dump labels:\n");
  for (const auto& l : var.dict) {
    std::print("  {}:{}\n", l.first, l.second);
  }
  std::print("dump lgraphs:\n");
  for (const auto& l : var.lgs) {
    std::print("  {}/{}\n", l->get_path(), l->get_name());
  }
  std::print("dump lnast:\n");
  for (const auto& l : var.lnasts) {
    std::print("  {}\n", l->get_top_module_name());
  }
}

void Meta_api::liberty(Eprp_var& var) {
  auto files = var.get("files");
  auto path  = var.get("path");
  std::print("lgraph.liberty path:{} ", path);
  for (const auto& f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    std::print("file:{} ", f);
  }
  std::print(" (FIXME!, NOT IMPLEMENTED)\n");
}

void Meta_api::sdc(Eprp_var& var) {
  auto files = var.get("files");
  auto path  = var.get("path");
  std::print("lgraph.sdc path:{} ", path);
  for (const auto& f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    std::print("file:{} ", f);
  }
  std::print(" (FIXME!, NOT IMPLEMENTED)\n");
}

void Meta_api::spef(Eprp_var& var) {
  auto files = var.get("files");
  auto path  = var.get("path");
  std::print("lgraph.spef path:{} ", path);
  for (const auto& f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    std::print("file:{} ", f);
  }
  std::print(" (FIXME!, NOT IMPLEMENTED)\n");
}

void Meta_api::lgdump(Eprp_var& var) {
  // auto odir = var.get("odir");
  auto hier = var.get("hier") == "true" ? true : false;
  std::print("lgraph.dump lgraphs:\n");
  for (const auto& l : var.lgs) {
    std::print("  {}/{}\n", l->get_path(), l->get_name());
    l->dump(hier);
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
  Eprp_method m1("lgraph.open", "open an lgraph if it exists", &Meta_api::open);
  m1.add_label_optional("path", "lgraph path", "lgdb");
  m1.add_label_required("name", "lgraph name");
  m1.add_label_optional("hier", "open all the subgraphs too", "false");

  eprp.register_method(m1);
  //--------

  Eprp_method m1b("lgraph.save", "save an lgraph", &Meta_api::save);
  m1b.add_label_optional("hier", "save all the subgraphs too", "false");

  eprp.register_method(m1b);

  //---------------------
  Eprp_method m2("lgraph.create", "create a new lgraph", &Meta_api::create);
  m2.add_label_optional("path", "lgraph path", "lgdb");
  m2.add_label_required("name", "lgraph name");

  eprp.register_method(m2);

  //---------------------
  Eprp_method m3("lgraph.stats", "print the stats from the passed graphs", &Meta_api::stats);

  eprp.register_method(m3);

  //---------------------
  Eprp_method m4a("lnast.dump", "round-trippable LNAST text dump (read with lnast.read)", &Meta_api::lnastdump);
  m4a.add_label_optional("file", "file to write the dump to (default: stdout)", "");
  Eprp_method m4b("lgraph.dump", "verbose lgraph dump ", &Meta_api::lgdump);
  m4b.add_label_optional("hier", "hierarchical traversal", "false");
  m4b.add_label_optional("odir", "directory to dump (not screen)", "");
  Eprp_method m4c("lnast.print", "pretty (human-only) LNAST tree print", &Meta_api::lnastprint);
  m4c.add_label_optional("file", "file to write the pretty print to (default: stdout)", "");
  Eprp_method m4d("lnast.read", "load an LNAST from a file produced by lnast.dump", &Meta_api::lnastread);
  m4d.add_label_required("file", "file produced by lnast.dump");

  eprp.register_method(m4a);
  eprp.register_method(m4b);
  eprp.register_method(m4c);
  eprp.register_method(m4d);
  //---------------------
  Eprp_method m5("dump", "dump labels and lgraphs passed", &Meta_api::dump);
  eprp.register_method(m5);

  //---------------------
  Eprp_method m6("lgraph.rename", "rename a lgraph", &Meta_api::rename);
  m6.add_label_optional("path", "lgraph path", "lgdb");
  m6.add_label_required("name", "lgraph name");
  m6.add_label_required("dest", "lgraph destination name");

  eprp.register_method(m6);

  //---------------------
  Eprp_method m7("lgraph.match", "open many lgraphs (match regex)", &Meta_api::match);
  m7.add_label_optional("path", "lgraph path", "lgdb");
  m7.add_label_optional("match", "quoted string of regex to match . E.g: match:\"\\.v$\" for verilog files.", ".*");
  eprp.register_method(m7);

  //---------------------
  Eprp_method m8("lgraph.liberty", "add liberty files to the lgraph library", &Meta_api::liberty);
  m8.add_label_required("files", "liberty files to add (comma separated)");
  m8.add_label_optional("path", "lgraph path", "lgdb");

  eprp.register_method(m8);

  //---------------------
  Eprp_method m9("lgraph.sdc", "add sdc files to the lgraph library", &Meta_api::sdc);
  m9.add_label_required("files", "sdc files to add (comma separated)");
  m9.add_label_optional("path", "lgraph path", "lgdb");

  eprp.register_method(m9);

  //---------------------
  Eprp_method m10("lgraph.spef", "add spef files to the lgraph library", &Meta_api::spef);
  m10.add_label_required("files", "spef files to add (comma separated)");
  m10.add_label_optional("path", "lgraph path", "lgdb");

  eprp.register_method(m10);
}
