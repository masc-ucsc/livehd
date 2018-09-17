//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef INOU_RAND_H
#define INOU_RAND_H

#include <string>

#include "inou.hpp"
#include "options.hpp"

class Inou_rand_options : public Options_base {
public:
  int         rand_seed;
  int         rand_size;
  int         rand_crate;
  double      rand_eratio;

  Inou_rand_options() {
     rand_seed   = std::rand();
     rand_size   = 8192;
     rand_crate  = 10;
     rand_eratio = 4;
  }
  void set(const std::string &key, const std::string &value) final;
};

class Inou_rand : public Inou {
private:
protected:
  Inou_rand_options opack;

public:
  Inou_rand();

  std::vector<LGraph *> tolg() final;
  void fromlg(std::vector<const LGraph *> &out) final;

  void set(const std::string &key, const std::string &value) final {
    opack.set(key,value);
  }
};

#endif
