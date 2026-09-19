// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>

#include "hhds/graph.hpp"

class Enableopt {
public:
  void do_trans(const std::shared_ptr<hhds::Graph>& graph);
};
