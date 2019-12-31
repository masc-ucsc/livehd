
#include "absl/strings/substitute.h"
#include "chunkify_verilog.hpp"

#include "inou_liveparse.hpp"

void setup_inou_liveparse() {
  Inou_liveparse::setup();
}

void Inou_liveparse::setup() {
  Eprp_method m1("inou.liveparse", "liveparse and chunkify verilog/pyrope files", &Inou_liveparse::tolg);

  register_inou("liveparse", m1);
}

Inou_liveparse::Inou_liveparse(const Eprp_var &var)
  : Pass("inou.liveparse", var) {
}

void Inou_liveparse::do_tolg() {
  Chunkify_verilog chunker(path);

  for (const auto &f : absl::StrSplit(files, ',')) {
    if(absl::EndsWith(f, ".v") || absl::EndsWith(f, ".sv")) {
      chunker.parse_file(f);
    } else if(absl::EndsWith(f, ".prp")) {
      error("inou.liveparse pyrope chunkify NOT implemented for {}", f);
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
