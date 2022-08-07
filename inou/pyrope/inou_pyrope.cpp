//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_pyrope.hpp"

#include "absl/strings/str_split.h"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"
#include "prp_lnast.hpp"
#include "thread_pool.hpp"

void setup_inou_pyrope() { Inou_pyrope::setup(); }

void Inou_pyrope::setup() {
  Eprp_method m1("inou.pyrope", "Parse the input file and convert to an LNAST", &Inou_pyrope::parse_to_lnast);
  m1.add_label_required("files", "pyrope files to process (comma separated)");

  register_pass(m1);
}

Inou_pyrope::Inou_pyrope(const Eprp_var &var) : Pass("inou.pyrope", var) {}

void Inou_pyrope::parse_to_lnast(Eprp_var &var) {
  Inou_pyrope p(var);

  std::mutex              var_add_mutex;

  for (auto f : absl::StrSplit(p.files, ',')) {

    std::string fname{f};

    thread_pool.add([&var, &var_add_mutex, fname]() -> void {
      TRACE_EVENT("pyrope", perfetto::DynamicString{fname});

      Prp_lnast converter;
      converter.parse_file(fname);

      auto basename       = str_tools::get_str_after_last_if_exists(fname, '/');
      auto basename_noext = str_tools::get_str_before_first(basename, '.');

      auto lnast = converter.prp_ast_to_lnast(basename_noext);
      {
        const std::lock_guard<std::mutex> guard(var_add_mutex);
        var.add(std::move(lnast));
      }
    });
  }

  thread_pool.wait_all();
}
