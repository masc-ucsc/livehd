//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

#include <assert.h>

#include <map>
#include <set>
#include <string>

#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include "lbench.hpp"
#include "lgyosys_dump.hpp"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// each pass contains a singleton object that is derived from Pass
// note that this is a frontend to yosys
struct LG2Yosys_Pass : public Yosys::Pass {
  LG2Yosys_Pass() : Pass("lg2yosys", "converts lgraph to yosys") {}
  virtual void help() {
    //   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
    log("\n");
    log("    lg2yosys [options]\n");
    log("\n");
    log("Reads lgraph(s) into yosys.\n");
    log("\n");
    log("    -hierarchy [optional]\n");
    log("        Reads the whole hierarchy bellow the graph name indicated\n");
    log("    -name (required)\n");
    log("        Specify the graphs name to read\n");
    log("    -path [default=lgdb]\n");
    log("        Specify from which path to read\n");
    log("\n");
    log("\n");
  }

  virtual void execute(std::vector<std::string> args, RTLIL::Design *design) {
    log_header(design, "Executing lg2yosys pass (convert from lgraph to yosys).\n");

    // parse options
    size_t      argidx;
    bool        single_graph_mode = false;
    bool        hierarchy         = false;
    std::string name;
    std::string path = "lgdb";

    for (argidx = 1; argidx < args.size(); argidx++) {
      if (args[argidx] == "-name") {
        single_graph_mode = true;
        name              = args[++argidx];
        continue;
      }
      if (args[argidx] == "-path") {
        path = args[++argidx];
        continue;
      }
      if (args[argidx] == "-hierarchy") {
        hierarchy = true;
        continue;
      }
      break;
    }

    // handle extra options (e.g. selection)
    extra_args(args, argidx, design);

    Lbench b("inou.yosys.fromlg");

    std::vector<LGraph *> lgs;
    if (single_graph_mode) {
      LGraph *lg = LGraph::open(path, name);
      if (lg == 0) {
        log_error("could not open graph %s in path %s\n.", name.c_str(), path.c_str());
      } else {
        lgs.push_back(lg);
      }
      if (!hierarchy) {
        log("converting graph %s in path %s\n.", name.c_str(), path.c_str());
      } else {
        log("converting graph %s and all its subgraphs in path %s\n.", name.c_str(), path.c_str());
      }
    } else {
      // FIXME: create lgraph::open_all(path);
      log("converting all graphs in path %s.\n", path.c_str());
    }

    std::set<LGraph *> generated;
    Lgyosys_dump       dumper(design, hierarchy);

    dumper.fromlg(lgs);
    for (auto *g : lgs) {
      generated.insert(g);
    }
  }

} Lg2yosys_Pass;

PRIVATE_NAMESPACE_END
