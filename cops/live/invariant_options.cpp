//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include "pass.hpp"

#include "invariant_options.hpp"

void Invariant_find_options::set(const std::string &key, const std::string &value) {

  try {
    if (is_opt(key, "top")) {
      top = value;
    } else if (is_opt(key, "elab_lgdb")) {
      elab_lgdb = value;
    } else if (is_opt(key, "synth_lgdb")) {
      synth_lgdb = value;
    } else if (is_opt(key, "invariant_file")) {
      invariant_file = value;
    } else if (is_opt(key, "hier_sep")) {
      hierarchical_separator = value;
/*    } else if (is_opt(key, "clusters")) {
      clusters   = std::stoi(value);
      do_cluster = true;
    } else if (is_opt(key, "cluster_dir")) {
      cluster_dir = value;*/
    } else {
      set_val(key, value);
    }
  } catch (const std::invalid_argument &ia) {
    Pass::error(fmt::format("key {} has an invalid argument", key));
  }
}
