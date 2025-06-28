// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "main_api.hpp"

#include "cloud_api.hpp"
#include "file_utils.hpp"
#include "inou_lef_api.hpp"
#include "meta_api.hpp"
#include "thread_pool.hpp"
#include "top_api.hpp"

void setup_inou_pyrope();
void setup_inou_yosys();
void setup_inou_liveparse();

// add new setup function prototypes here

void Main_api::init() {
  for (const auto &it : Pass_plugin::get_registry()) {
    // std::cout << std::format("function:{}\n", it.first);
    it.second();
  }

  setup_inou_pyrope();
  setup_inou_yosys();
  setup_inou_liveparse();

  // FIXME beyond this point (to delete some of them)

  Top_api::setup(Pass::eprp);  // *

  Meta_api::setup(Pass::eprp);   // lgraph.*
  Cloud_api::setup(Pass::eprp);  // cloud.*
}

void Main_api::parse_inline(std::string_view line) {
  Pass::eprp.parse_inline(line);

  thread_pool.wait_all();
}
