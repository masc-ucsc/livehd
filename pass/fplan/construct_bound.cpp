#include <cstdio> // for popen, pclose

#include <cstring>  // for strcpy
#include <filesystem> // for *nix file hierarchy traversal
#include <fstream>
#include <random>
#include <unordered_map>

#include <string>

#include "fmt/format.h"
#include "hier_tree.hpp"

constexpr double max_aspect_ratio = 1.0 / 5.0;

std::default_random_engine     gen;
std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

std::unordered_map<Lg_type_id::type, double> label_area_map;

void Hier_tree::construct_bounds(const size_t pat_index, const size_t num_inst, const unsigned int optimal_thresh) {
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

    std::cout << "\ninput string stream:\n";
    std::cout << instr.str();

    std::ofstream inf;

    inf.open("/tmp/infile.txt", std::ios::out | std::ios::trunc);
    inf << instr.str();
    inf.close();

    // should be <...>/livehd
    auto curr_p = std::filesystem::current_path();

    auto blobb_p = curr_p / "third_party" / "misc" / "blobb_compass" / "bin" / "blobb";
    if (!std::filesystem::exists(blobb_p)) {
      throw std::runtime_error("BloBB isn't built!");
    }

    // use "w" to print to stdout
    const std::string args = std::string(" /tmp/infile.txt /tmp/outfile.bbb --backtrack --slicing --free-orient -t");
    auto cfstream = popen(blobb_p.string().append(args).c_str(), "r");
    I(cfstream);
    pclose(cfstream);
  }

  /*
    High level alg:
      create a bounding curve for a pattern
      add that bounding curve to a list of created bounding curves
      go to next pattern, loop
        if we recognize a pattern, use the already-created bounding curve instead
        (not sure how I would do this with blobb...)
  */
}