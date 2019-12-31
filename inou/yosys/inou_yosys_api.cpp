//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_yosys_api.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ext/stdio_filebuf.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "eprp_utils.hpp"
#include "iassert.hpp"
#include "lgraph.hpp"
#include "mustache.hpp"

void setup_inou_yosys() { Inou_yosys_api::setup(); }

Inou_yosys_api::Inou_yosys_api(Eprp_var &var, bool do_read) : Pass("inou.yosys", var) {
  yosys = var.get("yosys");
  set_script_liblg(var, do_read);
}

void Inou_yosys_api::set_script_liblg(const Eprp_var &var, bool do_read) {
  auto script = var.get("script");
  liblg       = var.get("liblg");

  auto main_path = Eprp_utils::get_exe_path();

  if (liblg.empty()) {
    liblg = main_path + "/lgshell.runfiles/lgraph/inou/yosys/liblgraph_yosys.so";
    if (access(liblg.c_str(), X_OK) == -1) {
      // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
      const std::string liblg2 = main_path + "/../share/lgraph/inou/yosys/liblgraph_yosys.so";
      if (access(liblg2.c_str(), X_OK) == -1) {
        // sandbox path
        const std::string liblg3 = main_path + "/inou/yosys/liblgraph_yosys.so";
        if (access(liblg3.c_str(), X_OK) != -1) {
          liblg = liblg3;
        }
      } else {
        liblg = liblg2;
      }
    }
  }
  if (access(liblg.c_str(), X_OK) == -1) {
    error(fmt::format("could not find liblgraph_yosys.so, the {} is not executable\n", liblg));
    return;
  }

  if (script.empty()) {
    std::string do_read_str;
    if (do_read)
      do_read_str = "inou_yosys_read.ys";
    else
      do_read_str = "inou_yosys_write.ys";

    script_file = main_path + "/lgshell.runfiles/lgraph/inou/yosys/" + do_read_str;
    if (access(script_file.c_str(), R_OK) == -1) {
      // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
      const std::string script_file2 = main_path + "/../share/lgraph/inou/yosys/" + do_read_str;
      if (access(script_file2.c_str(), R_OK) == -1) {
        const std::string script_file3 = main_path + "/inou/yosys/" + do_read_str;
        if (access(script_file3.c_str(), R_OK) != -1) {
          script_file = script_file3;
        }
      } else {
        script_file = script_file2;
      }
    }
  }else{
    script_file = script;
  }

  if (access(std::string(script_file).c_str(), R_OK) != F_OK) {
    error("could not find the provided script:{} file", script_file);
    return;
  }

}

int Inou_yosys_api::create_lib(const std::string &lib_file, const std::string &lgdb) {
  std::string ofile(lgdb);
  ofile += "/tech_library";

  fmt::print("creating tech_library file for {} in {}", lib_file, ofile);
  int fail = 0;

  int pid = fork();
  if (pid < 0) {
    error("inou.yosys: unable to fork??");
    return -1;
  }

  if (pid == 0) {  // Child
    mkdir(lgdb.c_str(), 0755);

    // redirect stdout to tech_file
    int output = open(ofile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    dup2(output, STDOUT_FILENO);

    std::string tech_parser = "./inou/tech/func_liberty_json.sh";
    char *      argv[]      = {strdup(tech_parser.c_str()), strdup(lib_file.c_str()), 0};

    if (execvp(tech_parser.c_str(), argv) < 0) {
      error("tech_library generation failed for {} in {}, will not call yosys", lib_file, ofile);
    }
    exit(0);
  }

  waitpid(pid, &fail, WUNTRACED | WCONTINUED);
  return fail;
}

int Inou_yosys_api::call_yosys(mustache::data &vars) {
  std::ifstream inFile;
  inFile.open(std::string(script_file));
  if (!inFile.good()) throw std::runtime_error(fmt::format("inou_yosys_api: could not open {}", script_file));

  std::stringstream strStream;
  strStream << inFile.rdbuf();  // read the whole file

  mustache::mustache tmpl(strStream.str());
  tmpl.set_custom_escape([](const std::string &s) { return s; });  // No HTML escape

  const std::string yosys_cmd = tmpl.render(vars);
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

  fmt::print("yosys {} synthesis cmd: {} -m {} using {}\n", filename, yosys, liblg, script_file);

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

    char *argv[] = {strdup(std::string(yosys).c_str()),
                    strdup("-q"),
                    strdup("-m"),
                    strdup(std::string(liblg).c_str()),
                    strdup("-s"),
                    strdup(filename),
                    0};

    if (execvp(std::string(yosys).c_str(), argv) < 0) {
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

  if (techmap == "alumacc") {
    vars.set("techmap_alumacc", mustache::data::type::bool_true);
  } else if (techmap == "full") {
    vars.set("techmap_full", mustache::data::type::bool_true);
  } else if (techmap == "none") {
    if (!lib.empty()) {
      vars.set("liberty_tmap", mustache::data::type::bool_true);
      vars.set("liberty_file", lib);

      create_lib(lib, path);

    } else {
      // Nothing
    }
  } else {
    error("unrecognized techmap {} option. Either full or alumacc", techmap);
    return;
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

  gl->sync();  // Before calling remote thread in call_yosys

  call_yosys(vars);

  gl->reload();  // after the call_yosys

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

  var.add(lgs);
}

void Inou_yosys_api::fromlg(Eprp_var &var) {
  Inou_yosys_api p(var, false);

  for (auto &lg : var.lgs) {
    mustache::data vars;

    vars.set("path", std::string(p.path));
    vars.set("odir", std::string(p.odir));

    auto file = absl::StrCat(p.odir, "/", lg->get_name(), ".v");
    vars.set("file", std::string(file));
    vars.set("name", std::string(lg->get_name()));

    p.call_yosys(vars);
  }
}

void Inou_yosys_api::setup() {
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
  m1.add_label_optional("liblg", "path for libgraph_yosys.so library");
  m1.add_label_optional("top", "define top module, will call yosys hierarchy pass (-auto-top allowed)");

  register_inou("yosys", m1);

  Eprp_method m2("inou.yosys.fromlg", "write verilog using yosys from lgraph", &Inou_yosys_api::fromlg);
  m2.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m2.add_label_optional("odir", "output directory for generated verilog files", ".");
  m2.add_label_optional("script", "alternative custom inou_yosys_write.ys command");
  m2.add_label_optional("yosys", "path for yosys command", yosys);

  register_inou("yosys", m2);
}
