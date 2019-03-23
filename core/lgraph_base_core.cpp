//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lgedgeiter.hpp"
#include "lgraph_base_core.hpp"
#include "tech_library.hpp"
#include "graph_library.hpp"

std::string Lgraph_base_core::Setup_path::last_path = "";

Lgraph_base_core::Setup_path::Setup_path(std::string_view path) {
  if (last_path == path) return;
  last_path = path;

  struct stat info;

  std::string spath(path);

  if (stat(spath.c_str(), &info) == 0) {
    if ((info.st_mode & S_IFDIR)) return;

    unlink(spath.c_str());
  }

  mkdir(spath.c_str(), 0755);
}

Lgraph_base_core::Lgraph_base_core(std::string_view _path, std::string_view _name, Lg_type_id lgid)
    : p(_path)
    , path(_path)
    , name(_name)
    , long_name(absl::StrCat("lgraph_", _name))
    , lgraph_id(lgid)
    , locked(false) {
  assert(lgid);

  library  = Graph_library::instance(path);
  tlibrary = Tech_library::instance(path);
}

void Lgraph_base_core::get_lock() {
  if (locked) return;

  std::string lock = absl::StrCat(path, "/", std::to_string(lgraph_id), ".lock");
  int         err  = ::open(lock.c_str(), O_CREAT | O_EXCL, 420);  // 644
  if (err < 0) {
    Pass::error("Could not get lock:{}. Already running? Unclear exit?", lock.c_str());
    assert(false);  // ::error raises an exception
  }
  ::close(err);

  locked = true;
}

bool Lgraph_base_core::close() {
  if (!locked) return true;

  library->update(lgraph_id);

  return true;
}

void Lgraph_base_core::clear() {
  if (!locked) return;

  // whenever we clean, we unlock
  std::string lock = absl::StrCat(path, "/", std::to_string(lgraph_id), ".lock");
  unlink(lock.c_str());

  library->update_nentries(lg_id(), 0);

  locked = false;
}

void Lgraph_base_core::sync() {
  if (!locked) return;

  library->sync();
  tlibrary->sync();

  std::string lock = absl::StrCat(path, "/", std::to_string(lgraph_id), ".lock");
  unlink(lock.c_str());
  locked = false;
}

