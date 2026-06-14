//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// #include <ext/stdio_filebuf.h>
#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "cgen_verilog.hpp"
#include "file_utils.hpp"
#include "inou_yosys_api.hpp"
#ifdef I
#undef I
#endif
#include "graph_library_singleton.hpp"
#include "kernel/yosys.h"
#include "mustache.hpp"

static void log_error_atexit() { throw std::runtime_error("yosys finished"); }

void setup_inou_yosys() {
  Yosys::log_error_stderr    = true;
  Yosys::log_cmd_error_throw = true;
  Yosys::log_errfile         = stderr;
  Yosys::log_error_atexit    = log_error_atexit;
  Inou_yosys_api::setup();
}

Inou_yosys_api::Inou_yosys_api(Eprp_var& var, bool do_read) : Pass("inou.yosys", var) { set_script_yosys(var, do_read); }

void Inou_yosys_api::set_script_yosys(const Eprp_var& var, bool do_read) {
  auto script = var.get("script");

  auto main_path = file_utils::get_exe_path();

  std::vector<std::string> alt_paths{"/../pass/mockturtle/mt_test.sh.runfiles/livehd/inou/yosys/",
                                     "/../pass/sample/sample_test1.sh.runfiles/livehd/inou/yosys/",
                                     "/main_test.runfiles/livehd/inou/yosys/",
                                     "/verilog.sh.runfiles/livehd/inou/yosys/",
                                     "/verilog.sh-long.runfiles/livehd/inou/yosys/",
                                     "/lhd.runfiles/livehd/inou/yosys/",
                                     "/lhd.runfiles/_main/inou/yosys/",
                                     "/../share/livehd/inou/yosys/",
                                     "/../inou/yosys/",
                                     "/inou/yosys/"};

  if (script.empty()) {
    std::string do_read_str;
    if (do_read) {
      do_read_str = "inou_yosys_read.ys";
    } else {
      do_read_str = "inou_yosys_write.ys";
    }

    for (const auto& e : alt_paths) {
      auto test = main_path + e + do_read_str;
      if (access(test.c_str(), R_OK) != -1) {
        script_file = test;
        break;
      }
    }
  } else {
    script_file = script;
  }

  if (access(std::string(script_file).c_str(), R_OK) != F_OK) {
    livehd::diag::err("inou.yosys", "missing-file", "io")
        .msg("yosys setup could not find the provided script:{} file binary_path:{}", script_file, main_path)
        .fatal();
    return;
  }

  std::print("path:{} script:{}\n", main_path, script_file);
}

