#ifndef LIVE_OPTIONS_H_
#define LIVE_OPTIONS_H_

#include "core/options.hpp"
#include <boost/program_options.hpp>

class Live_pass_pack {

public:
  //input related options
  std::string original_lgdb;
  std::string modified_lgdb;
  std::string synth_lgdb;

  std::string boundaries_name;

  //output related options
  std::string delta_lgdb;
  std::string diff_file;

  Live_pass_pack(int argc, const char **argv);
};

#endif
