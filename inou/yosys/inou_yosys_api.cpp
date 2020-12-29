//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//#include <ext/stdio_filebuf.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "eprp_utils.hpp"
#include "inou_yosys_api.hpp"
#include "kernel/yosys.h"
#include "lgraph.hpp"
#include "mustache.hpp"

static void log_error_atexit() { throw std::runtime_error("yosys finished"); }

void setup_inou_yosys() {
  Yosys::log_error_stderr    = true;
  Yosys::log_cmd_error_throw = true;
  Yosys::log_errfile         = stderr;
  Yosys::log_error_atexit    = log_error_atexit;
  Inou_yosys_api::setup();
}

Inou_yosys_api::Inou_yosys_api(Eprp_var &var, bool do_read) : Pass("inou.yosys", var) { set_script_yosys(var, do_read); }

void Inou_yosys_api::set_script_yosys(const Eprp_var &var, bool do_read) {
  auto script = var.get("script");

  auto main_path = Eprp_utils::get_exe_path();

  fmt::print("path:{}\n", main_path);

  std::vector<std::string> alt_paths{"/../pass/mockturtle/mt_test.sh.runfiles/livehd/inou/yosys/",
                                     "/../pass/lnast_fromlg/lgtoln_verif_from_verilog.sh.runfiles/livehd/inou/yosys/",
                                     "/../pass/lnast_fromlg/lgtoln_verif_from_pyrope.sh.runfiles/livehd/inou/yosys/",
                                     "/../pass/sample/sample_test1.sh.runfiles/livehd/inou/yosys/",
                                     "/main_test.runfiles/livehd/inou/yosys/",
                                     "/verilog.sh.runfiles/livehd/inou/yosys/",
                                     "/verilog.sh-long.runfiles/livehd/inou/yosys/",
                                     "/lgshell.runfiles/livehd/inou/yosys/",
                                     "/../inou/pyrope/lnast_prp_test.sh.runfiles/livehd/inou/yosys/",
                                     "/../share/livehd/inou/yosys/",
                                     "/../inou/yosys/",
                                     "/inou/yosys/"};

  if (script.empty()) {
    std::string do_read_str;
    if (do_read)
      do_read_str = "inou_yosys_read.ys";
    else
      do_read_str = "inou_yosys_write.ys";

    for (const auto& e : alt_paths) {
      auto test = main_path + e + do_read_str;
      if (access(test.c_str(), R_OK) != -1) {
        script_file = test;
        break;
      }
    }
  }

  if (access(std::string(script_file).c_str(), R_OK) != F_OK) {
    error("yosys setup could not find the provided script:{} file", script_file);
    return;
  }
}

void Inou_yosys_api::call_yosys(mustache::data &vars) {
  std::ifstream inFile;
  inFile.open(std::string(script_file));
  if (!inFile.good())
    error("inou_yosys_api: could not open {}", script_file);

  std::stringstream strStream;
  strStream << inFile.rdbuf();  // read the whole file

  mustache::mustache tmpl(strStream.str());
  tmpl.set_custom_escape([](const std::string &s) { return s; });  // No HTML escape

  const std::string yosys_all_cmds = tmpl.render(vars);

  Yosys::RTLIL::Design design;

  auto cmd_list = absl::StrSplit(yosys_all_cmds, '\n');

  for (const auto &c : cmd_list) {
    auto x = std::find_if(c.begin(), c.end(), [](char ch) { return std::isalnum(ch); });
    if (x == c.end())
      continue;  // skip empty (or just space lines)

    std::string cmd{c};  // yosys call needs std::string

    fmt::print("yosys cmd:{}\n", cmd);
    try {
      Yosys::Pass::call(&design, cmd);
    } catch (...) {
      error("inou.yosys cmd:{} failed\n", cmd);
    }
  }

#if 0
  char              filename[1024];
  strcpy(filename, "yosys_script.XXXXXX");

  int fd = mkstemp(filename);
  if (fd < 0) {
    error("Could not create yosys_script.XXXXXX file\n");
    return -1;
  }
  int sz_check = write(fd, yosys_cmd.c_str(), yosys_cmd.size());
  I(sz_check == yosys_cmd.size());
  close(fd);

  fmt::print("yosys {} synthesis cmd: {} using {}\n", filename, yosys, script_file);

  int pid = fork();
  if (pid < 0) {
    error("unable to fork??");
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
      error("execvp fail with {}", strerror(errno));
      exit(-3);
    }

    exit(0);
  }

  int wstatus;
