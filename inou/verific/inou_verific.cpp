//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "absl/strings/substitute.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "eprp_utils.hpp"

#include "inou_verific.hpp"

void setup_inou_verific() {
  Inou_verific p;
  p.setup();
}

Inou_verific::Inou_verific()
    : Pass("verific") {
}

void Inou_verific::setup() {
  Eprp_method m1("inou.verific.tolg", "import verilog to lgraph with verific", &Inou_verific::tolg);
  register_inou(m1);

}

void Inou_verific::tolg(Eprp_var &var) {

  auto files = var.get("files");
  if(files.empty()) {
    Pass::error("inou.verific.tolg: no files provided");
    return;
  }

  Inou_verific p;

  auto path = var.get("path");
  bool  ok  = p.setup_directory(path);
  if(!ok)
    return;

  std::vector<LGraph *> lgs;
  for(const auto &f : absl::StrSplit(files, ',')) {

    std::string_view name = f.substr(f.find_last_of("/\\") + 1);
    if(absl::EndsWith(name, ".v")) {
      name = name.substr(0, name.size() - 2); // remove .v
    }else if(absl::EndsWith(name, ".sv")) {
      name = name.substr(0, name.size() - 3); // remove .sv
    } else {
      Pass::error("inou.verific unknown file extension {}, expected .sv or .v", name);
      continue;
    }

    LGraph *lg = LGraph::create(path, name, f);

    bool ok = p.verific_parse(lg,f);

    if (ok) {
      lg->sync();
      lgs.push_back(lg);
    }else{
      lg->close();
    }
  }

  var.add(lgs);
}

bool Inou_verific::verific_parse(LGraph *lg, std::string_view filename) {

  fmt::print("FIXME: do pass here\n");


  return true; // no errors
}

