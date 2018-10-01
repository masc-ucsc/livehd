//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LIVE_OPTIONS_H_
#define LIVE_OPTIONS_H_

#include "options.hpp"

class Live_pass_options : public Options_base {

public:
  //input related options
  std::string original_lgdb;
  std::string modified_lgdb;
  std::string synth_lgdb;

  std::string boundaries_name;

  //output related options
  std::string delta_lgdb;
  std::string diff_file;

  Live_pass_options() : original_lgdb("lgdb"), modified_lgdb("mod-lgdb"),
                 synth_lgdb("synth-lgdb"), boundaries_name("boundaries"),
                 delta_lgdb("delta-lgdb"), diff_file("diff")
  {}

  void set(const std::string &key, const std::string &value) final;
};

#endif
