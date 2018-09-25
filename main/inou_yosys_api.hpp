//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ext/stdio_filebuf.h>

#include <fstream>
#include <iostream>
#include <string>

#include "lgraph.hpp"
#include "main_api.hpp"
#include "mustache.hpp"

using namespace kainjow;

class Inou_yosys_api {
protected:
static void tolg(Eprp_var &var) {

  const std::string path    = var.get("path","lgdb");
  const std::string script  = var.get("script");
  const std::string yosys   = var.get("yosys","yosys");
  const std::string techmap = var.get("techmap","alumac");
  const std::string abc     = var.get("abc","false");
  const std::string files   = var.get("files");

  const auto &main_path = Main_api::get_main_path();
  std::string liblg = main_path + "/lgraph.runfiles/__main__/inou/yosys/liblgraph_yosys.so";
  if(access(liblg.c_str(), X_OK) == -1) {
    // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
    const std::string liblg2 = main_path + "/../share/lgraph/inou/yosys/liblgraph_yosys.so";
    if(access(liblg2.c_str(), X_OK) == -1) {
      Main_api::error(fmt::format("could not find liblgraph_yosys.so, the {} is not executable", liblg));
      return;
    }
    liblg = liblg2;
  }

  std::string script_file;
  if (script.empty()) {
    script_file = main_path + "/lgraph.runfiles/__main__/main/inou_yosys_read.ys";
    if(access(script_file.c_str(), R_OK) == -1) {
      // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
      const std::string script_file2 = main_path + "/../share/lgraph/main/inou_yosys_read.ys";
      if(access(script_file2.c_str(), X_OK) == -1) {
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

  if (files.empty()) {
    Main_api::error(fmt::format("inou.yosys.tolg: no files provided"));
    return;
  }

  char seps[] = ",";
  char *token;

  std::vector<std::string> raw_file_list;

  char *files_char = (char *)alloca(files.size());
  strcpy(files_char,files.c_str());
  token = std::strtok(files_char, seps);
  while( token != NULL ) {
    /* Do your thing */
    if(access(token, R_OK) == -1) {
      Main_api::error(fmt::format("inou.yosys.tolf: could not open file {} {} {}", token, files_char,files));
      return;
    }
    raw_file_list.push_back(token);

    token = std::strtok( NULL, seps );
  }


  std::ifstream inFile;
  inFile.open(script_file);

  std::stringstream strStream;
  strStream << inFile.rdbuf(); //read the whole file

  mustache::mustache tmpl(strStream.str());
  mustache::data vars;

  mustache::data filelist{mustache::data::type::list};
  for(const auto &f:raw_file_list) {
    filelist << mustache::data{"file", f};
  }

  vars.set("filelist",filelist);

  if (strcasecmp(techmap.c_str(),"alumacc") == 0) {
    vars.set("techmap_alumacc", mustache::data::type::bool_true);
  }else if (strcasecmp(techmap.c_str(),"full") == 0) {
    vars.set("techmap_full", mustache::data::type::bool_true);
  }else if (strcasecmp(techmap.c_str(),"none") == 0) {
    // Nothing
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

  vars.set("path",path);

  int pipefd[2];
  pipe(pipefd);

  int pid = fork();
  if (pid<0) {
    Main_api::error(fmt::format("inou.yosys.tolf: unable to fork??"));
    return;
  }

  if (pid==0) { // Child

    dup2(pipefd[0], 0); // stdin
    close(pipefd[1]);   // unused

    //int fd = creat("yosys.log", 0644);
    //close(1);
    //dup2(fd, 1); // stdout

    char *argv[] = { strdup(yosys.c_str()), strdup("-q"), strdup("-m"), strdup(liblg.c_str()), 0};
    std::string command = fmt::format("CMD: {} -q -m {}", yosys, liblg);
    std::cout << command << std::endl;

    if (execvp(yosys.c_str(), argv) < 0) {
      Main_api::error(fmt::format("inou.yosys.tolf: execvp fail with {}", strerror(errno)));
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
      Main_api::error(fmt::format("inou.yosys.tolf: waitpid fail with {}", strerror(errno)));
      return;
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

}


  Inou_yosys_api() {
  }
public:

  static void setup(Eprp &eprp) {
    Eprp_method m1("inou.yosys.tolg", "read verilog using yosys to lgraph", &Inou_yosys_api::tolg);
    m1.add_label_required("files","verilog files to process (comma separated)");
    m1.add_label_optional("path","path to build the lgraph[s]");
    m1.add_label_optional("techmap","Either full or alumac techmap or none from yosys");
    m1.add_label_optional("abc","run ABC inside yosys before loading lgraph");
    m1.add_label_optional("script","alternative custom inou_yosys_read.ys command");
    m1.add_label_optional("yosys","path for yosys command");

    eprp.register_method(m1);
  }

};

