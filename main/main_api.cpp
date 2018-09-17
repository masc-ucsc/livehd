
#include "main_api.hpp"

#include "meta_api.hpp"
#include "inou_abc_api.hpp"
#include "inou_cfg_api.hpp"
#include "inou_rand_api.hpp"

Eprp Main_api::eprp;

void Main_api::init() {
  Meta_api::setup(eprp);
  Inou_abc_api::setup(eprp);
  Inou_cfg_api::setup(eprp);
  Inou_rand_api::setup(eprp);
}

