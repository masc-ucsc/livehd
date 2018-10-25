//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ext/stdio_filebuf.h>

#include <fstream>
#include <iostream>

#include "lgraph.hpp"
#include "main_api.hpp"
#include "mustache.hpp"
#include "eprp_utils.hpp"
#include "inou_yosys_api.hpp"

void Inou_yosys_api::set_script_liblg(Eprp_var &var, std::string &script_file, std::string &liblg, bool do_read) {

  const std::string script  = var.get("script");

  const auto &main_path = Main_api::get_main_path();
  liblg = main_path + "/lgshell.runfiles/__main__/inou/yosys/liblgraph_yosys.so";
  fmt::print("1.yosys path:{} liblg:{}\n", main_path, liblg);
  if(access(liblg.c_str(), X_OK) == -1) {
    // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
    const std::string liblg2 = main_path + "/../share/lgraph/inou/yosys/liblgraph_yosys.so";
    fmt::print("1.yosys path:{} liblg:{}\n", main_path, liblg2);
    if(access(liblg2.c_str(), X_OK) == -1) {

      //sandbox path
      const std::string liblg3 = main_path + "/inou/yosys/liblgraph_yosys.so";
      fmt::print("1.yosys path:{} liblg:{}\n", main_path, liblg2);
      if(access(liblg3.c_str(), X_OK) == -1) {
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
    if(access(script_file.c_str(), R_OK) == -1) {
      // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
      const std::string script_file2 = main_path + "/../share/lgraph/main/" + do_read_str;
      if(access(script_file2.c_str(), R_OK) == -1) {
        Main_api::error(fmt::format("could not find the default script:{} file", script_file));
        return;
      }
      script_file = script_file2;
    }
  }else{
    if(access(script.c_str(), X_OK) == -1) {
      Main_api::error(fmt::format("could not find the provided script:{} file", script));
      return;
    }
    script_file = script;
  }
}


//TODO: this should go once we have a proper lib file in c++
int Inou_yosys_api::create_lib(const std::string &lib_file, const std::string& lgdb) {

  std::string ofile = lgdb + "/tech_library";

  fmt::print("creating tech_library file for {} in {}",lib_file, ofile);
  int fail = 0;

  int pid = fork();
  if (pid<0) {
    Main_api::error(fmt::format("inou.yosys: unable to fork??"));
    return -1;
  }

  if (pid==0) { // Child
    mkdir(lgdb.c_str(), 0755);

    //redirect stdout to tech_file
    int output = open(ofile.c_str(), O_WRONLY | O_CREAT, S_IRWXU);
    dup2 (output, STDOUT_FILENO);

    std::string tech_parser = "./inou/tech/func_liberty_json.sh";
    char *argv[] = { strdup(tech_parser.c_str()), strdup(lib_file.c_str()), 0};

    if (execvp(tech_parser.c_str(), argv) < 0) {
      Main_api::error(fmt::format("tech_library generation failed for {} in {}, will not call yosys",lib_file, ofile));
    }
    exit(0);
  }

  waitpid(pid, &fail, WUNTRACED | WCONTINUED);
  return fail;
}

int Inou_yosys_api::do_work(const std::string &yosys, const std::string &liblg, const std::string &script_file, mustache::data &vars) {

  fmt::print("yosys do work {} -m {} using {}",yosys, liblg, script_file);

  std::ifstream inFile;
  inFile.open(script_file);

  std::stringstream strStream;
  strStream << inFile.rdbuf(); //read the whole file

  mustache::mustache tmpl(strStream.str());

  int pipefd[2];
  pipe(pipefd);

  int pid = fork();
  if (pid<0) {
    Main_api::error(fmt::format("inou.yosys: unable to fork??"));
    return -1;
  }

  if (pid==0) { // Child

    dup2(pipefd[0], 0); // stdin
    close(pipefd[1]);   // unused

    char *argv[] = { strdup(yosys.c_str()), strdup("-q"), strdup("-m"), strdup(liblg.c_str()), 0};

    if (execvp(yosys.c_str(), argv) < 0) {
      Main_api::error(fmt::format("inou.yosys: execvp fail with {}", strerror(errno)));
    }

    exit(0);
  }
  // parent
  close(pipefd[0]); // unsused

  const std::string rendered = tmpl.render(vars);
  write(pipefd[1],rendered.c_str(), rendered.size());
  close(pipefd[1]); // Force flush

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

  const std::string path    = var.get("path","lgdb");
  const std::string yosys   = var.get("yosys","yosys");
  const std::string techmap = var.get("techmap","none");
  const std::string abc     = var.get("abc","false");
  const std::string files   = var.get("files");
  const std::string top     = var.get("top","");
  const std::string lib     = var.get("liberty","");

  std::string script_file;
  std::string liblg;
  set_script_liblg(var, script_file, liblg, true);

  if (files.empty()) {
    Main_api::error(fmt::format("inou.yosys.tolg: no files provided"));
    return;
  }

  mustache::data vars;

  vars.set("path",path);

  mustache::data filelist{mustache::data::type::list};
  for(const auto &f:Eprp_utils::parse_files(files,"inou.yosys.tolg")) {
    filelist << mustache::data{"input", f};
  }

  vars.set("filelist",filelist);

  if(!top.empty()) {
    vars.set("hierarchy", mustache::data::type::bool_true);
    if(top != "-auto-top") {
      vars.set("top","-top " + top);
    } else {
      vars.set("top",top);
    }
  } else {
    vars.set("hierarchy", mustache::data::type::bool_false);
  }

  if (strcasecmp(techmap.c_str(),"alumacc") == 0) {
    vars.set("techmap_alumacc", mustache::data::type::bool_true);
  }else if (strcasecmp(techmap.c_str(),"full") == 0) {
    vars.set("techmap_full", mustache::data::type::bool_true);
  }else if (strcasecmp(techmap.c_str(),"none") == 0) {
    if(!lib.empty()) {
      vars.set("liberty_tmap", mustache::data::type::bool_true);
      vars.set("liberty_file", lib);

      create_lib(lib, path);

    } else {
      // Nothing
    }
  }else{
    Main_api::error(fmt::format("inou.yosys.tolf: unrecognized techmap {} option. Either full or alumacc", techmap));
    return;
  }
  if (strcasecmp(abc.c_str(),"true") == 0) {
    vars.set("abc_in_yosys", mustache::data::type::bool_true);
  }else if (strcasecmp(abc.c_str(),"false") == 0) {
    // Nothing to do
  }else{
    Main_api::error(fmt::format("inou.yosys.tolf: unrecognized abc {} option. Either true or false", techmap));
  }

  auto gl = Graph_library::instance(path);

  int max_version = gl->get_max_version();

  gl->sync(); // Before calling remote thread in do_work

  do_work(yosys, liblg, script_file, vars);

  gl->reload(); // after the do_work

  std::vector<LGraph *> lgs;
  gl->each_graph([&lgs, gl, max_version, path](const std::string &name, int id) {
      if (gl->get_version(id) > max_version) {
        LGraph *lg = LGraph::open(path, id);
        if (lg==0) {
          Main_api::warn(fmt::format("could not open graph {}", name));
        }else{
          lgs.push_back(lg);
        }
      }
    });

  var.add(lgs);
}

void Inou_yosys_api::fromlg(Eprp_var &var) {
  const std::string path  = var.get("path","lgdb");
  const std::string yosys = var.get("yosys","yosys");
  const std::string odir  = var.get("odir",".");

  std::string script_file;
  std::string liblg;
  set_script_liblg(var, script_file, liblg, false);

  for(auto &lg:var.lgs) {
    mustache::data vars;

    vars.set("path", path);
    vars.set("odir", odir);

    std::string file = odir + "/" + lg->get_name() + ".v";
    vars.set("file", file);
    vars.set("name", lg->get_name());

    do_work(yosys, liblg, script_file, vars);
  }
}

void Inou_yosys_api::setup(Eprp &eprp) {
  Eprp_method m1("inou.yosys.tolg", "read verilog using yosys to lgraph", &Inou_yosys_api::tolg);
  m1.add_label_required("files","verilog files to process (comma separated)");
  m1.add_label_optional("path","path to build the lgraph[s]");
  m1.add_label_optional("techmap","Either full or alumac techmap or none from yosys. Cannot be used with liberty");
  m1.add_label_optional("liberty","Liberty file for technology mapping. Cannot be used with techmap, will call abc for tmap");
  m1.add_label_optional("abc","run ABC inside yosys before loading lgraph");
  m1.add_label_optional("script","alternative custom inou_yosys_read.ys command");
  m1.add_label_optional("yosys","path for yosys command");
  m1.add_label_optional("top","define top module, will call yosys hierarchy pass (-auto-top allowed)");

  eprp.register_method(m1);

  Eprp_method m2("inou.yosys.fromlg", "write verilog using yosys from lgraph", &Inou_yosys_api::fromlg);
  m2.add_label_optional("path","path to read the lgraph[s]");
  m2.add_label_optional("odir","output directory for generated verilog files");
  m2.add_label_optional("script","alternative custom inou_yosys_write.ys command");
  m2.add_label_optional("yosys","path for yosys command");

  eprp.register_method(m2);
}

