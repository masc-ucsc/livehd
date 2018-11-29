//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lgedgeiter.hpp"
#include "lgraph_base_core.hpp"

std::string Lgraph_base_core::Setup_path::last_path = "";

Lgraph_base_core::Setup_path::Setup_path(const std::string &path) {

  if(last_path == path)
    return;
  last_path = path;

  struct stat info;

  if(stat(path.c_str(), &info) == 0) {
    if((info.st_mode & S_IFDIR))
      return;

    unlink(path.c_str());
  }

  mkdir(path.c_str(), 0755);
}

Lgraph_base_core::Lgraph_base_core(const std::string &_path, const std::string &_name)
    : p(_path)
    , path(_path)
    , name(_name)
    , long_name("lgraph_" + _name)
    , node_internal(path + "/lgraph_" + name + "_nodes"){

      };

Fast_edge_iterator Lgraph_base_core::fast() const {
  return Fast_edge_iterator(fast_next(0), this); // Skip after 1, but first may be deleted, so fast_next
}

int Console_init::_static_initializer = Console_init::initialize_logger();
