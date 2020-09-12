#include <cstdio>      // for popen, pclose
#include <cstring>     // for strcpy
#include <filesystem>  // for *nix file hierarchy traversal
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>

#include "fmt/format.h"
#include "hier_tree.hpp"
#include "profile_time.hpp"

constexpr double max_aspect_ratio = 1.0 / 5.0;

void Hier_tree::construct_bounds(const size_t pat_index, const unsigned int optimal_thresh) {
  std::default_random_engine     gen;
  std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

  std::unordered_map<Lg_type_id::type, double> label_area_map;
  label_area_map.reserve(ginfo.al.order());

  for (auto v : ginfo.al.verts()) {
    auto label = ginfo.labels(v);
    I(label_area_map[label] == 0.0 || label_area_map[label] == ginfo.areas(v));
    label_area_map[label] = ginfo.areas(v);
  }

  // pat_list is a sequence of patterns that must be traversed linearly
  pattern_vec_t& pat_list = pattern_lists[pat_index];

  for (pattern_t pat : pat_list) {
    // copying strings to silence warnings
    std::stringstream instr;
    instr << fmt::format("{}\n", pat.size());
    for (auto v : pat) {
      double width_factor = dist(gen);
      double width        = label_area_map[v.first] * width_factor;
      double height       = label_area_map[v.first] * (1.0 - width_factor);
      instr << fmt::format("{:.4f} {:.4f}\n", width, height);
    }

    // shelling out here because blobb uses static variables that can't easily be reset across calls.
    // additionally, forking is actually pretty fast compared to the other things I have to do!

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
    const std::string alg        = (pat.size() < optimal_thresh) ? "--backtrack" : "--hierarchical";

    int dead_p = 15;  // when set to the default (5%), small floorplans fail frequently

    std::ifstream blobb_outf;

    // run BloBB with increasing deadspace percentages until we get a floorplan
    do {
      std::string deadspace = fmt::format("-d {}", dead_p);
      dead_p += 10;

      const std::string command = fmt::format("{} {} {} {} {}", blobb_p.string(), files, fixed_args, alg, deadspace);

      profile_time::timer t;
      t.start();

      fmt::print("    running blobb...");
      // forking requires C file handles :/
      FILE* cfstream = popen(command.c_str(), "r");
      I(cfstream);

      // even if we don't print the output of BloBB, we still have to read it or BloBB will hang?
      char c;
      while ((c = fgetc(cfstream)) != EOF) {
        // putchar(ch);
      }

      pclose(cfstream);
      fmt::print("done ({} ms).\n", t.time());

      blobb_outf.open("/tmp/outfile.bbb");
      I(blobb_outf);

      if (blobb_outf.peek() == std::ifstream::traits_type::eof()) {
        if (bound_verbose) {
          fmt::print("no output was sent to file, trying with {}% deadspace...\n", dead_p);
        }
        blobb_outf.close();
      }

    } while (!blobb_outf.is_open() && dead_p < 100);

    I(dead_p < 100);

    // read file
    double width, height;
    blobb_outf >> width;
    blobb_outf >> height;

    blobb_outf.close();

    if (bound_verbose) {
      fmt::print("adding ({:.4f}, {:.4f}) to bounding curve\n", width, height);
    }
    bounding_curve.emplace_back(width, height);
  }
}