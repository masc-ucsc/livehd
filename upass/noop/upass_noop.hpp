//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "upass_core.hpp"

struct uPass_noop : public upass::uPass {
public:
  using uPass::uPass;
};

static upass::uPass_plugin noop("noop", upass::uPass_wrapper<uPass_noop>::get_upass);
