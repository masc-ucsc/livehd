//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "main_api.hpp"

class Meta_api {
protected:
  static void open(Eprp_var &var);
  static void create(Eprp_var &var);
  static void rename(Eprp_var &var);
  static void copy(Eprp_var &var);

  static void match(Eprp_var &var);

  static void stats(Eprp_var &var);
  static void dump(Eprp_var &var);

  static void liberty(Eprp_var &var);
  static void sdc(Eprp_var &var);

  static void spef(Eprp_var &var);

  static void lgdump(Eprp_var &var);
  static void lnastdump(Eprp_var &var);

  Meta_api() {}

public:
  static void setup(Eprp &eprp);
};

