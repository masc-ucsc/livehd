//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass.hpp"

#include "live_options.hpp"

void Live_pass_options::set(const std::string &key, const std::string &value) {

  try {
    if (is_opt(key, "olgdb")) {
      original_lgdb = value;
    } else if (is_opt(key, "mlgdb")) {
      modified_lgdb = value;
    } else if (is_opt(key, "slgdb")) {
      synth_lgdb = value;
    } else if (is_opt(key, "boundaries_name")) {
      boundaries_name = value;
    } else if (is_opt(key, "delta_lgdb")) {
      delta_lgdb = value;
    } else if (is_opt(key, "diff_file")) {
      diff_file = value;
    } else {
      set_val(key, value);
    }
  } catch (const std::invalid_argument& ia) {
    Pass::error(fmt::format("Live_pass_options: key {} has an invalid argument", key));
  }
}
