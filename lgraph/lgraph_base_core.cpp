//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph_base_core.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "str_tools.hpp"
#include "tech_library.hpp"

// static_assert(sizeof(Hierarchy_index) == 16);

Lgraph_base_core::Setup_path::Setup_path(std::string_view path) {
  if (last_path == path) {
    return;
  }
  last_path = path;

  struct stat info;

  std::string spath(path);

  if (stat(spath.c_str(), &info) == 0) {
    if ((info.st_mode & S_IFDIR)) {
      return;
    }

    unlink(spath.c_str());
  }

  mkdir(spath.c_str(), 0755);
}

Lgraph_base_core::Lgraph_base_core(std::string_view _path, std::string_view _name, Lg_type_id _lgid)
    : _p(_path)
    , path(_path)
    , name(_name)
    , unique_name(absl::StrCat(_path, "/", str_tools::to_s(_lgid.value)))
    , long_name(absl::StrCat("lgraph_", _name))
    , lgid(_lgid) {
  assert(lgid);
}

void Lgraph_base_core::clear() {
  // whenever we clean, we unlock
  auto lock = absl::StrCat(path, "/", std::to_string(lgid), ".lock");
  unlink(lock.c_str());
}
