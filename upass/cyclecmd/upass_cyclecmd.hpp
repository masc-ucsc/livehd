//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "upass_core.hpp"

struct uPass_cyclecmd_a : public upass::uPass {
public:
  using uPass::uPass;
};

struct uPass_cyclecmd_b : public upass::uPass {
public:
  using uPass::uPass;
};

static upass::uPass_plugin cyclecmd_a(
    "__upass_cycle_cmd_a",
    upass::uPass_wrapper<uPass_cyclecmd_a>::get_upass,
    {"__upass_cycle_cmd_b"});

static upass::uPass_plugin cyclecmd_b(
    "__upass_cycle_cmd_b",
    upass::uPass_wrapper<uPass_cyclecmd_b>::get_upass,
    {"__upass_cycle_cmd_a"});
