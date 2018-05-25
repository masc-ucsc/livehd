
#ifndef PASS_DCE_H
#define PASS_DCE_H

#include "options.hpp"
#include "pass.hpp"

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

  void transform(LGraph *orig) final;
};

#endif
