#pragma once

#include "graph_info.hpp"
#include "hier_tree.hpp"
#include "pass.hpp"

class Pass_fplan : public Pass {
public:
  Pass_fplan(const Eprp_var &var) : Pass("pass.fplan", var), gi() {}

  static void setup();

  static void pass(Eprp_var &v);

private:
  friend class Pass_fplan_dump;
  
  Graph_info<g_type> gi;
};

class Pass_fplan_dump : public Pass {
public:
  Pass_fplan_dump(const Eprp_var &var) : Pass("pass.fplan", var) {}

  static void setup();

  static void dump_hier(Eprp_var &v);
  static void dump_tree(Eprp_var &v);
};