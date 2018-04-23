
#ifndef PASS_DCE_H
#define PASS_DCE_H

#include "pass.hpp"
#include "options.hpp"

#include <string>

class Pass_dce_options_pack : public Options_pack {
public:
};

class Pass_dce : public Pass {
private:
protected:
  std::string dce_type;

  Pass_dce_options_pack opack;
public:
  Pass_dce();

  void transform(LGraph *orig) override final;
};

#endif

