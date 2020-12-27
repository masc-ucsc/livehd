#pragma once

#include <functional>

#include "absl/container/flat_hash_map.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"

#include "archfp_driver.hpp"

class Pass_fplan : public Pass {
public:
  Pass_fplan(const Eprp_var& var);

  static void setup();

  static void pass(Eprp_var& v);

private:

  LGraph* root_lg;  // public for debugging

};
