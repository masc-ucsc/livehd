#pragma once

#include <string_view>

#include "lg_flat_floorp.hpp"
#include "lg_hier_floorp.hpp"
#include "node_flat_floorp.hpp"
#include "node_hier_floorp.hpp"

#include "lgraph.hpp"
#include "pass.hpp"
#include "iassert.hpp"

class Pass_fplan_makefp : public Pass {
public:
  Pass_fplan_makefp(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);

private:
  LGraph* root_lg;

  void makefp_int(Lhd_floorplanner& fp, bool write_file);
};

class Pass_fplan_writearea : public Pass {
public:
  Pass_fplan_writearea(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);
};

class Pass_fplan_analyzefp : public Pass {
public:
  Pass_fplan_analyzefp(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);
};