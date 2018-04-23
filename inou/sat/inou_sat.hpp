#ifndef INOU_SAT_H_
#define INOU_SAT_H_


#include <boost/program_options.hpp>
#include "core/options.hpp"

class Sat_pass_pack {

public:

  //input related options
  std::string sat_lgdb;
  Sat_pass_pack(int argc, const char ** argv);
};

#endif
