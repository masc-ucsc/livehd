//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "floorplanner.hpp"

class Lg_hier_floorp : public Lhd_floorplanner {
public:
  Lg_hier_floorp(Node_tree&& nt_arg);
  void load();

private:
  geogLayout* load_lg_modules(LGraph* lg);
};