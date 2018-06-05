
#ifndef INOU_RAND_H
#define INOU_RAND_H

#include <string>

#include "core/inou.hpp"

class Inou_rand_options_pack : public Options_pack {
public:
  Inou_rand_options_pack();

  std::string rand_type;
  int         rand_seed;
  int         rand_size;
  int         rand_crate;
  double      rand_eratio;
};

class Inou_rand : public Inou {
private:
protected:
  Inou_rand_options_pack opack;

public:
  Inou_rand();

  std::vector<LGraph *> generate() final;
  using Inou::generate;
  void generate(std::vector<const LGraph *> &out) final;
};

#endif
