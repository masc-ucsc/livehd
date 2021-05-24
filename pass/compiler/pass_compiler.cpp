// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_compiler.hpp"

#include <cstddef>

#include "lcompiler.hpp"

static Pass_plugin sample("pass_compiler", Pass_compiler::setup);

void Pass_compiler::setup() {
  Eprp_method m1("pass.compiler", "LiveHD multi-HDLs compilation, default language: Pyrope", &Pass_compiler::compile);
  m1.add_label_optional("path", "lgraph path", "lgdb");
  m1.add_label_optional("files", "files to process (comma separated)");
  m1.add_label_optional("firrtl", "is firrtl front-end");
  m1.add_label_optional("top", "specify the top module");
  m1.add_label_optional("odir", "output directory", ".");
  m1.add_label_optional("gviz", "dump graphviz");

  register_pass(m1);
}

Pass_compiler::Pass_compiler(const Eprp_var &var) : Pass("pass.compiler", var) {}

void Pass_compiler::compile(Eprp_var &var) {
  // Lbench b("pass.compile.front");
  Pass_compiler    pc(var);
  auto             path       = pc.get_path(var);
  auto             odir       = pc.get_odir(var);
  auto             top        = pc.check_option_top(var);
  auto             gviz       = pc.check_option_gviz(var);
  std::string_view get_firrtl = pc.check_option_firrtl(var);

  Lcompiler compiler(path, odir, top, gviz);
  fmt::print("top module_name is: {}\n", top);

  if (var.lnasts.empty()) {
    auto files = pc.get_files(var);
    if (files.empty()) {
      Pass::warn("nothing to compile. no files or lnast");
      return;
    }

    for (auto f : absl::StrSplit(files, ',')) Pass::warn("todo: start from prp parser:{}", f);
  }

  if (!get_firrtl.empty()) {
    I(top != "", "firrtl front-end must specify the top firrtl name!");
    Lgraph *seed_lg;
    auto *  library = Graph_library::instance(path);
    if (!library->exists(path, "__firop_seed")) {
      seed_lg = Lgraph::create(path, "__firop_seed", "-");
      setup_firmap_library(seed_lg);
    }
    firrtl_compilation(var, compiler);
  } else {
    pyrope_compilation(var, compiler);
  }

  auto lgs = compiler.wait_all();
  var.add(lgs);
  return;
}

void Pass_compiler::pyrope_compilation(Eprp_var &var, Lcompiler &compiler) {
  compiler.do_prp_lnast2lgraph(var.lnasts);
  compiler.do_prp_local_cprop_bitwidth();
  compiler.do_prp_global_bitwidth_inference();
}

void Pass_compiler::firrtl_compilation(Eprp_var &var, Lcompiler &compiler) {
  compiler.do_fir_lnast2lgraph(var.lnasts);
  compiler.do_fir_cprop();
  compiler.do_fir_firbits();
  compiler.do_fir_firmap_bitwidth();
}

