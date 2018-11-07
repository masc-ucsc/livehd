//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef INOU_RAND_H
#define INOU_RAND_H

#include <string>
#include "pass.hpp"

class Inou_rand_options {
public:
  int         rand_seed;
  int         rand_size;
  int         rand_crate;
  double      rand_eratio;
  std::string name;
  std::string path;

  Inou_rand_options() {
     rand_seed   = std::rand();
     rand_size   = 8192;
     rand_crate  = 10;
     rand_eratio = 4;
  }
};

class Inou_rand : public Pass {
private:
protected:
  Inou_rand_options opack;

  std::vector<LGraph *> do_tolg();
  static void tolg(Eprp_var &var);

public:
  Inou_rand();

  void setup() final;
};

#endif
