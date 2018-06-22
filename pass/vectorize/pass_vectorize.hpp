//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef PASS_vectorize_H
#define PASS_vectorize_H

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_vectorize_options_pack : public Options_pack {
public:
};

class Pass_vectorize : public Pass {
private:
  void collapse_reset(LGraph *g);
  void collapse_join(LGraph *g);

protected:
  std::string vectorize_type;

  Pass_vectorize_options_pack opack;

public:
  Pass_vectorize();

  void transform(LGraph *orig) final;
};

#endif