void Pass_compiler::setup_firmap_library(Lgraph *lg) {
  auto &lg_fir_const = lg->ref_library()->setup_sub("__fir_const", "-");
  lg_fir_const.add_output_pin("Y");

  auto &lg_fir_add = lg->ref_library()->setup_sub("__fir_add", "-");
  lg_fir_add.add_input_pin("e1");
  lg_fir_add.add_input_pin("e2");
  lg_fir_add.add_output_pin("Y");

  auto &lg_fir_sub = lg->ref_library()->setup_sub("__fir_sub", "-");
  lg_fir_sub.add_input_pin("e1");
  lg_fir_sub.add_input_pin("e2");
  lg_fir_sub.add_output_pin("Y");

  auto &lg_fir_mul = lg->ref_library()->setup_sub("__fir_mul", "-");
  lg_fir_mul.add_input_pin("e1");
  lg_fir_mul.add_input_pin("e2");
  lg_fir_mul.add_output_pin("Y");

  auto &lg_fir_div = lg->ref_library()->setup_sub("__fir_div", "-");
  lg_fir_div.add_input_pin("e1");
  lg_fir_div.add_input_pin("e2");
  lg_fir_div.add_output_pin("Y");

  auto &lg_fir_rem = lg->ref_library()->setup_sub("__fir_rem", "-");
  lg_fir_rem.add_input_pin("e1");
  lg_fir_rem.add_input_pin("e2");
  lg_fir_rem.add_output_pin("Y");

  auto &lg_fir_lt = lg->ref_library()->setup_sub("__fir_lt", "-");
  lg_fir_lt.add_input_pin("e1");
  lg_fir_lt.add_input_pin("e2");
  lg_fir_lt.add_output_pin("Y");

  auto &lg_fir_leq = lg->ref_library()->setup_sub("__fir_leq", "-");
  lg_fir_leq.add_input_pin("e1");
  lg_fir_leq.add_input_pin("e2");
  lg_fir_leq.add_output_pin("Y");

  auto &lg_fir_gt = lg->ref_library()->setup_sub("__fir_gt", "-");
  lg_fir_gt.add_input_pin("e1");
  lg_fir_gt.add_input_pin("e2");
  lg_fir_gt.add_output_pin("Y");

  auto &lg_fir_geq = lg->ref_library()->setup_sub("__fir_geq", "-");
  lg_fir_geq.add_input_pin("e1");
  lg_fir_geq.add_input_pin("e2");
  lg_fir_geq.add_output_pin("Y");

  auto &lg_fir_eq = lg->ref_library()->setup_sub("__fir_eq", "-");
  lg_fir_eq.add_input_pin("e1");
  lg_fir_eq.add_input_pin("e2");
  lg_fir_eq.add_output_pin("Y");

  auto &lg_fir_neq = lg->ref_library()->setup_sub("__fir_neq", "-");
  lg_fir_neq.add_input_pin("e1");
  lg_fir_neq.add_input_pin("e2");
  lg_fir_neq.add_output_pin("Y");

  auto &lg_fir_pad = lg->ref_library()->setup_sub("__fir_pad", "-");
  lg_fir_pad.add_input_pin("e1");
  lg_fir_pad.add_input_pin("e2");
  lg_fir_pad.add_output_pin("Y");

  auto &lg_fir_as_uint = lg->ref_library()->setup_sub("__fir_as_uint", "-");
  lg_fir_as_uint.add_input_pin("e1");
  lg_fir_as_uint.add_output_pin("Y");

  auto &lg_fir_as_sint = lg->ref_library()->setup_sub("__fir_as_sint", "-");
  lg_fir_as_sint.add_input_pin("e1");
  lg_fir_as_sint.add_output_pin("Y");

  auto &lg_fir_as_clock = lg->ref_library()->setup_sub("__fir_as_clock", "-");
  lg_fir_as_clock.add_input_pin("e1");
  lg_fir_as_clock.add_output_pin("Y");

  auto &lg_fir_as_async = lg->ref_library()->setup_sub("__fir_as_async", "-");
  lg_fir_as_async.add_input_pin("e1");
  lg_fir_as_async.add_output_pin("Y");

  auto &lg_fir_shl = lg->ref_library()->setup_sub("__fir_shl", "-");
  lg_fir_shl.add_input_pin("e1");
  lg_fir_shl.add_input_pin("e2");
  lg_fir_shl.add_output_pin("Y");

  auto &lg_fir_shr = lg->ref_library()->setup_sub("__fir_shr", "-");
  lg_fir_shr.add_input_pin("e1");
  lg_fir_shr.add_input_pin("e2");
  lg_fir_shr.add_output_pin("Y");

  auto &lg_fir_dshl = lg->ref_library()->setup_sub("__fir_dshl", "-");
  lg_fir_dshl.add_input_pin("e1");
  lg_fir_dshl.add_input_pin("e2");
  lg_fir_dshl.add_output_pin("Y");

  auto &lg_fir_dshr = lg->ref_library()->setup_sub("__fir_dshr", "-");
  lg_fir_dshr.add_input_pin("e1");
  lg_fir_dshr.add_input_pin("e2");
  lg_fir_dshr.add_output_pin("Y");

  auto &lg_fir_cvt = lg->ref_library()->setup_sub("__fir_cvt", "-");
  lg_fir_cvt.add_input_pin("e1");
  lg_fir_cvt.add_output_pin("Y");

  auto &lg_fir_neg = lg->ref_library()->setup_sub("__fir_neg", "-");
  lg_fir_neg.add_input_pin("e1");
  lg_fir_neg.add_output_pin("Y");

  auto &lg_fir_not = lg->ref_library()->setup_sub("__fir_not", "-");
  lg_fir_not.add_input_pin("e1");
  lg_fir_not.add_output_pin("Y");

  auto &lg_fir_and = lg->ref_library()->setup_sub("__fir_and", "-");
  lg_fir_and.add_input_pin("e1");
  lg_fir_and.add_input_pin("e2");
  lg_fir_and.add_output_pin("Y");

  auto &lg_fir_or = lg->ref_library()->setup_sub("__fir_or", "-");
  lg_fir_or.add_input_pin("e1");
  lg_fir_or.add_input_pin("e2");
  lg_fir_or.add_output_pin("Y");

  auto &lg_fir_xor = lg->ref_library()->setup_sub("__fir_xor", "-");
  lg_fir_xor.add_input_pin("e1");
  lg_fir_xor.add_input_pin("e2");
  lg_fir_xor.add_output_pin("Y");

  auto &lg_fir_andr = lg->ref_library()->setup_sub("__fir_andr", "-");
  lg_fir_andr.add_input_pin("e1");
  lg_fir_andr.add_output_pin("Y");

  auto &lg_fir_orr = lg->ref_library()->setup_sub("__fir_orr", "-");
  lg_fir_orr.add_input_pin("e1");
  lg_fir_orr.add_output_pin("Y");

  auto &lg_fir_xorr = lg->ref_library()->setup_sub("__fir_xorr", "-");
  lg_fir_xorr.add_input_pin("e1");
  lg_fir_xorr.add_output_pin("Y");

  auto &lg_fir_cat = lg->ref_library()->setup_sub("__fir_cat", "-");
  lg_fir_cat.add_input_pin("e1");
  lg_fir_cat.add_input_pin("e2");
  lg_fir_cat.add_output_pin("Y");

  auto &lg_fir_bits = lg->ref_library()->setup_sub("__fir_bits", "-");
  lg_fir_bits.add_input_pin("e1");
  lg_fir_bits.add_input_pin("e2");  // hi
  lg_fir_bits.add_input_pin("e3");  // lo
  lg_fir_bits.add_output_pin("Y");

  auto &lg_fir_head = lg->ref_library()->setup_sub("__fir_head", "-");
  lg_fir_head.add_input_pin("e1");
  lg_fir_head.add_input_pin("e2");
  lg_fir_head.add_output_pin("Y");

  auto &lg_fir_tail = lg->ref_library()->setup_sub("__fir_tail", "-");
  lg_fir_tail.add_input_pin("e1");
  lg_fir_tail.add_input_pin("e2");
  lg_fir_tail.add_output_pin("Y");

  lg->ref_library()->sync();
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

std::string Pass_compiler::check_option_top(Eprp_var &var) {
  std::string top;
  if (var.has_label("top")) {
    top = var.get("top");
  }
  return top;
}
std::string_view Pass_compiler::check_option_firrtl(Eprp_var &var) {
  std::string_view get_firrtl;
  if (var.has_label("firrtl")) {
    auto fir = var.get("firrtl");
    if (fir.compare("false") != 0 && fir.compare("0") != 0) {
      get_firrtl = fir;
    } else {
      get_firrtl = "";
    }
  } else {
    get_firrtl = "";
  }
  return get_firrtl;
}
