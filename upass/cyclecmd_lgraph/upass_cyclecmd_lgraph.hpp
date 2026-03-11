//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "upass_lgraph_core.hpp"

struct uPass_lgraph_cyclecmd_a : public upass::uPass_lgraph {
public:
  using uPass_lgraph::uPass_lgraph;
};

struct uPass_lgraph_cyclecmd_b : public upass::uPass_lgraph {
public:
  using uPass_lgraph::uPass_lgraph;
};

static upass::uPass_lgraph_plugin lgraph_cyclecmd_a("__upass_lgraph_cycle_cmd_a",
                                                    upass::uPass_lgraph_wrapper<uPass_lgraph_cyclecmd_a>::get_upass,
                                                    {"__upass_lgraph_cycle_cmd_b"});

static upass::uPass_lgraph_plugin lgraph_cyclecmd_b("__upass_lgraph_cycle_cmd_b",
                                                    upass::uPass_lgraph_wrapper<uPass_lgraph_cyclecmd_b>::get_upass,
                                                    {"__upass_lgraph_cycle_cmd_a"});
