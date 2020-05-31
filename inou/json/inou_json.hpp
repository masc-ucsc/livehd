//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#pragma once

#include <string>

#include "pass.hpp"
#include "rapidjson/document.h"
#include "lgraph.hpp"

class Inou_json : public Pass {
private:
protected:
  //absl::flat_hash_map<int, Node> json_remap;
  absl::flat_hash_map<int, Node::Compact_class> json_remap;
  // for (auto ent: json_remap) {
  //   auto node = Node(g, ent.second);
  // }

  bool is_const_op(const std::string &s) const;
  bool is_int(const std::string &s) const;

  void from_json(LGraph *g, rapidjson::Document &document);
  void to_json(LGraph *lg, const std::string &filename) const;

  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);

public:
  Inou_json(const Eprp_var &var);

  static void setup();
};
