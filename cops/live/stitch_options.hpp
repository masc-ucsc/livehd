//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef STITCH_OPTIONS_H_
#define STITCH_OPTIONS_H_

#include "options.hpp"

class Stitch_pass_options : public Options_base {

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

  Stitch_pass_options() : osynth_lgdb("lgdb"), nsynth_lgdb("lgdb-stitch"),
                          boundaries_name("boundaries"), diff_file("diff"),
                          method(Live_method::LiveSynth)  { }

  void set(const std::string &key, const std::string &value) final;
};

#endif
