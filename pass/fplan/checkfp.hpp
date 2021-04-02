//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph.hpp"
#include "pass.hpp"

class Pass_fplan_checkfp : public Pass {
public:
  Pass_fplan_checkfp(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);

private:
  Lgraph* root_lg;

  // checks all other nodes for overlapping floorplans, returns overlapping node or
  // invalid node if no overlaps found.
  Node check_bb(const Node& n);
};