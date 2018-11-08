//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#ifndef INOU_JSON_H
#define INOU_JSON_H

#include <string>

#include "rapidjson/document.h"

#include "pass.hpp"

class Inou_json : public Pass {
private:
protected:

  std::map<Index_ID, Index_ID> json_remap;

  bool is_const_op(const std::string &s) const;
  bool is_int(const std::string &s) const;

  void from_json(LGraph *g, rapidjson::Document &document);
  void to_json(const LGraph *g, const std::string &filename) const;

  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);

public:
  Inou_json();

  void setup() final;

};

#endif
