//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_read.hpp"

#include <filesystem>
#include <fstream>

#include "absl/strings/str_split.h"
#include "fmt/printf.h"
#include "lnast_parser.hpp"

static Pass_plugin sample("lnast.read", Pass_lnast_read::setup);

Pass_lnast_read::Pass_lnast_read(Eprp_var& var) : Pass("lnast.read", var) {}

void Pass_lnast_read::setup() {
  Eprp_method m1("lnast.read", "Read from LNAST textual IR files and convert them to LNAST", &Pass_lnast_read::do_work);
  m1.add_label_optional("files", "LNAST textual IR files");
  m1.add_label_optional("path", "LNAST textual IR directory");
  register_pass(m1);
}

void Pass_lnast_read::do_work(Eprp_var& var) {
  Pass_lnast_read pass(var);
  if (var.has_label("files")) {
    for (const auto& file : absl::StrSplit(var.get("files"), ',')) {
      std::ifstream fs;
      fs.open(std::string(file));
      Lnast_parser parser(fs);
      var.add(parser.parse_all());
    }
  }
  if (var.has_label("dir")) {
    for (const auto& entry : std::filesystem::directory_iterator(var.get("dir"))) {
      auto file = entry.path();
      fmt::print("lnast_read : {}\n", std::string{file});
      std::ifstream fs;
      fs.open(std::string(file));
      Lnast_parser parser(fs);
      var.add(parser.parse_all());
    }
  }
}
