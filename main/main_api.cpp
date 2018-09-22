
#include "main_api.hpp"

#include "top_api.hpp"
#include "meta_api.hpp"
#include "inou_cfg_api.hpp"
#include "inou_lef_api.hpp"
#include "inou_json_api.hpp"
#include "inou_pyrope_api.hpp"
#include "inou_rand_api.hpp"
#include "inou_yosys_api.hpp"
#include "pass_abc_api.hpp"
#include "pass_dfg_api.hpp"

Eprp Main_api::eprp;
std::string Main_api::main_path;

void Main_api::init() {
  Top_api::setup(eprp);        // *

  Meta_api::setup(eprp);       // lgraph.*

  Inou_cfg_api::setup(eprp);   // inou.cfg.*
  Inou_lef_api::setup(eprp);   // inou.lef.*
  Inou_json_api::setup(eprp);  // inou.json.*
  Inou_pyrope_api::setup(eprp);// inou.pyrope.*
  Inou_rand_api::setup(eprp);  // inou.rand.*
  Inou_yosys_api::setup(eprp); // inou.yosys.*

  Pass_abc_api::setup(eprp);   // pass.abc.*
  Pass_dfg_api::setup(eprp);   // pass.dfg.*

  char exePath[PATH_MAX];
  int len = readlink("/proc/self/exe", exePath, PATH_MAX);
  assert(len > 0 && len < PATH_MAX);
  for(int p=len-1;p>=0;p--) {
    if (exePath[p] == '/') {
      len = p;
      break;
    }
  }

  main_path = std::string(exePath,0,len);
}

