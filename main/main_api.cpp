
#include "main_api.hpp"

#include "top_api.hpp"

#include "meta_api.hpp"

#include "cloud_api.hpp"

#include "cops_live_api.hpp"

#include "inou_cfg_api.hpp"
#include "inou_lef_api.hpp"
#include "inou_json_api.hpp"
#include "inou_pyrope_api.hpp"
#include "inou_graphviz_api.hpp"
#include "inou_yosys_api.hpp"

#include "eprp_utils.hpp"

std::string Main_api::main_path;

void setup_pass_abc();
void setup_pass_bitwidth();
void setup_pass_dce();
void setup_pass_dfg();
void setup_pass_sample();

void setup_inou_liveparse();
void setup_inou_rand();

void Main_api::init() {
  setup_pass_abc();
  setup_pass_bitwidth();
  setup_pass_dce();
  setup_pass_dfg();
  setup_pass_sample();

  setup_inou_liveparse();

  Top_api::setup(Pass::eprp);              // *

  Meta_api::setup(Pass::eprp);             // lgraph.*
  Cloud_api::setup(Pass::eprp);            // cloud.*

  Inou_cfg_api::setup(Pass::eprp);         // inou.cfg.*
  Inou_lef_api::setup(Pass::eprp);         // inou.lef.*
  Inou_json_api::setup(Pass::eprp);        // inou.json.*
  Inou_pyrope_api::setup(Pass::eprp);      // inou.pyrope.*
  Inou_graphviz_api::setup(Pass::eprp);    // inou.graphviz.*
  Inou_yosys_api::setup(Pass::eprp);       // inou.yosys.*


  Cops_live_api::setup(Pass::eprp);        // pass.dfg.*

  main_path = Eprp_utils::get_exe_path();
}

