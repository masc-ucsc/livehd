//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_json.hpp"

#include <fstream>

#include "absl/strings/substitute.h"
#include "eprp_utils.hpp"
#include "lg_to_yjson.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

static Pass_plugin sample("inou_json", Inou_json::setup);

void Inou_json::setup() {
  Eprp_method m1("inou.json.tolg", mmap_lib::str("import from json to lgraph"), &Inou_json::tolg);
  register_inou("json", m1);

  Eprp_method m2(mmap_lib::str("inou.json.fromlg"), mmap_lib::str("export from lgraph to json"), &Inou_json::fromlg);
  register_inou("json", m2);
}

Inou_json::Inou_json(const Eprp_var &var) : Pass("inou.json", var) {}

void Inou_json::fromlg(Eprp_var &var) {
  Inou_json p(var);

  mmap_lib::str odir(var.get("odir"));
  bool          ok = p.setup_directory(odir);
  if (!ok) {
    error("inou.json.fromlg: could not create/access the odir:{} path", odir);
    return;
  }
  LGtoYJson conv;
  conv.to_json(var);
}

void Inou_json::tolg(Eprp_var &var) {
  auto files = var.get("files");
  if (files.empty()) {
    error("inou.json.tolg: no files provided");
    return;
  }

  Inou_json p(var);

  auto lgdb = var.get("lgdb");
  bool ok   = p.setup_directory(lgdb);
  if (!ok) {
    error("inou.json.tolg: could not create/access the lgdb:{} path", lgdb);
    return;
  }

  std::vector<Lgraph *> lgs;
  for (const auto &f : files.split(',')) {
    auto name = f.get_str_after_last_if_exists('/');
    if (name.ends_with(".json")) {
      name = name.substr(0, name.size() - 5);  // remove .json
    } else {
      error(fmt::format("inou.json.tolg unknown file extension {}, expected .json", name));
      continue;
    }

    FILE *pFile = fopen(f.to_s().c_str(), "rb");
    if (pFile == 0) {
      Pass::error("Inou_json::tolg could not open {} file", f);
      continue;
    }

    Lgraph *lg = Lgraph::create(lgdb, name, f);

    char                      buffer[65536];
    rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
    rapidjson::Document       document;
    document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);
    lgs.push_back(lg);
  }

  var.add(lgs);
}
