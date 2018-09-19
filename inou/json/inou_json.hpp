//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 12/18/17.
//

#ifndef INOU_JSON_H
#define INOU_JSON_H

#include "inou.hpp"
#include "options.hpp"
#include "rapidjson/document.h"

#include <string>

class Inou_json_options : public Options_base {
public:
  Inou_json_options()
    : json_file("file.json") {
  }

  std::string json_file;

  void set(const std::string &key, const std::string &value) final;
};

class Inou_json : public Inou {
private:
protected:
  Inou_json_options opack;

  std::map<Index_ID, Index_ID> json_remap;

  bool is_const_op(std::string s);

  bool is_int(std::string s);

  void from_json(LGraph *g, rapidjson::Document &document);

  void to_json(const LGraph *g, const std::string &filename) const;

public:
  Inou_json();
  virtual ~Inou_json();

  std::vector<LGraph *> tolg() final;
  void fromlg(std::vector<const LGraph *> &out) final;

  void set(const std::string &key, const std::string &value) final {
    opack.set(key,value);
  }
};

#endif
