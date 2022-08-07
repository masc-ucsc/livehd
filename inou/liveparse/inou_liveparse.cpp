//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inou_liveparse.hpp"

#include "absl/strings/str_split.h"
#include "chunkify_verilog.hpp"
#include "str_tools.hpp"

void setup_inou_liveparse() { Inou_liveparse::setup(); }

void Inou_liveparse::setup() {
  Eprp_method m1("inou.liveparse", "liveparse and chunkify verilog/pyrope files", &Inou_liveparse::tolg);

  m1.add_label_optional("incr", "Incremental mode", "false");

  register_inou("liveparse", m1);
}

Inou_liveparse::Inou_liveparse(const Eprp_var &var) : Pass("inou.liveparse", var) {}

void Inou_liveparse::do_tolg(Eprp_var &var) {

  bool incremental_mode = false;

  if (var.has_label("incr")) {
    auto l = var.get("incr");
    if (l != "false") {
      incremental_mode = true;
    }
  }

  Chunkify_verilog chunker_v(path, incremental_mode);

  for (const auto &f : absl::StrSplit(files, ',')) {
    if (str_tools::ends_with(f, ".v") || str_tools::ends_with(f, ".sv")) {
      chunker_v.parse_file(f);
    } else if (str_tools::ends_with(f, ".prp")) {
      error("inou.liveparse chunkify Pyrope not implemented yet {}", f);
      return;
    } else {
      error("inou.liveparse chunkify unrecognized file format {}", f);
      return;
    }
  }

  std::string files2;
  for(const auto &f:chunker_v.get_generated_files()) {
    if (files2.empty())
      files2 = f;
    else
      absl::StrAppend(&files2, ",", f);
  }

  if (files2.empty()) {
    var.delete_label("files");
  }else{
    var.add("files", files2);
  }
}

void Inou_liveparse::tolg(Eprp_var &var) {
  Inou_liveparse lp(var);

  lp.do_tolg(var);
}