void Inou_yosys_api::call_yosys(mustache::data& vars) {
  std::ifstream inFile;
  inFile.open(std::string(script_file));
  if (!inFile.good()) {
    livehd::diag::err("inou.yosys", "missing-file", "io").msg("could not open {}", script_file).fatal();
  }

  std::stringstream strStream;
  strStream << inFile.rdbuf();  // read the whole file

  mustache::mustache tmpl(strStream.str());
  tmpl.set_custom_escape([](const std::string& s) { return s; });  // No HTML escape

  const std::string yosys_all_cmds = tmpl.render(vars);

  Yosys::RTLIL::Design design;

  auto cmd_list = absl::StrSplit(yosys_all_cmds, '\n');

  for (const auto& c : cmd_list) {
    auto x = std::find_if(c.begin(), c.end(), [](char ch) { return std::isalnum(ch); });
    if (x == c.end()) {
      continue;  // skip empty (or just space lines)
    }

    std::string cmd{c};  // yosys call needs std::string

    std::print("yosys cmd:{}\n", cmd);
    try {
      Yosys::Pass::call(&design, cmd);
    } catch (...) {
      err_tracker::logger("inou.yosys cmd:{} failed\n", cmd);
      livehd::diag::err("inou.yosys", "yosys-failed", "io").msg("cmd:{} failed", cmd).fatal();
    }
  }

#if 0
  char              filename[1024];
  strcpy(filename, "yosys_script.XXXXXX");

  int fd = mkstemp(filename);
  if (fd < 0) {
    livehd::diag::err("inou.yosys", "write-failed", "io").msg("Could not create yosys_script.XXXXXX file").fatal();
    return -1;
  }
  int sz_check = write(fd, yosys_cmd.c_str(), yosys_cmd.size());
  I(sz_check == yosys_cmd.size());
  close(fd);

  std::print("yosys {} synthesis cmd: {} using {}\n", filename, yosys, script_file);

  int pid = fork();
  if (pid < 0) {
    livehd::diag::err("inou.yosys", "yosys-failed", "internal").msg("unable to fork??").fatal();
    return -1;
  }

  if (pid == 0) {  // Child

    char filename2[1024 + 32];

    sprintf(filename2, "%s.log", filename);
    int fd_out = open(filename2, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_out < 0) {
      fprintf(stderr, "ERROR inou_yosys_api could not create %s file\n", filename);
      exit(-3);
    }
    sprintf(filename2, "%s.err", filename);
    int fd_err = open(filename2, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_err < 0) {
      fprintf(stderr, "ERROR inou_yosys_api could not create %s file\n", filename);
      exit(-3);
    }

    dup2(fd_out, STDOUT_FILENO);
    dup2(fd_err, STDERR_FILENO);
    close(fd_err);
    close(fd_out);

    char *argv[] = {strdup(yosys.c_str()),
                    strdup("-q"),
                    strdup("-s"),
                    strdup(filename),
                    0};

    if (execvp(yosys.c_str(), argv) < 0) {
      livehd::diag::err("inou.yosys", "yosys-failed", "internal").msg("execvp fail with {}", strerror(errno)).fatal();
      exit(-3);
    }

    exit(0);
  }

  int wstatus;
#if 1
  do {
    int w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
    if (w == -1) {
      livehd::diag::err("inou.yosys", "yosys-failed", "internal").msg("waitpid fail with {}", strerror(errno)).fatal();
      return errno;
    }

    if (WIFEXITED(wstatus)) {
      printf("exited, status=%d\n", WEXITSTATUS(wstatus));
      if (wstatus != 0) {
        std::string errpath = filename;
        errpath.append(".err");
        std::ifstream errfile;
        errfile.open(errpath, std::ios::in);
        std::string errtext;
        getline(errfile, errtext);
        std::cout << "\nyosys output: " << errtext << std::endl;
        errfile.close();
      }
    } else if (WIFSIGNALED(wstatus)) {
      printf("killed by signal %d\n", WTERMSIG(wstatus));
    } else if (WIFSTOPPED(wstatus)) {
      printf("stopped by signal %d\n", WSTOPSIG(wstatus));
    } else if (WIFCONTINUED(wstatus)) {
      printf("continued\n");
    }
  } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
#else
  wait(&wstatus);
#endif
  return wstatus;
#endif
}

void Inou_yosys_api::tolg(Eprp_var& var) {
  Inou_yosys_api p(var, true);

  p.do_tolg(var);
}

