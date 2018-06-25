//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/stat.h>

#include "lgraph_base_core.hpp"
#include "lgedgeiter.hpp"


std::string Lgraph_base_core::Setup_path::last_path="";

Lgraph_base_core::Setup_path::Setup_path(const std::string &path) {

  if (last_path == path)
    return;

  console->info("Lgraph_base_core.cpp: mkdir {}",path);

  mkdir(path.c_str(), 0755);

  last_path = path;
}

Fast_edge_iterator Lgraph_base_core::fast() const {
  if(node_internal.empty())
    return Fast_edge_iterator(0, this);

  return Fast_edge_iterator(1, this);
}

int Console_init::_static_initializer = Console_init::initialize_logger();
