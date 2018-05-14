#ifndef STITCH_OPTIONS_H_
#define STITCH_OPTIONS_H_

#include "core/options.hpp"
#include <boost/program_options.hpp>

class Stitch_pass_pack {

public:
  typedef enum {
    LiveSynth,
    Structural
  } Live_method;

  std::string osynth_lgdb;
  std::string nsynth_lgdb;

  std::string boundaries_name;
  std::string diff_file;

  Live_method method;

  Stitch_pass_pack(int argc, const char **argv);
};

#endif
