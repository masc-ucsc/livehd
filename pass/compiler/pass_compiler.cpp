// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_compiler.hpp"

#include <cstddef>

#include "absl/strings/str_split.h"
#include "lcompiler.hpp"
#include "str_tools.hpp"

static Pass_plugin sample("pass_compiler", Pass_compiler::setup);

void Pass_compiler::setup() {
  Eprp_method m1("pass.compiler", "LiveHD multi-HDLs compilation, default language: Pyrope", &Pass_compiler::compile);
  m1.add_label_optional("path", "lgraph path", "lgdb");
  m1.add_label_optional("files", "files to process (comma separated)");
  m1.add_label_optional("firrtl", "is firrtl front-end");
  m1.add_label_optional("cprop_tup_only", "only valid for firrtl frontend. No firmap/BW pass. No scalar_pass in cprop.");
  m1.add_label_optional("top", "specify the top module");
  m1.add_label_optional("odir", "output directory", ".");
  m1.add_label_optional("gviz", "dump graphviz");

  register_pass(m1);
}

Pass_compiler::Pass_compiler(const Eprp_var &var) : Pass("pass.compiler", var) {}

void Pass_compiler::compile(Eprp_var &var) {
  // TRACE_EVENT("pass", "pass.compile");

  Pass_compiler pc(var);
  auto          path       = pc.get_path(var);
  auto          odir       = pc.get_odir(var);
  auto          top        = pc.check_option_top(var);
  auto          gviz       = pc.check_option_gviz(var);
  auto          get_firrtl = pc.check_option_firrtl(var);
  auto          only_tup_cprop = pc.check_option_cprop(var);

  Lcompiler compiler(path, odir, top, gviz);
  fmt::print("top module_name is: {}\n", top);

  if (var.lnasts.empty()) {
    auto files = pc.get_files(var);
    if (files.empty()) {
      Pass::warn("nothing to compile. no files or lnast");
      return;
    }

    for (auto f : absl::StrSplit(files, ',')) {
      Pass::warn("todo: start from prp parser:{}", f);
    }
  }

  if (!get_firrtl.empty()) {
    I(top != "", "firrtl front-end must specify the top firrtl name!");
    firrtl_compilation(var, compiler, only_tup_cprop);
    // google::protobuf::ShutdownProtobufLibrary();
  } else {
    pyrope_compilation(var, compiler);
  }

  auto lgs = compiler.wait_all();
  var.add(lgs);
}

void Pass_compiler::pyrope_compilation(Eprp_var &var, Lcompiler &compiler) {
  compiler.do_prp_lnast2lgraph(var.lnasts);
  compiler.do_prp_local_cprop_bitwidth();
  // compiler.do_prp_global_bitwidth_inference();
}

void Pass_compiler::firrtl_compilation(Eprp_var &var, Lcompiler &compiler, bool &only_tup_cprop) {
  (void)only_tup_cprop;
  compiler.do_fir_lnast2lgraph(var.lnasts);
	compiler.do_fir_cprop(only_tup_cprop);
  if (!only_tup_cprop) {
    compiler.do_fir_firbits();
    compiler.do_fir_firmap_bitwidth();
  }
}

bool Pass_compiler::check_option_gviz(Eprp_var &var) {
  bool gviz_en;
  if (var.has_label("gviz")) {
    auto gv = var.get("gviz");
    gviz_en = gv != "false" && gv != "0";
  } else {
    gviz_en = false;
  }
  return gviz_en;
}

bool Pass_compiler::check_option_cprop(Eprp_var &var) {
  bool is_cprop_only;
  if (var.has_label("cprop_tup_only")) {
    auto co       = var.get("cprop_tup_only");
    is_cprop_only = co != "false" && co != "0";
  } else {
    is_cprop_only = false;
  }
  return is_cprop_only;
}

std::string_view Pass_compiler::check_option_top(Eprp_var &var) {
  if (var.has_label("top")) {
    return var.get("top");
  }
  return "";
}

std::string_view Pass_compiler::check_option_firrtl(Eprp_var &var) {
  std::string_view get_firrtl;
  if (var.has_label("firrtl")) {
    auto fir = var.get("firrtl");
    if (fir != "false" && fir != "0") {
      get_firrtl = fir;
    } else {
      get_firrtl = "";
    }
  } else {
    get_firrtl = "";
  }

  return get_firrtl;
}
