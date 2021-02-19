//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "analyzefp.hpp"
#include "checkfp.hpp"
#include "makefp.hpp"
#include "write_range.hpp"

// single call to set all fplan-related passes up
void setup_pass_fplan() {
  Pass_fplan_write_range::setup();
  Pass_fplan_makefp::setup();
  Pass_fplan_checkfp::setup();
  Pass_fplan_analyzefp::setup();
}