//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_firrtl.hpp"

/* For help understanding FIRRTL/Protobuf:
 * 1) Semantics regarding FIRRTL language:
 * www2.eecs.berkeley.edu/Pubs/TechRpts/2019/EECS-2019-168.pdf
 * 2) Structure of FIRRTL Protobuf file:
 * github.com/freechipsproject/firrtl/blob/master/src/main/proto/firrtl.proto */

void setup_inou_firrtl() { Inou_firrtl::setup(); }

void Inou_firrtl::setup() {
  Eprp_method m1("inou.firrtl.tolnast", "Translate FIRRTL to LNAST (in progress)", &Inou_firrtl::toLNAST);
  m1.add_label_required("files", "protobuf data file[s] gotten from Chisel's toProto functionality");
  register_inou("firrtl", m1);

  Eprp_method m2("inou.firrtl.tofirrtl", "LNAST to FIRRTL", &Inou_firrtl::toFIRRTL);
  m2.add_label_optional("odir", "output directory for generated firrtl files", ".");
  register_inou("firrtl", m2);
}

Inou_firrtl::Inou_firrtl(const Eprp_var &var) : Pass("firrtl", var) {}
