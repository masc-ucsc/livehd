//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef INVARIANT_OPTIONS_H_
#define INVARIANT_OPTIONS_H_

#include "options.hpp"

class Invariant_find_options : public Options_base {

public:
  std::string top;
  std::string elab_lgdb;
  std::string synth_lgdb;
  std::string invariant_file;
  std::string hierarchical_separator;

  //cluster related options
  std::string cluster_dir;
  int         clusters;
  bool        do_cluster;

  Invariant_find_options() : top(""), elab_lgdb("lgdb"), synth_lgdb("synth-lgdb"),
              invariant_file("boundaries"), hierarchical_separator("."), clusters(0),
              do_cluster(false), cluster_dir("") { }

  void set(const std::string &key, const std::string &value) final;
};

#endif
