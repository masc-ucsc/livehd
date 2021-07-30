#include "pass.hpp"

#include <sys/stat.h>

// Eprp Pass::eprp;

mmap_lib::str Pass::get_files(const Eprp_var &var) const {
  mmap_lib::str _files;
  if (var.has_label("files")) {
    _files = mmap_lib::str(var.get("files"));

    for (auto f : _files.split(',')) {
      if (access(f.to_s().c_str(), F_OK) == -1) {
        _files = "/INVALID";
        error("{} could not access file:{}", pass_name, f);
      }
    }
  } else {
    _files = "/INVALID";
  }

  return _files;  // eg.: returns "inou/cfg/tests/nested_if.prp"
}

mmap_lib::str Pass::get_path(const Eprp_var &var) const {
  mmap_lib::str _path;

  if (var.has_label("path")) {
    _path = mmap_lib::str(var.get("path"));
    if (!setup_directory(_path)) {
      _path = "/INVALID";
      error("{} could not gain access to path:{}", pass_name, _path);
    }
  } else {
    _path = "/INVALID";
  }

  return _path;
}

mmap_lib::str Pass::get_odir(const Eprp_var &var) const {
  mmap_lib::str _odir;

  if (var.has_label("odir")) {
    _odir = mmap_lib::str(var.get("odir"));
    if (!setup_directory(_odir)) {
      _odir = "/INVALID";
      error("{} could not gain access to odir:{}", pass_name, _odir);
    }
  } else {
    _odir = "/INVALID";
  }

  return _odir;
}

Pass::Pass(const mmap_lib::str &_pass_name, const Eprp_var &var)
    : pass_name(_pass_name), files(get_files(var)), path(get_path(var)), odir(get_odir(var)) {}

void Pass::register_pass(Eprp_method &method) {
  eprp.register_method(method);

  // All the passses should start with pass.*
  assert(method.get_name().substr(0, 5) == "pass." || method.get_name().substr(0, 5) == "live."
         || method.get_name().substr(0, 5) == "inou.");
}

void Pass::register_inou(const mmap_lib::str &pname, Eprp_method &method) {
  // All the inou should start with inou.*

  if (method.get_name() == mmap_lib::str::concat("inou.", pname, ".tolg")) {
    method.add_label_optional("path", "lgraph path", "lgdb");
    method.add_label_required("files", "input file[s]");
  } else if (method.get_name() == mmap_lib::str::concat("inou.", pname, ".fromlg")) {
    method.add_label_optional("odir", mmap_lib::str("output directory"), ".");
  } else if (method.get_name() == mmap_lib::str::concat("inou.", pname, ".fromlnast")) {  // for dot
    method.add_label_required("files", "input file[s]");
    method.add_label_optional("odir", mmap_lib::str("output directory"), ".");
  } else if (method.get_name().rfind(mmap_lib::str::concat(mmap_lib::str("inou."), pname), 0) == 0) {
    method.add_label_optional("path", "lgraph path", "lgdb");
    method.add_label_optional("files", "input file[s]");
    method.add_label_optional("odir", mmap_lib::str("output directory"), ".");
  } else {
    assert(false);
    // inou methods should be inou.name.tolg or inou.name.fromlg or inou.name generic for passes that handle one way only
    //
    // Possible to have submothods like inou.name.tolg.foobar
  }

  eprp.register_method(method);
}

bool Pass::setup_directory(const mmap_lib::str &dir) const {
  if (dir == ".")
    return true;

  struct stat sb;

  std::string sdir(dir.to_s());

  if (stat(sdir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode))
    return true;

  int e = mkdir(sdir.c_str(), 0755);
  if (e < 0) {
    return false;
  }

  return true;
}
