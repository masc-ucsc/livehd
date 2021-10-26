//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph_base_core.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "graph_library.hpp"
#include "lgedgeiter.hpp"
#include "mmap_map.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "tech_library.hpp"

static_assert(sizeof(Hierarchy_index) == 16);

mmap_lib::str Lgraph_base_core::Setup_path::last_path("");

Lgraph_base_core::Setup_path::Setup_path(const mmap_lib::str &path) {
  if (last_path == path)
    return;
  last_path = path;

  struct stat info;

  std::string spath(path.to_s());

  if (stat(spath.c_str(), &info) == 0) {
    if ((info.st_mode & S_IFDIR))
      return;

    unlink(spath.c_str());
  }

  mkdir(spath.c_str(), 0755);
}

Lgraph_base_core::Lgraph_base_core(const mmap_lib::str &_path, const mmap_lib::str &_name, Lg_type_id _lgid)
    : _p(_path)
    , path(_path)
    , name(_name)
    , unique_name(mmap_lib::str::concat(_path, "/", std::to_string(_lgid.value)))
    , long_name(mmap_lib::str::concat(mmap_lib::str("lgraph_"), _name))
    , lgid(_lgid)
    , locked(false) {
  assert(lgid);
}

void Lgraph_base_core::get_lock() {
  if (locked)
    return;

  std::string lock = absl::StrCat(path.to_s(), "/", std::to_string(lgid), ".lock");
  int         err  = ::open(lock.c_str(), O_CREAT | O_EXCL, 420);  // 644
  if (err < 0) {
    mmap_lib::mmap_gc::try_collect_fd();
    err = ::open(lock.c_str(), O_CREAT | O_EXCL, 420);  // 644
    if (err < 0) {
      perror("Error: ");
      fmt::print("error: could not get lock:{}. Already running? Unclear exit?", lock.c_str());
      throw std::runtime_error("unable to acquire lock");
    }
  }
  ::close(err);

  locked = true;
}

void Lgraph_base_core::clear() {
  // if (!locked) return;

  // whenever we clean, we unlock
  std::string lock = absl::StrCat(path.to_s(), "/", std::to_string(lgid), ".lock");
  unlink(lock.c_str());

  locked = false;
}

void Lgraph_base_core::sync() {
  if (!locked)
    return;

  std::string lock = absl::StrCat(path.to_s(), "/", std::to_string(lgid), ".lock");
  unlink(lock.c_str());
  locked = false;
}
