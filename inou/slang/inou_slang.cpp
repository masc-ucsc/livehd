//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// clang-format off

#include "absl/strings/str_split.h"

#include "slang_tree.hpp"
#include "inou_slang.hpp"

#include "lbench.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"
#include "thread_pool.hpp"

// clang-format on

extern int slang_main(int argc, char **argv, Slang_tree &tree);  // in slang_driver.cpp

static Pass_plugin sample("inou.verilog", Inou_slang::setup);

void Inou_slang::setup() {
  Eprp_method m1("inou.verilog", "System verilog to LNAST using slang", &Inou_slang::work);

  m1.add_label_required("files", "input verilog files");
  m1.add_label_optional("includes", "comma separated include paths (otherwise, verilog paths)");
  m1.add_label_optional("defines", "comma separated defines. E.g: defines:foo=1,XXX,LALA=1");
  m1.add_label_optional("undefines", "comma separated undefines");

  register_pass(m1);
}

Inou_slang::Inou_slang(const Eprp_var &var) : Pass("pass.lec", var) {}

void Inou_slang::work(Eprp_var &var) {
  TRACE_EVENT("verilog", "verilog_tolnast");
  Inou_slang p(var);

  std::vector<char *> argv;

  argv.push_back(strdup("lgshell"));

#ifdef NDEBUG
  argv.push_back(strdup("--quiet"));
#endif

  argv.push_back(strdup("--ignore-unknown-modules"));
  argv.push_back(strdup("--single-unit"));

  if (var.has_label("includes")) {
    auto txt = var.get("includes");
    for (const auto f : absl::StrSplit(txt, ',')) {
      argv.push_back(strdup("-I"));
      argv.push_back(strdup(std::string(f).c_str()));
    }
  }

  if (var.has_label("defines")) {
    auto txt = var.get("defines");
    for (const auto f : absl::StrSplit(txt, ',')) {
      argv.push_back(strdup("-D"));
      argv.push_back(strdup(std::string(f).c_str()));
    }
  }

  if (var.has_label("undefines")) {
    auto txt = var.get("undefines");
    for (const auto f : absl::StrSplit(txt, ',')) {
      argv.push_back(strdup("-U"));
      argv.push_back(strdup(std::string(f).c_str()));
    }
  }

  std::mutex              var_add_mutex;

  for (const auto f : absl::StrSplit(p.files, ',')) {
    std::string fname{f};

    thread_pool.add([=, &var, &argv, &var_add_mutex]() -> void {
      //const std::lock_guard<std::mutex> guard(var_add_mutex); // FIXME: slang multithread fails

      // TRACE_EVENT("verilog", perfetto::DynamicString{fname});
      TRACE_EVENT("verilog", nullptr, [&fname](perfetto::EventContext ctx) { 
          std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
          ctx.event()->set_name(converted_str + fname.c_str()); 
      });

      Slang_tree tree;

      std::vector<char *> argv_final{argv};

      char *ptr_fname = strdup(fname.c_str());

      argv_final.emplace_back(ptr_fname);
      argv_final.emplace_back(nullptr);

      slang_main(argv_final.size() - 1, argv_final.data(), tree);  // compile to lnasts

      {
        const std::lock_guard<std::mutex> guard(var_add_mutex);
        for (auto &ln : tree.pick_lnast()) {
          var.add(ln);
        }
      }

      free(ptr_fname);
    });
  }
  thread_pool.wait_all();

  for (char *ptr : argv) {
    if (ptr)
      free(ptr);
  }

}
