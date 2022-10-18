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
  m1.add_label_optional("cprop_only", "only valid for firrtl frontend. No firmap/BW pass");
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
  auto          only_cprop = pc.check_option_cprop(var);

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
    setup_firmap_library(path);
    firrtl_compilation(var, compiler, only_cprop);
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

void Pass_compiler::firrtl_compilation(Eprp_var &var, Lcompiler &compiler, bool &only_cprop) {
  (void) only_cprop;
  compiler.do_fir_lnast2lgraph(var.lnasts);
  compiler.do_fir_cprop();
	if (!only_cprop) {
    compiler.do_fir_firbits();
    compiler.do_fir_firmap_bitwidth();
  }
}

Sub_node *Pass_compiler::setup_firmap_library_gen(Graph_library *lib, std::string_view cell_name, const std::vector<std::string> &inp, std::string_view out) {

  auto *sub = lib->ref_or_create_sub(cell_name);
  auto pos = 0;
  for(const auto &i:inp) {
    sub->add_input_pin(i, pos++);
  }
  sub->add_output_pin(out, pos);
  sub->clear_loop_last();

  return sub;
}

void Pass_compiler::setup_firmap_library(std::string_view path) {
  auto   *lib = Graph_library::instance(path);
  if (lib->exists("__fir_const"))
    return;

  auto *sub_fir_const = setup_firmap_library_gen(lib, "__fir_const", {}, "Y");
  sub_fir_const->set_loop_first();

  setup_firmap_library_gen(lib, "__fir_bits", {"e1", "e2", "e3"}, "Y");

  setup_firmap_library_gen(lib, "__fir_add" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_sub" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_mul" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_div" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_rem" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_lt"  , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_leq" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_gt"  , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_geq" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_eq"  , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_neq" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_pad" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_shl" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_shr" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_dshl", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_dshr", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_and" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_or"  , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_xor" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_cat" , {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_head", {"e1", "e2"}, "Y");
  setup_firmap_library_gen(lib, "__fir_tail", {"e1", "e2"}, "Y");

  setup_firmap_library_gen(lib, "__fir_as_uint" , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_as_sint" , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_as_clock", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_as_async", {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_cvt"     , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_neg"     , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_not"     , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_andr"    , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_orr"     , {"e1"}, "Y");
  setup_firmap_library_gen(lib, "__fir_xorr"    , {"e1"}, "Y");
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
  if (var.has_label("cprop_only")) {
    auto co = var.get("cprop_only");
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
