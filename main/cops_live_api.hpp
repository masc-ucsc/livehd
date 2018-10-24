
#include "eprp.hpp"

class Cops_live_api {
protected:
  static void invariant_finder(Eprp_var &var);
  static void diff_finder(Eprp_var &var);
  static void netlist_merge(Eprp_var &var);

public:
  static void setup(Eprp &eprp);

};
