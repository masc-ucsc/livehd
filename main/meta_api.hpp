//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "main_api.hpp"

class Meta_api {
protected:
  static void save(Eprp_var& var);
  static void match(Eprp_var& var);

  static void lnastdump(Eprp_var& var);
  static void lnastprint(Eprp_var& var);
  static void lnastread(Eprp_var& var);

  Meta_api() {}

public:
  static void setup(Eprp& eprp);
};
