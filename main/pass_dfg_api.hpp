
#include "eprp.hpp"

class Pass_dfg_api {
protected:
  static void generate(Eprp_var &var);
  static void optimize(Eprp_var &var);

public:
  static void setup(Eprp &eprp);

};

