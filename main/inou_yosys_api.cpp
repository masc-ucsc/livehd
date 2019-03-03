//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ext/stdio_filebuf.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

#include "iassert.hpp"

#include "eprp_utils.hpp"
#include "inou_yosys_api.hpp"
#include "lgraph.hpp"
#include "main_api.hpp"
#include "mustache.hpp"

void Inou_yosys_api::set_script_liblg(Eprp_var &var, std::string &script_file, std::string &liblg, bool do_read) {
  auto script = var.get("script");

  const auto &main_path = Main_api::get_main_path();
  liblg                 = main_path + "/lgshell.runfiles/__main__/inou/yosys/liblgraph_yosys.so";
  if (access(liblg.c_str(), X_OK) == -1) {
    // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
    const std::string liblg2 = main_path + "/../share/lgraph/inou/yosys/liblgraph_yosys.so";
    if (access(liblg2.c_str(), X_OK) == -1) {
      // sandbox path
      const std::string liblg3 = main_path + "/inou/yosys/liblgraph_yosys.so";
      if (access(liblg3.c_str(), X_OK) == -1) {
        Main_api::error(fmt::format("could not find liblgraph_yosys.so, the {} is not executable", liblg));
        return;
      } else {
        liblg = liblg3;
      }
    } else {
      liblg = liblg2;
    }
  }

  if (script.empty()) {
    std::string do_read_str;
    if (do_read)
      do_read_str = "inou_yosys_read.ys";
    else
      do_read_str = "inou_yosys_write.ys";

    script_file = main_path + "/lgshell.runfiles/__main__/main/" + do_read_str;
    if (access(script_file.c_str(), R_OK) == -1) {
      // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
      const std::string script_file2 = main_path + "/../share/lgraph/main/" + do_read_str;
      if (access(script_file2.c_str(), R_OK) == -1) {
        Main_api::error(fmt::format("could not find the default script:{} file", script_file));
        return;
      }
      script_file = script_file2;
    }
  } else {
    if (access(std::string(script).c_str(), X_OK) == -1) {
      Main_api::error(fmt::format("could not find the provided script:{} file", script));
      return;
    }
    script_file = script;
  }
}

