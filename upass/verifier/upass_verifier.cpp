//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_verifier.hpp"

// Registered once here (not in the header) to avoid the registration being
// dropped when no TU in the final binary includes upass_verifier.hpp.
static upass::uPass_plugin plugin_verifier("verifier", upass::uPass_wrapper<uPass_verifier>::get_upass);
