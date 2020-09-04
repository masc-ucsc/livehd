#pragma once

#include "graph_info.hpp"
#include "pass.hpp"

class Pass_fplan : public Pass {
public:
  Pass_fplan(const Eprp_var &var) : Pass("pass.fplan", var), gi() {}

  static void setup();

  static void pass(Eprp_var &v);

private:
  friend class Pass_fplan_dump;

  void make_graph(Eprp_var &var);

  Graph_info<g_type> gi;
};

class Pass_fplan_dump : public Pass {
public:
  Pass_fplan_dump(const Eprp_var &var) : Pass("pass.fplan", var) {}

  static void setup();

  static void pass(Eprp_var &v);
};