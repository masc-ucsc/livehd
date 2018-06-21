//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by birdeclipse on 3/22/18.
//

#ifndef LGRAPH_INOU_LEF_HPP
#define LGRAPH_INOU_LEF_HPP

#include "lefiDefs.hpp"
#include "lefrReader.hpp"

#include "inou.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "options.hpp"
#include "tech_library.hpp"

class Inou_lef_options_pack : public Options_pack {
public:
  Inou_lef_options_pack();

  std::string lef_file;
};

class Inou_lef : public Inou {
private:
protected:
  Inou_lef_options_pack opack;

public:
  Inou_lef();

  virtual ~Inou_lef();

  std::vector<LGraph *> generate() final;

  using Inou::generate;

  void generate(std::vector<const LGraph *> &out) final;

  static void lef_parsing(Tech_library *tlib, std::string &lef_file_name);
};

#endif //LGRAPH_INOU_LEF_HPP
