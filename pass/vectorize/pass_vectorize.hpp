
#ifndef PASS_vectorize_H
#define PASS_vectorize_H

#include "pass.hpp"
#include "options.hpp"

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

  void transform(LGraph *orig) override final;
};

#endif

