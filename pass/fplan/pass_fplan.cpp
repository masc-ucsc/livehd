#include "pass_fplan.hpp"

void setup_pass_fplan() {
  Pass_fplan_writearea::setup();

  Pass_fplan_makefp::setup();
  Pass_fplan_checkfp::setup();
  Pass_fplan_analyzefp::setup();
}