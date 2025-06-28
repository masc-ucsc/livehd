//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "meta_api.hpp"

#include <format>
#include <iostream>
#include <regex>
#include <string>

#include "file_utils.hpp"
#include "graph_library.hpp"
#include "lgraph.hpp"
#include "main_api.hpp"
#include "thread_pool.hpp"

void Meta_api::open(Eprp_var &var) {
  auto path = var.get("path");
  auto name = var.get("name");
  auto hier = var.get("hier");
  assert(!name.empty());

  auto *lib = Graph_library::instance(path);
  if (lib == nullptr) {
    Main_api::error("could not open graph library path {}", path);
    return;
  }

  Lgraph *lg = lib->open_lgraph(name);

  if (lg == nullptr) {
    Main_api::warn("lgraph.open could not find {} lgraph in {} path", name, path);
  } else {
    if (lg->is_empty()) {
      Main_api::warn("lgraph.open lgraph {} is empty!", name);
    }
    if (hier != "false" && hier != "0") {
      lg->each_hier_unique_sub_bottom_up([&var](Lgraph *g) { var.add(g); });
    }
    var.add(lg);
  }
}

void Meta_api::save(Eprp_var &var) {
  auto hier = var.get("hier");

  for (Lgraph *lg : var.lgs) {
    if (lg->is_empty()) {
      file_utils::clean_dir(lg->get_save_filename());
    } else {
      if (hier != "false" && hier != "0") {
        // lg->each_hier_unique_sub_bottom_up([&var](Lgraph *g) { g->save(); });
        lg->each_hier_unique_sub_bottom_up([](Lgraph *g) { thread_pool.add([g]() -> void { g->save(); }); });
      } else {
        thread_pool.add([lg]() -> void { lg->save(); });
      }
    }
  }

  Graph_library::sync_all();  // nice but not needed
}

void Meta_api::create(Eprp_var &var) {
  auto path = var.get("path");
  auto name = var.get("name");
  assert(!name.empty());

  auto *lib = Graph_library::instance(path);
  if (lib == nullptr) {
    Main_api::error("lgraph.create could not graph_library in {} path", path);
    return;
  }
  Lgraph *lg = lib->open_lgraph(name);
  if (lg == nullptr) {
    Main_api::error("lgraph.create could not open {} lgraph in {} path", name, path);
    return;
  }

  var.add(lg);
}

void Meta_api::rename(Eprp_var &var) {
  auto path = var.get("path");
  auto name = var.get("name");
  auto dest = var.get("dest");
  assert(!name.empty());

  auto *glibrary = Graph_library::instance(path);
  glibrary->rename_name(name, dest);
}

void Meta_api::copy(Eprp_var &var) {
  auto path = var.get("path");
  auto name = var.get("name");
  auto dest = var.get("dest");
  assert(!name.empty());

  auto *glibrary = Graph_library::instance(path);

  glibrary->copy_lgraph(name, dest);
}

void Meta_api::match(Eprp_var &var) {
  auto path  = var.get("path");
  auto match = var.get("match");

  auto *library = Graph_library::instance(path);
  if (library == nullptr) {
    Main_api::warn("lgraph.match could not open {} path", path);
    return;
  }

  std::vector<Lgraph *> lgs;

  try {
    library->each_sub(match, [&lgs, library](Lg_type_id lgid, std::string_view name) {
      (void)lgid;

      auto *lg = library->open_lgraph(name);
      if (lg) {
        if (lg->is_empty()) {
          std::cout << std::format("lgraph.match lgraph {} is empty\n", name);
        }
        lgs.push_back(lg);
      }
    });
  } catch (const std::regex_error &e) {
    Main_api::error("invalid match:{} regex. It is a FULL regex unlike bash. To test, try: `ls path | grep -E \"match\"`", match);
  }

  for (Lgraph *lg : lgs) {
    var.add(lg);
  }
}

void Meta_api::stats(Eprp_var &var) {
  for (const auto &lg : var.lgs) {
    assert(lg);
    lg->print_stats();
  }
}

void Meta_api::dump(Eprp_var &var) {
  std::cout << std::format("dump labels:\n");
  for (const auto &l : var.dict) {
    std::cout << std::format("  {}:{}\n", l.first, l.second);
  }
  std::cout << std::format("dump lgraphs:\n");
  for (const auto &l : var.lgs) {
    std::cout << std::format("  {}/{}\n", l->get_path(), l->get_name());
  }
  std::cout << std::format("dump lnast:\n");
  for (const auto &l : var.lnasts) {
    std::cout << std::format("  {}\n", l->get_top_module_name());
  }
}

void Meta_api::liberty(Eprp_var &var) {
  auto files = var.get("files");
  auto path  = var.get("path");
  std::cout << std::format("lgraph.liberty path:{} ", path);
  for (const auto &f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    std::cout << std::format("file:{} ", f);
  }
  std::cout << std::format(" (FIXME!, NOT IMPLEMENTED)\n");
}

void Meta_api::sdc(Eprp_var &var) {
  auto files = var.get("files");
  auto path  = var.get("path");
  std::cout << std::format("lgraph.sdc path:{} ", path);
  for (const auto &f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    std::cout << std::format("file:{} ", f);
  }
  std::cout << std::format(" (FIXME!, NOT IMPLEMENTED)\n");
}

void Meta_api::spef(Eprp_var &var) {
  auto files = var.get("files");
  auto path  = var.get("path");
  std::cout << std::format("lgraph.spef path:{} ", path);
  for (const auto &f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    std::cout << std::format("file:{} ", f);
  }
  std::cout << std::format(" (FIXME!, NOT IMPLEMENTED)\n");
}

void Meta_api::lgdump(Eprp_var &var) {
  // auto odir = var.get("odir");
  auto hier = var.get("hier") == "true" ? true : false;
  std::cout << std::format("lgraph.dump lgraphs:\n");
  for (const auto &l : var.lgs) {
    std::cout << std::format("  {}/{}\n", l->get_path(), l->get_name());
    l->dump(hier);
  }
}

void Meta_api::lnastdump(Eprp_var &var) {
  std::cout << std::format("lnast.dump lnast:\n");
  for (const auto &l : var.lnasts) {
    std::cout << std::format("  {}\n", l->get_top_module_name());
    l->dump();
  }
}

void Meta_api::setup(Eprp &eprp) {
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
  Eprp_method m4a("lnast.dump", "verbose LNAST dump ", &Meta_api::lnastdump);
  Eprp_method m4b("lgraph.dump", "verbose lgraph dump ", &Meta_api::lgdump);
  m4b.add_label_optional("hier", "hierarchical traversal", "false");
  m4b.add_label_optional("odir", "directory to dump (not screen)", "");

  eprp.register_method(m4a);
  eprp.register_method(m4b);
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

  //---------------------
  Eprp_method m11("lgraph.copy", "copy a lgraph", &Meta_api::copy);
  m11.add_label_optional("path", "lgraph path", "lgdb");
  m11.add_label_required("name", "lgraph name");
  m11.add_label_required("dest", "lgraph destination name");

  eprp.register_method(m11);
}