void Inou_yosys_api::do_tolg(Eprp_var& var) {
  const auto filelist_file{var.get("filelist_file")};

  const bool has_files = !files.empty() && files != "/INVALID";
  // Sources can also ride in slang_flags (e.g. `lhd ... --reader yosys-slang --
  // -F filelist.f`, where lhd forwards the raw `--` args as slang_flags into
  // read_slang). Accept that as a valid source spec too.
  const bool has_slang_flag_sources = !var.get("slang_flags").empty();
  if (!has_files && filelist_file.empty() && !has_slang_flag_sources) {
    livehd::diag::err("inou.yosys", "bad-option", "io")
        .msg("at least one of files, filelist_file, or slang_flags sources must be provided")
        .fatal();
    return;
  }

  const auto techmap{var.get("techmap")};
  const auto abc{var.get("abc")};
  const auto top{var.get("top")};
  const auto frontend{var.get("frontend")};
  // const auto lib{var.get("liberty")};

  mustache::data vars;
  vars.set("path", path);

  // Set slang plugin path (assume users always install slang.so in LiveHD using Bazel)
  auto        exe_path = file_utils::get_exe_path();
  std::string slang_plugin_path;
  for (const auto& candidate : {absl::StrCat(exe_path, "/../external/+_repo_rules+yosys_slang/slang.so"),
                                absl::StrCat(exe_path, "/../external/+http_archive+yosys_slang/slang.so"),
                                absl::StrCat(exe_path, "/lhd.runfiles/+http_archive+yosys_slang/slang.so")}) {
    if (access(candidate.c_str(), R_OK) != -1) {
      slang_plugin_path = candidate;
      break;
    }
  }
  if (slang_plugin_path.empty()) {
    livehd::diag::err("inou.yosys", "missing-file", "io")
        .msg("internal error: slang.so could not be found (tried paths relative to exe_path:{})", exe_path)
        .fatal();
    return;
  }
  vars.set("slang_plugin_path", slang_plugin_path);

  // For verilog frontend
  mustache::data filelist{mustache::data::type::list};
  if (has_files) {
    for (const auto& f : absl::StrSplit(files, ',')) {
      if (!f.empty()) {
        filelist << mustache::data{"input", std::string(f)};
      }
    }
  }

  // Build space-separated files list for slang
  std::string files_str;
  if (has_files) {
    for (const auto& f : absl::StrSplit(files, ',')) {
      if (!f.empty()) {
        if (!files_str.empty()) {
          files_str += " ";
        }
        files_str += std::string(f);
      }
    }
  }
  vars.set("filelist", filelist);

  // Set a list of files as well as the filelist/manifest file for `read_lang`
  if (!files_str.empty()) {
    vars.set("files_str", files_str);
  }
  if (!filelist_file.empty()) {
    if (filelist_file.size() >= 2 && filelist_file[0] == '-' && (filelist_file[1] == 'f' || filelist_file[1] == 'F')) {
      vars.set("filelist_file_str", std::string(filelist_file));
    } else {
      vars.set("filelist_file_str", absl::StrCat("-f ", filelist_file));
    }
  }

  // Set frontend type
  if (frontend == "slang") {
    vars.set("use_slang", mustache::data::type::bool_true);
    vars.set("use_verilog", mustache::data::type::bool_false);

    const auto slang_flags{var.get("slang_flags")};
    if (!slang_flags.empty()) {
      // Comma-separated by convention (EPRP labels). A '\x1f' (ASCII unit
      // separator) delimiter is also accepted so callers like lhd can pass
      // flags that themselves contain commas (e.g. +incdir+a,b) losslessly.
      const char  sep = slang_flags.find('\x1f') != std::string_view::npos ? '\x1f' : ',';
      std::string flags_str;
      for (const auto& f : absl::StrSplit(slang_flags, sep)) {
        if (!flags_str.empty()) {
          flags_str += " ";
        }
        flags_str += std::string(f);
      }
      vars.set("slang_flags", flags_str);
    }
  } else if (frontend.empty() || frontend == "verilog") {
    vars.set("use_slang", mustache::data::type::bool_false);
    vars.set("use_verilog", mustache::data::type::bool_true);
  } else {
    livehd::diag::err("inou.yosys", "bad-option", "io")
        .msg("unrecognized frontend {} option. Either verilog or slang", frontend)
        .fatal();
    return;
  }

  const auto elab_top{var.get("elab_top")};

  if (!top.empty()) {
    vars.set("hierarchy", mustache::data::type::bool_true);
    if (top != "-auto-top") {
      vars.set("top", absl::StrCat("-top ", top));
    } else {
      vars.set("top", std::string(top));
    }
  } else {
    vars.set("hierarchy", mustache::data::type::bool_false);
  }

  // Set slang_top for read_slang: use elab_top if provided, otherwise use top
  if (!elab_top.empty()) {
    vars.set("slang_top", absl::StrCat("--top ", elab_top));
  } else if (!top.empty() && top != "-auto-top") {
    vars.set("slang_top", absl::StrCat("--top ", top));
  }

  if (!techmap.empty()) {
    if (techmap == "alumacc") {
      vars.set("techmap_alumacc", mustache::data::type::bool_true);
    } else if (techmap == "full") {
      vars.set("techmap_full", mustache::data::type::bool_true);
    } else {
      livehd::diag::err("inou.yosys", "bad-option", "io")
          .msg("unrecognized techmap {} option. Either full or alumacc", techmap)
          .fatal();
      return;
    }
  }

  const auto rename_top{var.get("rename_top")};
  if (!rename_top.empty()) {
    vars.set("rename_top", std::string(rename_top));
    if (!top.empty() && top != "-auto-top") {
      vars.set("rename_from", std::string(top));
    }
  }

  if (abc == "true" || abc == "1") {
    vars.set("abc_in_yosys", mustache::data::type::bool_true);
  } else if (abc == "false" || abc == "0") {
    // Nothing to do
  } else {
    livehd::diag::err("inou.yosys", "bad-option", "io").msg("unrecognized abc {} option. Either true or false", techmap).fatal();
  }

  auto& lib = livehd::Hhds_graph_library::instance(path);

  // gids are sparse name-hashes (not a sequential counter), so "new graphs"
  // can't be a capacity range — snapshot the live gid set before yosys runs and
  // diff against it afterward. all_gids() returns a sorted vector.
  const std::vector<hhds::Gid> before = lib.all_gids();

  Yosys::yosys_setup();

  call_yosys(vars);

  for (const hhds::Gid id : lib.all_gids()) {
    if (std::binary_search(before.begin(), before.end(), id)) {
      continue;  // existed before yosys ran
    }
    auto g = lib.get_graph(id);
    if (g) {
      var.add(g);
    }
  }
}

