// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>

#include "hhds/graph.hpp"
#include "lean_options.hpp"
#include "pass.hpp"

class Pass_lean : public Pass, private LeanOptions {
public:
  static void setup();
  static void work(Eprp_var& var);
  explicit Pass_lean(const Eprp_var& var);

private:
  void emit_for_graph(const std::shared_ptr<hhds::Graph>& graph) const;
};
