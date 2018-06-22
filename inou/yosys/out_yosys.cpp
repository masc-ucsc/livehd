//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <assert.h>
#include <map>
#include <set>
#include <string>

#include "dump_yosys.hpp"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// each pass contains a singleton object that is derived from Pass
// note that this is a frontend to yosys
struct Dump_Yosys_Pass : public Pass {
  Dump_Yosys_Pass() : Pass("dump_yosys", "converts lgraph to yosys") {}
  virtual void help() {
    //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
    log("\n");
    log("    dump_yosys [options]\n");
    log("\n");
    log("Reads lgraph(s) into yosys.\n");
    log("\n");
    log("    -hierarchy [optional]\n");
    log("        Reads the whole hierarchy bellow the graph_name indicated\n");
    log("    -graph_name (required)\n");
    log("        Specify the graphs name to read\n");
    log("    -directory [default=lgdb]\n");
    log("        Specify from which directory to read\n");
    log("\n");
    log("\n");
  }

  virtual void execute(std::vector<std::string> args, RTLIL::Design *design) {
    log_header(design, "Executing dump_yosys pass (convert from lgraph to yosys).\n");

    // parse options
    size_t      argidx;
    bool        single_graph_mode = false;
    bool        hierarchy         = false;
    std::string graph_name;
    std::string input_directory = "lgdb";

    for(argidx = 1; argidx < args.size(); argidx++) {
      if(args[argidx] == "-graph_name") {
        single_graph_mode = true;
        graph_name        = args[++argidx];
        continue;
      }
      if(args[argidx] == "-directory") {
        input_directory = args[++argidx];
        continue;
      }
      if(args[argidx] == "-hierarchy") {
        hierarchy = true;
        continue;
      }
      break;
    }

    // handle extra options (e.g. selection)
    extra_args(args, argidx, design);

    if(single_graph_mode) {
      if(!hierarchy) {
        log("converting graph %s in directory %s\n.", graph_name.c_str(), input_directory.c_str());
      } else {
        log("converting graph %s and all its subgraphs in directory %s\n.", graph_name.c_str(), input_directory.c_str());
      }
    } else {
      log("converting all graphs in directory %s.\n", input_directory.c_str());
    }
    if(single_graph_mode) {
      const char *argvs2[] = {"", "--lgdb", input_directory.c_str(), "--graph_name", graph_name.c_str()};
      Options::setup(5, argvs2);
    } else {
      const char *argvs2[] = {"", "--lgdb", input_directory.c_str()};
      Options::setup(3, argvs2);
    }

    std::set<LGraph *> generated;
    Options_pack       opt;
    Dump_yosys         dumper(design, opt, hierarchy);

    //read existing graph
    std::vector<LGraph *> lgs = dumper.generate();

    //export to yosys
    dumper.generate(lgs);
    for(auto *g : lgs) {
      generated.insert(g);
    }

    while(dumper.subgraphs().size() >= generated.size()) {
      std::vector<LGraph *> lgs_;
      for(auto *g : dumper.subgraphs()) {
        if(generated.find(g) == generated.end()) {
          lgs_.push_back(g);
          generated.insert(g);
        }
      }
      dumper.generate(lgs_);
    }
  }

} Dump_Yosys_Pass;

PRIVATE_NAMESPACE_END
