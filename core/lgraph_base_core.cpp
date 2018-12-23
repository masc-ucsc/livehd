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

Lgraph_base_core::Lgraph_base_core(const std::string &_path, const std::string &_name, Lg_type_id lgid)
    : p(_path)
    , path(_path)
    , name(_name)
    , long_name("lgraph_" + _name)
    , lgraph_id(lgid)
    , node_internal(path + "/lgraph_" + name + "_nodes")
    , locked(false) {

  assert(lgid);

  library  = Graph_library::instance(path);
  tlibrary = Tech_library::instance(path);
}

void Lgraph_base_core::get_lock() {
  if(locked)
    return;

  std::string lock = path + "/" + long_name + ".lock";
  int         err  = ::open(lock.c_str(), O_CREAT | O_EXCL, 420); // 644
  if(err < 0) {
    Pass::error("Could not get lock:{}. Already running? Unclear exit?", lock.c_str());
    assert(false); // ::error raises an exception
  }
  ::close(err);

  locked = true;
}

void Lgraph_base_core::close() {
  if(!locked)
    return;

  library->update(lgraph_id);
}

void Lgraph_base_core::clear() {
  if (!locked)
    return;

  // whenever we clean, we unlock
  std::string lock = path + "/" + long_name + ".lock";
  unlink(lock.c_str());

  library->update_nentries(lg_id(), 0);

  locked = false;
}

void Lgraph_base_core::sync() {
  if (!locked)
    return;

  library->update_nentries(lg_id(), node_internal.size());

  library->sync();
  tlibrary->sync();

  std::string lock = path + "/" + long_name + ".lock";
  unlink(lock.c_str());
  locked = false;
}

Fast_edge_iterator Lgraph_base_core::fast() const {
  return Fast_edge_iterator(fast_next(0), this); // Skip after 1, but first may be deleted, so fast_next
}

