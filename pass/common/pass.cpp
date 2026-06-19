
#include "pass.hpp"

#include <sys/stat.h>

#include "absl/strings/str_split.h"
#include "str_tools.hpp"

// Eprp Pass::eprp;

std::string Pass::get_files(const Eprp_var& var) const {
  std::string _files;
  if (var.has_label("files")) {
    _files = var.get("files");

    for (const auto& f : absl::StrSplit(_files, ',')) {
      std::string fname(f);
      if (access(fname.c_str(), F_OK) == -1) {
        _files = "/INVALID";
        livehd::diag::err(pass_name, "missing-file", "io").msg("could not access file:{}", f).fatal();
      }
    }
  } else {
    _files = "/INVALID";
  }

  return _files;  // eg.: returns "inou/cfg/tests/nested_if.prp"
}

std::string Pass::get_path(const Eprp_var& var) const {
  std::string _path = var.has_label("path") ? std::string{var.get("path")} : std::string{};

  // No explicit `path`: return the legacy default name but do NOT create a
  // directory. Most passes (lnastfmt, prp_writer, cgen, the slang/prp readers,
  // upass) never touch a graph library, and eagerly mkdir-ing the old default
  // `lgdb/` for every constructed Pass left a stray empty dir in the cwd.
  // Passes that actually persist a library receive an explicit `path` (the
  // kernel routes it to the `lg:` emit dir or --workdir), and the directory is
  // created lazily by whoever writes to it (Hhds_graph_library::save).
  if (_path.empty()) {
    return "lgdb";
  }

  if (!setup_directory(_path)) {
    livehd::diag::err(pass_name, "missing-path", "io").msg("could not gain access to path:{}", _path).fatal();
    _path = "/INVALID";
  }

  return _path;
}

std::string Pass::get_odir(const Eprp_var& var) const {
  std::string _odir;

  if (var.has_label("odir")) {
    _odir = var.get("odir");
    if (!setup_directory(_odir)) {
      _odir = "/INVALID";
      livehd::diag::err(pass_name, "missing-path", "io").msg("could not gain access to odir:{}", _odir).fatal();
    }
  } else {
    _odir = "/INVALID";
  }

  return _odir;
}

Pass::Pass(std::string_view _pass_name, const Eprp_var& var)
    : pass_name(_pass_name), files(get_files(var)), path(get_path(var)), odir(get_odir(var)) {}

void Pass::register_pass(Eprp_method& method) {
  eprp.register_method(method);

  // All the passses should start with pass.*
  assert(method.get_name().substr(0, 5) == "pass." || method.get_name().substr(0, 5) == "live."
         || method.get_name().substr(0, 5) == "inou." || method.get_name().substr(0, 6) == "lnast."
         || method.get_name().substr(0, 7) == "lgraph.");
}

void Pass::register_inou(std::string_view pname, Eprp_method& method) {
  // All the inou should start with inou.*

  if (method.get_name() == absl::StrCat("inou.", pname, ".tolg")) {
    // Empty default: do not inject "lgdb" into the var (eprp only injects
    // non-empty defaults), so a pass run without an explicit path does not
    // materialize a stray `lgdb/` via get_path/setup_directory.
    method.add_label_optional("path", "lgraph path", "");
    method.add_label_required("files", "input file[s]");
  } else if (method.get_name() == absl::StrCat("inou.", pname, ".fromlg")) {
    method.add_label_optional("odir", "output directory", ".");
  } else if (method.get_name() == absl::StrCat("inou.", pname, ".fromlnast")) {  // for dot
    method.add_label_required("files", "input file[s]");
    method.add_label_optional("odir", "output directory", ".");
  } else if (method.get_name().rfind(absl::StrCat("inou.", pname), 0) == 0) {
    method.add_label_optional("path", "lgraph path", "");  // empty: see .tolg note above
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
  if (dir == ".") {
    return true;
  }

  struct stat sb;

  std::string sdir(dir);

  if (stat(sdir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return true;
  }

  int e = mkdir(sdir.c_str(), 0755);
  if (e < 0) {
    return false;
  }

  return true;
}
