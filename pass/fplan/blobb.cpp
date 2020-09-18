#include <cstdio>      // for popen, pclose
#include <cstring>     // for strcpy
#include <filesystem>  // for *nix file hierarchy traversal
#include <fstream>
#include <stdexcept>
#include <string>

#include "hier_tree.hpp"
#include "profile_time.hpp"

void Hier_tree::invoke_blobb(const std::stringstream& instr, std::stringstream& outstr, const bool hier) {
  // shelling out here because blobb uses static variables that can't easily be reset across calls.
  // TODO: information is passed to blobb by reading and writing files for now, which is slow.
  // can we map files to memory?  The files are pretty small...
  std::ofstream blobb_inf;
  std::ifstream blobb_outf;

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

  // configure BloBB as follows:
  // 1. only consider slicing packings (default)
  // 2. allow for blocks to be rotated (default)
  // 3. enable quiet operation (-t)
  // 4. switch between an optimal floorplanning algorithm and a hierarchical one capable of handling bigger inputs with reduced
  // quality
  // 5. a maximum deadspace percent that starts small and grows bigger over time

  // note that another option here is to use the --backtrack option to find _some_ packing with a slowly increasing deadspace
  // percent it isn't optimal, but it's a lot faster

  const std::string files      = "/tmp/infile.txt /tmp/outfile.bbb";
  const std::string fixed_args = "-t";

  std::string alg;
  if (hier) {
    alg = "--hierarchical ";
  } else {
    alg = "--optimal ";
  }

  profile_time::timer t;
  t.start();

  fmt::print("    running blobb...");

  const std::string command = fmt::format("{} {} {} {}", blobb_p.string(), files, fixed_args, alg);

  auto cfstream = popen(command.c_str(), "r");
  I(cfstream);

  // even if we don't print the output of BloBB, we still have to read it or BloBB will hang?
  char c;
  while ((c = fgetc(cfstream)) != EOF) {
    // putchar(ch);
  }

  int retcode = pclose(cfstream);
  if (retcode != 0) {
    throw std::runtime_error("blobb exited with non-zero value! (empty hierarchy?)");
  }

  blobb_outf.open("/tmp/outfile.bbb");
  if (!blobb_outf) {
    throw std::runtime_error("cannot open /tmp/outfile.bbb!");
  }

  if (blobb_outf.peek() == std::ifstream::traits_type::eof()) {
    throw std::runtime_error("no output from blobb!");
  }

  fmt::print("done ({} ms).\n", t.time());

  outstr << blobb_outf.rdbuf();

  blobb_outf.close();
}