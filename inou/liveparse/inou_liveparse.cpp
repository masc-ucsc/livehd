//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inou_liveparse.hpp"

#include "chunkify_verilog.hpp"

void setup_inou_liveparse() { Inou_liveparse::setup(); }

void Inou_liveparse::setup() {
  Eprp_method m1("inou.liveparse", mmap_lib::str("liveparse and chunkify verilog/pyrope files"), &Inou_liveparse::tolg);
  register_inou("liveparse", m1);
}

Inou_liveparse::Inou_liveparse(const Eprp_var &var) : Pass("inou.liveparse", var) {}

void Inou_liveparse::do_tolg() {
  Chunkify_verilog chunker_v(path);

  for (auto f : files.split(',')) {
    if (f.ends_with(".v") || f.ends_with(".sv")) {
      chunker_v.parse_file(f);
    } else if (f.ends_with(".prp")) {
      error("inou.liveparse chunkify Pyrope not implemented yet {}", f);
      return;
    } else {
      error("inou.liveparse chunkify unrecognized file format {}", f);
      return;
    }
  }
}

void Inou_liveparse::tolg(Eprp_var &var) {
  Inou_liveparse lp(var);

  lp.do_tolg();
}
