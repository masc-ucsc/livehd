#include "pass.hpp"

#include <sys/stat.h>

Eprp Pass::eprp;

const std::string Pass::get_files(const Eprp_var &var) const {
  std::string _files;
  if (var.has_label("files")) {
    _files = var.get("files");
    for (auto f : absl::StrSplit(_files, ',')) {
      if (access(std::string(f).c_str(), F_OK) == -1) {
        _files = "/INVALID";
        error("{} could not access file:{}", pass_name, f);
      }
    }
  } else {
    _files = "/INVALID";
  }

  return _files;
}

const std::string Pass::get_path(const Eprp_var &var) const {
  std::string _path;

  if (var.has_label("path")) {
    _path = var.get("path");
    if (!setup_directory(_path)) {
      _path = "/INVALID";
      error("{} could not gain access to path:{}", pass_name, _path);
    }
  } else {
    _path = "/INVALID";
  }

  return _path;
}

const std::string Pass::get_odir(const Eprp_var &var) const {
  std::string _odir;

  if (var.has_label("odir")) {
    _odir = var.get("odir");
    if (!setup_directory(_odir)) {
      _odir = "/INVALID";
      error("{} could not gain access to odir:{}", pass_name, _odir);
    }
  } else {
    _odir = "/INVALID";
  }

  return _odir;
}

Pass::Pass(std::string_view _pass_name, const Eprp_var &var)
    : pass_name(_pass_name), files(get_files(var)), path(get_path(var)), odir(get_odir(var)) {}

void Pass::register_pass(Eprp_method &method) {
  eprp.register_method(method);

  // All the passses should start with pass.*
  assert(method.get_name().substr(0, 5) == "pass." || method.get_name().substr(0, 5) == "live." ||
         method.get_name().substr(0, 5) == "inou.");
}

void Pass::register_inou(std::string_view _pname, Eprp_method &method) {
  // All the passses should start with inou.*
  const std::string pname{_pname};

  assert(method.get_name().compare(0, pname.size() + 5, std::string{"inou." + pname}) == 0);

  if (method.get_name() == std::string{"inou." + pname + ".tolg"}) {
    method.add_label_optional("path", "lgraph path", "lgdb");
    method.add_label_required("files", "input file[s]");
  } else if (method.get_name() == std::string{"inou." + pname + ".fromlg"}) {
    method.add_label_optional("odir", "output directory", ".");
  } else if (method.get_name() == std::string{"inou." + pname + ".fromlnast"}) {  // for dot
    method.add_label_required("files", "input file[s]");
    method.add_label_optional("odir", "output directory", ".");
  } else if (method.get_name().rfind(std::string{"inou." + pname}, 0) == 0) {
    method.add_label_optional("path", "lgraph path", "lgdb");
    method.add_label_optional("files", "input file[s]");
    method.add_label_optional("odir", "output directory", ".");
  } else {
    assert(false);
    // inou methods should be inou.name.tolg or inou.name.fromlg or inou.name generic for passes that handle one way only
    //
    // Possible to have submothods like inou.name.tolg.foobar
  }

  eprp.register_method(method);
}

bool Pass::setup_directory(std::string_view dir) const {
  if (dir == ".") return true;

  struct stat sb;

  std::string sdir(dir);

  if (stat(sdir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) return true;

  int e = mkdir(sdir.c_str(), 0755);
  if (e < 0) {
    return false;
  }

  return true;
}
