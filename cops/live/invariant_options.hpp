#ifndef INVARIANT_OPTIONS_H_
#define INVARIANT_OPTIONS_H_


#include <boost/program_options.hpp>
#include "core/options.hpp"

class Invariant_find_pack {

public:
  std::string top;
  std::string elab_lgdb;
  std::string synth_lgdb;
  std::string invariant_file;

  Invariant_find_pack();
};


#endif
