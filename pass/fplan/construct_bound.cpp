#include <cstdio>      // for popen, pclose
#include <cstring>     // for strcpy
#include <filesystem>  // for *nix file hierarchy traversal
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "dag.hpp"
#include "fmt/format.h"
#include "hier_tree.hpp"
#include "profile_time.hpp"

void Hier_tree::construct_bounds(const Dag::pdag pd, const unsigned int optimal_thresh) {
  // set dimensions of leaf nodes in the dag
  std::function<void(const Dag::pdag)> assign_leaf_dims = [&](const Dag::pdag pd) {
    if (pd->is_leaf()) {
      static constexpr double               max_aspect_ratio = 1.0 / 5.0;
      static std::default_random_engine     gen;
      static std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

      if (leaf_dims.count(pd->dag_label) == 0) {
        double width_factor = dist(gen);
        pd->width           = pd->area * width_factor;
        pd->height          = pd->area * (1.0 - width_factor);

        leaf_dims[pd->dag_label] = {pd->width, pd->height};
      } else {
        auto dims  = leaf_dims[pd->dag_label];
        pd->width  = dims.width;
        pd->height = dims.height;
      }
    }

    for (auto child : pd->children) {
      assign_leaf_dims(child);
    }
  };

  assign_leaf_dims(pd);

  std::function<void(const Dag::pdag)> floorplan_patterns = [&](const Dag::pdag pd) {
    // avoid floorplanning leaves (no point, since they only contain a single type of node)
    if (pd->is_leaf()) {
      return;
    }

    // make sure all children are floorplanned before we start floorplanning ourselves
    for (auto child : pd->children) {
      floorplan_patterns(child);
    }

    // avoid floorplanning the entire design for now
    if (pd->is_root()) {
      return;
    }

    std::stringstream instr, outstr;

    instr << fmt::format("{}\n", pd->children.size());
    for (auto child : pd->children) {
      I(child->width != 0);
      I(child->height != 0);
      instr << fmt::format("{:.4f} {:.4f}\n", child->width, child->height);
    }

    if (bound_verbose) {
      fmt::print("\ninput string stream:\n{}", instr.str());
    }

    

    // if the pattern is small, we can afford to use an exhaustive approach to finding floorplans.
    bool small_pat = pd->children.size() <= optimal_thresh;
    invoke_blobb(instr, outstr, small_pat);

    // TODO: memoize pattern generation with a map of Pattern -> Outline
    // TODO: at end: assign area!!!
  };

  floorplan_patterns(pd);
}

/*

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

void Hier_tree::construct_bounds(const unsigned int optimal_thresh) {
  for (size_t i = 0; i < dags.size(); i++) {
    construct_bounds(dags[i].root, optimal_thresh);
  }
}

void Hier_tree::invoke_blobb(const std::stringstream& instr, std::stringstream& outstr, const bool small) {
  // shelling out here because blobb uses static variables that can't easily be reset across calls.
  // TODO: information is passed to blobb by reading and writing files for now, which is slow.
  // can we map files to memory?  the files are pretty small...

  std::ofstream blobb_inf;

  blobb_inf.open("/tmp/infile.txt", std::ios::out | std::ios::trunc);
  if (!blobb_inf) {
    throw std::runtime_error("cannot open file /tmp/infile.txt!");
  }
  blobb_inf << instr.str();
  blobb_inf.close();

  // should be <...>/livehd (symlink)
  auto curr_p = std::filesystem::current_path();

  const auto blobb_p = curr_p / "bazel-bin" / "third_party" / "misc" / "blobb_compass" / "blobb";
  if (!std::filesystem::exists(blobb_p)) {
    throw std::runtime_error(fmt::format("No binary found in {}!", blobb_p.string()));
  }

  const std::string files      = "/tmp/infile.txt /tmp/outfile.bbb";
  const std::string fixed_args = "--slicing --free-orient -t";
  const std::string alg        = (small) ? "--backtrack" : "--hierarchical";

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

    int retcode = pclose(cfstream);
    if (retcode != 0) {
      throw std::runtime_error("blobb exited with non-zero value!");
    }

    blobb_outf.open("/tmp/outfile.bbb");
    if (!blobb_outf) {
      throw std::runtime_error("cannot open /tmp/outfile.bbb!");
    }

    if (blobb_outf.peek() == std::ifstream::traits_type::eof()) {
      if (bound_verbose) {
        fmt::print("\nno output was sent to file, trying with {}% deadspace...", dead_p);
      }
      blobb_outf.close();
    }

  } while (!blobb_outf.is_open() && dead_p < 100);

  fmt::print("\ndone ({} ms).\n", t.time());

  I(dead_p < 100);

  outstr << blobb_outf.rdbuf();

  blobb_outf.close();
}