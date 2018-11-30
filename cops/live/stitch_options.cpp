//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass.hpp"

#include "stitch_options.hpp"

void Stitch_pass_options::set(const std::string &key, const std::string &value) {

  try {
    if (is_opt(key, "osynth")) {
      osynth_lgdb = value;
    } else if (is_opt(key, "nsynth")) {
      nsynth_lgdb = value;
    } else if (is_opt(key, "boundaries")) {
      boundaries_name = value;
    } else if (is_opt(key, "methods")) {
      if(value == "LiveSynth")
        method = Live_method::LiveSynth;
      else if(value == "Structural")
        method = Live_method::Structural;
      else {
        Pass::error(fmt::format("Unrecognized option for stitch operation in incremental flow",value));
      }
    } else if (is_opt(key, "diff")) {
      diff_file = value;
    } else {
      set_val(key, value);
    }
  } catch (const std::invalid_argument& ia) {
    Pass::error(fmt::format("ERROR: key {} has an invalid argument {}", key));
  }
}
