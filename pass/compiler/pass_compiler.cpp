// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_compiler.hpp"

#include <cstddef>

#include "lcompiler.hpp"
#include "str_tools.hpp"

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
  TRACE_EVENT("pass", "compile.front");
  // Lbench b("pass.compile.front");
  Pass_compiler pc(var);
  auto path       = pc.get_path(var);
  auto odir       = pc.get_odir(var);
  auto top        = pc.check_option_top(var);
  auto gviz       = pc.check_option_gviz(var);
  auto get_firrtl = pc.check_option_firrtl(var);

  Lcompiler compiler(path, odir, top, gviz);
  fmt::print("top module_name is: {}\n", top);

  if (var.lnasts.empty()) {
    auto files = pc.get_files(var);
    if (files.empty()) {
      Pass::warn("nothing to compile. no files or lnast");
      return;
    }

    for (auto f : str_tools::split(files, ',')) {
      Pass::warn("todo: start from prp parser:{}", f);
    }
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
    // google::protobuf::ShutdownProtobufLibrary();
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
  auto *lg_fir_const = Lgraph::create(lg->get_path(),"__fir_const", "-");
  lg_fir_const->add_graph_output("Y",0,0);

  auto *lg_fir_add = Lgraph::create(lg->get_path(),"__fir_add", "-");
  lg_fir_add->add_graph_input("e1",0,0);
  lg_fir_add->add_graph_input("e2",1,0);
  lg_fir_add->add_graph_output("Y",2,0);

  auto *lg_fir_sub = Lgraph::create(lg->get_path(),"__fir_sub", "-");
  lg_fir_sub->add_graph_input("e1", 0, 0);
  lg_fir_sub->add_graph_input("e2", 1, 0);
  lg_fir_sub->add_graph_output("Y", 2, 0);

  auto *lg_fir_mul = Lgraph::create(lg->get_path(),"__fir_mul", "-");
  lg_fir_mul->add_graph_input("e1", 0, 0);
  lg_fir_mul->add_graph_input("e2", 1, 0);
  lg_fir_mul->add_graph_output("Y", 2, 0);

  auto *lg_fir_div = Lgraph::create(lg->get_path(),"__fir_div", "-");
  lg_fir_div->add_graph_input("e1", 0, 0);
  lg_fir_div->add_graph_input("e2", 1, 0);
  lg_fir_div->add_graph_output("Y", 2, 0);

  auto *lg_fir_rem = Lgraph::create(lg->get_path(),"__fir_rem", "-");
  lg_fir_rem->add_graph_input("e1", 0, 0);
  lg_fir_rem->add_graph_input("e2", 1, 0);
  lg_fir_rem->add_graph_output("Y", 2, 0);

  auto *lg_fir_lt = Lgraph::create(lg->get_path(),"__fir_lt", "-");
  lg_fir_lt->add_graph_input("e1", 0, 0);
  lg_fir_lt->add_graph_input("e2", 1, 0);
  lg_fir_lt->add_graph_output("Y", 2, 0);

  auto *lg_fir_leq = Lgraph::create(lg->get_path(),"__fir_leq", "-");
  lg_fir_leq->add_graph_input("e1", 0, 0);
  lg_fir_leq->add_graph_input("e2", 1, 0);
  lg_fir_leq->add_graph_output("Y", 2, 0);

  auto *lg_fir_gt = Lgraph::create(lg->get_path(),"__fir_gt", "-");
  lg_fir_gt->add_graph_input("e1", 0, 0);
  lg_fir_gt->add_graph_input("e2", 1, 0);
  lg_fir_gt->add_graph_output("Y", 2, 0);

  auto *lg_fir_geq = Lgraph::create(lg->get_path(),"__fir_geq", "-");
  lg_fir_geq->add_graph_input("e1", 0, 0);
  lg_fir_geq->add_graph_input("e2", 1, 0);
  lg_fir_geq->add_graph_output("Y", 2, 0);

  auto *lg_fir_eq = Lgraph::create(lg->get_path(),"__fir_eq", "-");
  lg_fir_eq->add_graph_input("e1", 0, 0);
  lg_fir_eq->add_graph_input("e2", 1, 0);
  lg_fir_eq->add_graph_output("Y", 2, 0);

  auto *lg_fir_neq = Lgraph::create(lg->get_path(),"__fir_neq", "-");
  lg_fir_neq->add_graph_input("e1", 0, 0);
  lg_fir_neq->add_graph_input("e2", 1, 0);
  lg_fir_neq->add_graph_output("Y", 2, 0);

  auto *lg_fir_pad = Lgraph::create(lg->get_path(),"__fir_pad", "-");
  lg_fir_pad->add_graph_input("e1", 0, 0);
  lg_fir_pad->add_graph_input("e2", 1, 0);
  lg_fir_pad->add_graph_output("Y", 2, 0);

  auto *lg_fir_as_uint = Lgraph::create(lg->get_path(),"__fir_as_uint", "-");
  lg_fir_as_uint->add_graph_input("e1", 0, 0);
  lg_fir_as_uint->add_graph_output("Y", 2, 0);

  auto *lg_fir_as_sint = Lgraph::create(lg->get_path(),"__fir_as_sint", "-");
  lg_fir_as_sint->add_graph_input("e1", 0, 0);
  lg_fir_as_sint->add_graph_output("Y", 2, 0);

  auto *lg_fir_as_clock = Lgraph::create(lg->get_path(),"__fir_as_clock", "-");
  lg_fir_as_clock->add_graph_input("e1", 0, 0);
  lg_fir_as_clock->add_graph_output("Y", 2, 0);

  auto *lg_fir_as_async = Lgraph::create(lg->get_path(),"__fir_as_async", "-");
  lg_fir_as_async->add_graph_input("e1", 0, 0);
  lg_fir_as_async->add_graph_output("Y", 2, 0);

  auto *lg_fir_shl = Lgraph::create(lg->get_path(),"__fir_shl", "-");
  lg_fir_shl->add_graph_input("e1", 0, 0);
  lg_fir_shl->add_graph_input("e2", 1, 0);
  lg_fir_shl->add_graph_output("Y", 2, 0);

  auto *lg_fir_shr = Lgraph::create(lg->get_path(),"__fir_shr", "-");
  lg_fir_shr->add_graph_input("e1", 0, 0);
  lg_fir_shr->add_graph_input("e2", 1, 0);
  lg_fir_shr->add_graph_output("Y", 2, 0);

  auto *lg_fir_dshl = Lgraph::create(lg->get_path(),"__fir_dshl", "-");
  lg_fir_dshl->add_graph_input("e1", 0, 0);
  lg_fir_dshl->add_graph_input("e2", 1, 0);
  lg_fir_dshl->add_graph_output("Y", 2, 0);

  auto *lg_fir_dshr = Lgraph::create(lg->get_path(),"__fir_dshr", "-");
  lg_fir_dshr->add_graph_input("e1", 0, 0);
  lg_fir_dshr->add_graph_input("e2", 1, 0);
  lg_fir_dshr->add_graph_output("Y", 2, 0);

  auto *lg_fir_cvt = Lgraph::create(lg->get_path(),"__fir_cvt", "-");
  lg_fir_cvt->add_graph_input("e1", 0, 0);
  lg_fir_cvt->add_graph_output("Y", 2, 0);

  auto *lg_fir_neg = Lgraph::create(lg->get_path(),"__fir_neg", "-");
  lg_fir_neg->add_graph_input("e1", 0, 0);
  lg_fir_neg->add_graph_output("Y", 2, 0);

  auto *lg_fir_not = Lgraph::create(lg->get_path(),"__fir_not", "-");
  lg_fir_not->add_graph_input("e1", 0, 0);
  lg_fir_not->add_graph_output("Y", 2, 0);

  auto *lg_fir_and = Lgraph::create(lg->get_path(),"__fir_and", "-");
  lg_fir_and->add_graph_input("e1", 0, 0);
  lg_fir_and->add_graph_input("e2", 1, 0);
  lg_fir_and->add_graph_output("Y", 2, 0);

  auto *lg_fir_or = Lgraph::create(lg->get_path(),"__fir_or", "-");
  lg_fir_or->add_graph_input("e1", 0, 0);
  lg_fir_or->add_graph_input("e2", 1, 0);
  lg_fir_or->add_graph_output("Y", 2, 0);

  auto *lg_fir_xor = Lgraph::create(lg->get_path(),"__fir_xor", "-");
  lg_fir_xor->add_graph_input("e1", 0, 0);
  lg_fir_xor->add_graph_input("e2", 1, 0);
  lg_fir_xor->add_graph_output("Y", 2, 0);

  auto *lg_fir_andr = Lgraph::create(lg->get_path(),"__fir_andr", "-");
  lg_fir_andr->add_graph_input("e1", 0, 0);
  lg_fir_andr->add_graph_output("Y", 2, 0);

  auto *lg_fir_orr = Lgraph::create(lg->get_path(),"__fir_orr", "-");
  lg_fir_orr->add_graph_input("e1", 0, 0);
  lg_fir_orr->add_graph_output("Y", 2, 0);

  auto *lg_fir_xorr = Lgraph::create(lg->get_path(),"__fir_xorr", "-");
  lg_fir_xorr->add_graph_input("e1", 0, 0);
  lg_fir_xorr->add_graph_output("Y", 2, 0);

  auto *lg_fir_cat = Lgraph::create(lg->get_path(),"__fir_cat", "-");
  lg_fir_cat->add_graph_input("e1", 0, 0);
  lg_fir_cat->add_graph_input("e2", 1, 0);
  lg_fir_cat->add_graph_output("Y", 2, 0);

  auto *lg_fir_bits = Lgraph::create(lg->get_path(),"__fir_bits", "-");
  lg_fir_bits->add_graph_input("e1", 0, 0);
  lg_fir_bits->add_graph_input("e2", 1, 0);  // hi
  lg_fir_bits->add_graph_input("e3", 2, 0);  // lo
  lg_fir_bits->add_graph_output("Y", 3, 0);

  auto *lg_fir_head = Lgraph::create(lg->get_path(),"__fir_head", "-");
  lg_fir_head->add_graph_input("e1", 0, 0);
  lg_fir_head->add_graph_input("e2", 1, 0);
  lg_fir_head->add_graph_output("Y", 2, 0);

  auto *lg_fir_tail = Lgraph::create(lg->get_path(),"__fir_tail", "-");
  lg_fir_tail->add_graph_input("e1", 0, 0);
  lg_fir_tail->add_graph_input("e2", 1, 0);
  lg_fir_tail->add_graph_output("Y", 2, 0);

  // lg->ref_library()->sync();
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

