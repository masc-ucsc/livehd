//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bundle_pre.hpp"

// Registered last (not in header) to avoid duplicate-registration when
// multiple TUs include upass_bundle_pre.hpp.
static upass::uPass_plugin plugin_bundle_pre("bundle_pre", upass::uPass_wrapper<uPass_bundle_pre>::get_upass);
