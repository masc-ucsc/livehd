//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <vector>

#include <boost/filesystem.hpp>

#include "options.hpp"

#include "lglog.hpp"

bool Options_pack::failed      = false;
bool Options_pack::initialized = false;

int    Options::cargc = 0;
char **Options::cargv;

boost::program_options::options_description Options::desc("lgraph inout options setup");

Options_pack::Options_pack()
    : lgdb_path("lgdb")
    , graph_name("") {

  // FIXME3 assert(Options::get_cargc() != 0); // Options::setup(argc,argv) must be called before setup() is called

  if(!initialized) {

    if(failed)
      exit(-1);

    Options::get_desc()->add_options()("lgdb", boost::program_options::value(&lgdb_path), "lg db path")(
        "graph_name", boost::program_options::value(&graph_name), "graph name in the lg db path")("help,h", "print usage message");
  }

  boost::program_options::variables_map vm;
  boost::program_options::store(boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv())
                                    .options(*Options::get_desc())
                                    .allow_unregistered()
                                    .run(),
                                vm);

  if(vm.count("lgdb")) {
    lgdb_path = vm["lgdb"].as<std::string>();
    console->info("setting up lgdb:{} directory\n", lgdb_path);
  }

  if(vm.count("graph_name")) {
    graph_name = vm["graph_name"].as<std::string>();
  }

  if(!initialized) {
    initialized = true;
    boost::filesystem::path p(lgdb_path);
    boost::filesystem::create_directory(p);
  }
}

void Options::setup_lock() {
  boost::program_options::variables_map vm;
  boost::program_options::store(boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv())
                                    .options(*Options::get_desc())
                                    .allow_unregistered()
                                    .run(),
                                vm);

  if(vm.count("help")) {
    std::cout << *Options::get_desc() << "\n";
    exit(0);
  }
}

void Options::setup(std::string cmd) {

  if(cargc) {
    // Delete the old
    for(int i = 0; i < cargc; i++) {
      if(cargv[i])
        delete cargv[i];
    }
    delete cargv;
    cargc = 0;
  }
  assert(cargc == 0);

  std::vector<char *> args;
  std::istringstream  iss(cmd);

  std::string token;
  while(iss >> token) {
    char *arg = new char[token.size() + 1];
    copy(token.begin(), token.end(), arg);
    arg[token.size()] = '\0';
    args.push_back(arg);
  }
  // args.push_back(0);

  cargv = new char *[args.size() + 1];
  for(size_t i = 0; i < args.size(); i++) {
    cargv[i] = args[i];
  }
  cargc        = args.size();
  cargv[cargc] = NULL;
}

void Options::setup(int argc, const char **argv) {

  assert(cargc == 0);

  cargv = (char **)malloc(sizeof(char *) * (argc + 1));
  for(int i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    cargv[i]   = (char *)malloc(sizeof(char) * len);
    strcpy(cargv[i], argv[i]);
  }
  cargv[argc] = NULL;
  cargc       = argc;
}
