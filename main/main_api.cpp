
#include "main_api.hpp"

#include "top_api.hpp"

#include "meta_api.hpp"

#include "cloud_api.hpp"

#include "cops_live_api.hpp"

#include "inou_cfg_api.hpp"
#include "inou_lef_api.hpp"
#include "inou_json_api.hpp"
#include "inou_pyrope_api.hpp"
#include "inou_ramgen_api.hpp"
#include "inou_rand_api.hpp"
#include "inou_yosys_api.hpp"

#include "live_parse_api.hpp"

#include "pass_abc_api.hpp"
#include "pass_dce_api.hpp"
#include "pass_dfg_api.hpp"
#include "pass_opentimer_api.hpp"
#include "pass_sample_api.hpp"
#include "pass_bitwidth_api.hpp"

#include "eprp_utils.hpp"

Eprp Main_api::eprp;
std::string Main_api::main_path;

void Main_api::init() {
  Top_api::setup(eprp);              // *

  Meta_api::setup(eprp);             // lgraph.*
  Cloud_api::setup(eprp);            // cloud.*

  Inou_cfg_api::setup(eprp);         // inou.cfg.*
  Inou_lef_api::setup(eprp);         // inou.lef.*
  Inou_json_api::setup(eprp);        // inou.json.*
  Inou_pyrope_api::setup(eprp);      // inou.pyrope.*
  Inou_ramgen_api::setup(eprp);      // inou.ramgen.*
  Inou_rand_api::setup(eprp);        // inou.rand.*
  Inou_yosys_api::setup(eprp);       // inou.yosys.*

  Live_parse_api::setup(eprp);       // live.parse.*

  Pass_abc_api::setup(eprp);         // pass.abc.*
  Pass_dce_api::setup(eprp);         // pass.dce.*
  Pass_dfg_api::setup(eprp);         // pass.dfg.*
  Pass_opentimer_api::setup(eprp);   // pass.opentimer.*
  Pass_sample_api::setup(eprp);      // pass.sample.*
  Pass_bitwidth_api::setup(eprp);    // pass.bitwidth.*

  Cops_live_api::setup(eprp);        // pass.dfg.*

  main_path = Eprp_utils::get_exe_path();
}

