//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass.hpp"

class Pass_lnast_save : public Pass {
public:
  Pass_lnast_save(const Eprp_var&);
  static void setup();
  static void do_work(const Eprp_var& var);

protected:
};