void Inou_yosys_api::fromlg(Eprp_var& var) {
  Inou_yosys_api p(var, false);

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    Cgen_verilog cgen(false, p.odir);
    cgen.do_from_graph(g);
  }
}

void Inou_yosys_api::setup() {
  Eprp_method m1("inou.yosys.tolg", "read verilog using yosys to lgraph", &Inou_yosys_api::tolg);
  m1.add_label_optional("files", "verilog files to process (comma separated)");
  m1.add_label_optional("filelist_file", "path to filelist file (.f/.F) containing additional source files for read_slang", "");
  m1.add_label_optional("path", "path to build the lgraph[s]", "lgdb");
  m1.add_label_optional("frontend", "frontend to use: verilog or slang", "verilog");
  m1.add_label_optional("slang_flags", "comma- (or \\x1f-) separated flags for read_slang command", "");
  m1.add_label_optional("techmap", "Either full or alumac techmap or none from yosys. Cannot be used with liberty", "");
  m1.add_label_optional("liberty", "Liberty file for technology mapping. Cannot be used with techmap, will call abc for tmap", "");
  m1.add_label_optional("abc", "run ABC inside yosys before loading lgraph", "false");
  m1.add_label_optional("script", "alternative custom inou_yosys_read.ys command");
  m1.add_label_optional("yosys", "path for yosys command", "");
  m1.add_label_required("top", "define top module for synthesis, will call yosys hierarchy pass (-auto-top allowed)");
  m1.add_label_optional("elab_top", "define top module for elaboration (read_slang). If not provided, uses 'top' value");
  m1.add_label_optional("rename_top", "rename the top module to the given name after synthesis");

  register_inou("yosys", m1);

  Eprp_method m2("inou.yosys.fromlg", "write verilog from lgraph", &Inou_yosys_api::fromlg);
  m2.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m2.add_label_optional("odir", "output directory for generated verilog files", ".");
  m2.add_label_optional("script", "alternative custom inou_yosys_write.ys command");
  m2.add_label_optional("yosys", "path for yosys command", "");
  m2.add_label_optional("hier", "hierarchy pass in LiveHD (like flat in yosys)");

  register_inou("yosys", m2);
}