#if 1
  do {
    int w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
    if (w == -1) {
      error("waitpid fail with {}", strerror(errno));
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

void Inou_yosys_api::tolg(Eprp_var &var) {
  Inou_yosys_api p(var, true);

  p.do_tolg(var);
}

void Inou_yosys_api::do_tolg(Eprp_var &var) {
  if (files.empty()) {
    error("files can not be empty");
    return;
  }

  const std::string techmap{var.get("techmap")};
  const std::string abc{var.get("abc")};
  const std::string top{var.get("top")};
  const std::string lib{var.get("liberty")};

  mustache::data vars;
  vars.set("path", std::string(path));

  mustache::data filelist{mustache::data::type::list};
  for (const auto &f : absl::StrSplit(files, ',')) {
    filelist << mustache::data{"input", std::string(f)};
  }

  vars.set("filelist", filelist);

  if (!top.empty()) {
    vars.set("hierarchy", mustache::data::type::bool_true);
    if (top != "-auto-top") {
      vars.set("top", "-top " + top);
    } else {
      vars.set("top", top);
    }
  } else {
    vars.set("hierarchy", mustache::data::type::bool_false);
  }

  if (!techmap.empty()) {
    if (techmap == "alumacc") {
      vars.set("techmap_alumacc", mustache::data::type::bool_true);
    } else if (techmap == "full") {
      vars.set("techmap_full", mustache::data::type::bool_true);
    } else {
      error("unrecognized techmap {} option. Either full or alumacc", techmap);
      return;
    }
  }

  if (abc == "true" || abc == "1") {
    vars.set("abc_in_yosys", mustache::data::type::bool_true);
  } else if (abc == "false" || abc == "0") {
    // Nothing to do
  } else {
    error("unrecognized abc {} option. Either true or false", techmap);
  }

  auto gl = Graph_library::instance(path);

  uint32_t max_version = gl->get_max_version();

  Yosys::yosys_setup();

  call_yosys(vars);

  std::vector<LGraph *> lgs;
  gl->each_lgraph([&lgs, gl, max_version, this](Lg_type_id id, std::string_view name) {
    (void)name;
    if (gl->get_version(id) > max_version) {
      LGraph *lg = LGraph::open(path, id);
      if (lg == 0) {
        warn("could not open graph lgid:{} in path:{}", (int)id, path);
      } else {
        lgs.push_back(lg);
      }
    }
  });

  // Yosys::memhasher_off();
  // Yosys::yosys_shutdown();

  var.add(lgs);
}

void Inou_yosys_api::fromlg(Eprp_var &var) {
  Inou_yosys_api p(var, false);

  Yosys::yosys_setup();

  for (auto &lg : var.lgs) {
    mustache::data vars;

    vars.set("path", std::string(p.path));
    vars.set("odir", std::string(p.odir));

    auto file = absl::StrCat(p.odir, "/", lg->get_name(), ".v");
    vars.set("file", std::string(file));
    vars.set("name", std::string(lg->get_name()));

    auto hier = var.get("hier");
    if (!hier.empty() && (hier == "1" || hier == "true")) {
      vars.set("hier", mustache::data::type::bool_true);
    } else {
      vars.set("hier", mustache::data::type::bool_false);
    }

    p.call_yosys(vars);
  }
}

void Inou_yosys_api::setup() {
  Eprp_method m1("inou.yosys.tolg", "read verilog using yosys to lgraph", &Inou_yosys_api::tolg);
  m1.add_label_required("files", "verilog files to process (comma separated)");
  m1.add_label_optional("path", "path to build the lgraph[s]", "lgdb");
  m1.add_label_optional("techmap", "Either full or alumac techmap or none from yosys. Cannot be used with liberty", "");
  m1.add_label_optional("liberty", "Liberty file for technology mapping. Cannot be used with techmap, will call abc for tmap", "");
  m1.add_label_optional("abc", "run ABC inside yosys before loading lgraph", "false");
  m1.add_label_optional("script", "alternative custom inou_yosys_read.ys command");
  m1.add_label_optional("yosys", "path for yosys command", "");
  m1.add_label_optional("top", "define top module, will call yosys hierarchy pass (-auto-top allowed)");

  register_inou("yosys", m1);

  Eprp_method m2("inou.yosys.fromlg", "write verilog using yosys from lgraph", &Inou_yosys_api::fromlg);
  m2.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m2.add_label_optional("odir", "output directory for generated verilog files", ".");
  m2.add_label_optional("script", "alternative custom inou_yosys_write.ys command");
  m2.add_label_optional("yosys", "path for yosys command", "");
  m2.add_label_optional("hier", "hierarchy pass in LiveHD (like flat in yosys)");

  register_inou("yosys", m2);
}
