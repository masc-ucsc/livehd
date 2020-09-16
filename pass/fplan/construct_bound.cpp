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

constexpr double max_aspect_ratio = 1.0 / 5.0;

// TODO: pointless to generate leaf dims if the leaves are collapsed into something else in the hierarchy
void Hier_tree::generate_leaf_dims(const unsigned int ndims) {
  static std::default_random_engine     gen;
  static std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

  I(ndims > 0);

  for (auto v : ginfo.al.verts()) {
    if (leaf_dims.count(ginfo.labels(v)) > 0) {
      continue;
    }

    for (size_t i = 0; i < ndims; i++) {
      double width_factor = dist(gen);
      double area         = ginfo.areas(v);

      leaf_dims[ginfo.labels(v)].emplace_back(area * width_factor, area * (1.0 - width_factor));
    }
  }

  num_dims = ndims;
}

void Hier_tree::construct_bounds(const unsigned int optimal_thresh) {
  /*
  for (auto& pat : hier_patterns) {
    for (size_t i = 0; i < num_dims; i++) {
      std::stringstream instr;
      instr << fmt::format("{}\n", pat.count());
      for (auto gv : pat.verts) {
        Layout gv_dim = leaf_dims[gv.first][i];
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

      auto blobb_p = curr_p / "bazel-bin" / "third_party" / "misc" / "blobb_compass" / "blobb";
      if (!std::filesystem::exists(blobb_p)) {
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

        auto cfstream = popen(command.c_str(), "r");
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

      blobb_outf >> pat.pat_layout.width;
      blobb_outf >> pat.pat_layout.height;

      size_t total_size;
      blobb_outf >> total_size;

      I(total_size == pat.count());

      for (auto inp_dims : pat.verts) {
        Layout l;
        blobb_outf >> l.width;
        blobb_outf >> l.height;

        pat.layouts[inp_dims.first].push_back(l);
      }

      for (auto& lpair : pat.layouts) {
        blobb_outf >> lpair.second[i].xpos;
        blobb_outf >> lpair.second[i].ypos;
      }

      blobb_outf.close();
    }
  }
  */
}