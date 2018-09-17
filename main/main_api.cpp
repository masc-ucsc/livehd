
#include "main_api.hpp"

#include "meta_api.hpp"
#include "inou_cfg_api.hpp"
#include "inou_lef_api.hpp"
#include "inou_json_api.hpp"
#include "inou_pyrope_api.hpp"
#include "inou_rand_api.hpp"
#include "pass_abc_api.hpp"

Eprp Main_api::eprp;

void Main_api::init() {
  Meta_api::setup(eprp);

  Inou_cfg_api::setup(eprp);
  Inou_lef_api::setup(eprp);
  Inou_json_api::setup(eprp);
  Inou_pyrope_api::setup(eprp);
  Inou_rand_api::setup(eprp);

  Pass_abc_api::setup(eprp);
}