// TODO: this should go once we have a proper lib file in c++
int Inou_yosys_api::create_lib(std::string_view lib_file, std::string_view lgdb) {
  std::string ofile(lgdb);
  ofile += "/tech_library";

  fmt::print("creating tech_library file for {} in {}", lib_file, ofile);
  int fail = 0;

  int pid = fork();
  if (pid < 0) {
    Main_api::error(fmt::format("inou.yosys: unable to fork??"));
    return -1;
  }

  if (pid == 0) {  // Child
    mkdir(std::string(lgdb).c_str(), 0755);

    // redirect stdout to tech_file
    int output = open(ofile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    dup2(output, STDOUT_FILENO);

    std::string tech_parser = "./inou/tech/func_liberty_json.sh";
    char *      argv[]      = {strdup(tech_parser.c_str()), strdup(std::string(lib_file).c_str()), 0};

    if (execvp(tech_parser.c_str(), argv) < 0) {
      Main_api::error(fmt::format("tech_library generation failed for {} in {}, will not call yosys", lib_file, ofile));
    }
    exit(0);
  }

  waitpid(pid, &fail, WUNTRACED | WCONTINUED);
  return fail;
}

int Inou_yosys_api::do_work(std::string_view yosys, std::string_view liblg, std::string_view script_file, mustache::data &vars) {
  fmt::print("yosys do work {} -m {} using {}", yosys, liblg, script_file);

  std::ifstream inFile;
  inFile.open(std::string(script_file));
  if (!inFile.good()) throw std::runtime_error(fmt::format("inou_yosys_api: could not open {}", script_file));

  std::stringstream strStream;
  strStream << inFile.rdbuf();  // read the whole file

  mustache::mustache tmpl(strStream.str());
  tmpl.set_custom_escape([](const std::string& s) { return s; }); // No HTML escape

  int pid = fork();
  if (pid < 0) {
    Main_api::error(fmt::format("inou.yosys: unable to fork??"));
    return -1;
  }

  if (pid == 0) {  // Child
    const std::string yosys_cmd = tmpl.render(vars);
    char *            argv[]    = {strdup(std::string(yosys).c_str()),
                    strdup("-q"),
                    strdup("-m"),
                    strdup(std::string(liblg).c_str()),
                    strdup("-p"),
                    strdup(std::string(yosys_cmd).c_str()),
                    0};

    if (execvp(std::string(yosys).c_str(), argv) < 0) {
      Main_api::error(fmt::format("inou.yosys: execvp fail with {}", strerror(errno)));
    }

    exit(0);
  }

  int wstatus;
#if 1
  do {
    int w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
    if (w == -1) {
      Main_api::error(fmt::format("inou.yosys: waitpid fail with {}", strerror(errno)));
      return errno;
    }

    if (WIFEXITED(wstatus)) {
      printf("exited, status=%d\n", WEXITSTATUS(wstatus));
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
}

void Inou_yosys_api::tolg(Eprp_var &var) {
  auto path    = var.get("path");
  auto yosys   = var.get("yosys");
  auto techmap = var.get("techmap");
  auto abc     = var.get("abc");
  auto files   = var.get("files");
  auto top     = var.get("top");
  auto lib     = var.get("liberty");

  std::string script_file;
  std::string liblg;
  set_script_liblg(var, script_file, liblg, true);

  if (files.empty()) {
    Main_api::error(fmt::format("inou.yosys.tolg: no files provided"));
    return;
  }

  mustache::data vars;
  vars.set("path", std::string(path));

  mustache::data filelist{mustache::data::type::list};
  for (const auto &f : absl::StrSplit(files, ',')) {
    I(!files.empty());
    filelist << mustache::data{"input", std::string(f)};
  }

  vars.set("filelist", filelist);

  if (!top.empty()) {
    vars.set("hierarchy", mustache::data::type::bool_true);
    if (top != "-auto-top") {
      vars.set("top", "-top " + std::string(top));
    } else {
      vars.set("top", std::string(top));
    }
  } else {
    vars.set("hierarchy", mustache::data::type::bool_false);
  }

  if (techmap == "alumacc") {
    vars.set("techmap_alumacc", mustache::data::type::bool_true);
  } else if (techmap == "full") {
    vars.set("techmap_full", mustache::data::type::bool_true);
  } else if (techmap == "none") {
    if (!lib.empty()) {
      vars.set("liberty_tmap", mustache::data::type::bool_true);
      vars.set("liberty_file", std::string(lib));

      create_lib(lib, path);

    } else {
      // Nothing
    }
  } else {
    Main_api::error(fmt::format("inou.yosys.tolf: unrecognized techmap {} option. Either full or alumacc", techmap));
    return;
  }
  if (abc == "true" || abc == "1") {
    vars.set("abc_in_yosys", mustache::data::type::bool_true);
  } else if (abc == "false" || abc == "0") {
    // Nothing to do
  } else {
    Main_api::error(fmt::format("inou.yosys.tolf: unrecognized abc {} option. Either true or false", techmap));
  }

  auto gl = Graph_library::instance(path);

  uint32_t max_version = gl->get_max_version();

  gl->sync();  // Before calling remote thread in do_work

  do_work(yosys, liblg, script_file, vars);

  gl->reload();  // after the do_work

  std::vector<LGraph *> lgs;
  gl->each_type([&lgs, gl, max_version, path](Lg_type_id id, std::string_view name) {
    if (gl->get_version(id) > max_version) {
      LGraph *lg = LGraph::open(path, id);
      if (lg == 0) {
        Main_api::warn(fmt::format("could not open graph {}", name));
      } else {
        lgs.push_back(lg);
      }
    }
  });

  var.add(lgs);
}

void Inou_yosys_api::fromlg(Eprp_var &var) {
  auto path  = var.get("path");
  auto yosys = var.get("yosys");
  auto odir  = var.get("odir");

  std::string script_file;
  std::string liblg;
  set_script_liblg(var, script_file, liblg, false);

  for (auto &lg : var.lgs) {
    mustache::data vars;

    vars.set("path", std::string(path));
    vars.set("odir", std::string(odir));

    auto file = absl::StrCat(odir, "/", lg->get_name(), ".v");
    vars.set("file", std::string(file));
    vars.set("name", std::string(lg->get_name()));

    do_work(yosys, liblg, script_file, vars);
  }
}

void Inou_yosys_api::setup(Eprp &eprp) {
  std::string yosys;
  yosys = "/usr/bin/yosys";
  if (access(yosys.c_str(), X_OK) == -1) {
    yosys = "/usr/local/bin/yosys";
    if (access(yosys.c_str(), X_OK) == -1) {
      yosys = "yosys";
    }
  }

  Eprp_method m1("inou.yosys.tolg", "read verilog using yosys to lgraph", &Inou_yosys_api::tolg);
  m1.add_label_required("files", "verilog files to process (comma separated)");
  m1.add_label_optional("path", "path to build the lgraph[s]", "lgdb");
  m1.add_label_optional("techmap", "Either full or alumac techmap or none from yosys. Cannot be used with liberty", "none");
  m1.add_label_optional("liberty", "Liberty file for technology mapping. Cannot be used with techmap, will call abc for tmap", "");
  m1.add_label_optional("abc", "run ABC inside yosys before loading lgraph", "false");
  m1.add_label_optional("script", "alternative custom inou_yosys_read.ys command");
  m1.add_label_optional("yosys", "path for yosys command", yosys);
  m1.add_label_optional("top", "define top module, will call yosys hierarchy pass (-auto-top allowed)");

  eprp.register_method(m1);

  Eprp_method m2("inou.yosys.fromlg", "write verilog using yosys from lgraph", &Inou_yosys_api::fromlg);
  m2.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m2.add_label_optional("odir", "output directory for generated verilog files", ".");
  m2.add_label_optional("script", "alternative custom inou_yosys_write.ys command");
  m2.add_label_optional("yosys", "path for yosys command", yosys);

  eprp.register_method(m2);
}
