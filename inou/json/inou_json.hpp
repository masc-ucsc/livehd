//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string>

#include "lgraph.hpp"
#include "pass.hpp"
#include "rapidjson/document.h"

class Inou_json : public Pass {
private:
protected:
  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);

public:
  Inou_json(const Eprp_var &var);

  static void setup();
};

void from_json(Lgraph *g, rapidjson::Document &document);
void to_json(Lgraph *lg, const std::string &filename);
