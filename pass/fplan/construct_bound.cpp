#include <cstdio>      // for popen, pclose
#include <cstring>     // for strcpy
#include <filesystem>  // for *nix file hierarchy traversal
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "fmt/format.h"
#include "hier_tree.hpp"
#include "profile_time.hpp"

void Hier_tree::construct_bounds(const unsigned int optimal_thresh) {
  for (auto& pat : hier_patterns) {
    std::stringstream instr;
    instr << fmt::format("{}\n", pat.count());
    for (auto gv : pat.verts) {
      Dim gv_dim = leaf_dims[gv.first];
      I(gv_dim.width > max_aspect_ratio && gv_dim.height > max_aspect_ratio);
      instr << fmt::format("{:.4f} {:.4f}\n", gv_dim.width, gv_dim.height);
    }

    // shelling out here because blobb uses static variables that can't easily be reset across calls.
    // TODO: information is passed to blobb by reading and writing files for now, which is slow.

    if (bound_verbose) {
      fmt::print("\ninput string stream:\n{}", instr.str());
    }

    std::ofstream blobb_inf;

    blobb_inf.open("/tmp/infile.txt", std::ios::out | std::ios::trunc);
    I(blobb_inf);
    blobb_inf << instr.str();
    blobb_inf.close();

    // should be <...>/livehd (symlink)
    auto curr_p = std::filesystem::current_path();

    auto blobb_p = curr_p / "third_party" / "misc" / "blobb_compass" / "bin" / "blobb";
    if (!std::filesystem::exists(blobb_p) || !std::filesystem::is_symlink(blobb_p)) {
      throw std::runtime_error(fmt::format("No binary found in {}!", blobb_p.string()));
    }

    const std::string files      = "/tmp/infile.txt /tmp/outfile.bbb";
    const std::string fixed_args = "--slicing --free-orient -t";
    const std::string alg        = (pat.count() < optimal_thresh) ? "--backtrack" : "--hierarchical";

    int dead_p = 15;  // when set to the default (5%), small floorplans fail frequently

    std::ifstream blobb_outf;

    // run BloBB with increasing deadspace percentages until we get a floorplan
    profile_time::timer t;
    t.start();

    fmt::print("    running blobb...");
    do {
      std::string deadspace = fmt::format("-d {}", dead_p);
      dead_p += 10;

      const std::string command = fmt::format("{} {} {} {} {}", blobb_p.string(), files, fixed_args, alg, deadspace);

      // forking requires C file handles :/
      FILE* cfstream = popen(command.c_str(), "r");
      I(cfstream);

      // even if we don't print the output of BloBB, we still have to read it or BloBB will hang?
      char c;
      while ((c = fgetc(cfstream)) != EOF) {
        // putchar(ch);
      }

      pclose(cfstream);

      blobb_outf.open("/tmp/outfile.bbb");
      I(blobb_outf);

      if (blobb_outf.peek() == std::ifstream::traits_type::eof()) {
        if (bound_verbose) {
          fmt::print("no output was sent to file, trying with {}% deadspace...\n", dead_p);
        }
        blobb_outf.close();
      }

    } while (!blobb_outf.is_open() && dead_p < 100);

    fmt::print("done ({} ms).\n", t.time());

    I(dead_p < 100);

    blobb_outf >> pat.d.width;
    blobb_outf >> pat.d.height;

    // TODO: read out the rest of the floorplan as well and save for later

    blobb_outf.close();
  }
}