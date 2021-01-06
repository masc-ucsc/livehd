//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_slang.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

static Pass_plugin sample("inou.verilog", Inou_slang::setup);

void Inou_slang::setup() {
  Eprp_method m1("inou.verilog", "System verilog to LNAST using slang", &Inou_slang::work);

  register_pass(m1);
}

Inou_slang::Inou_slang(const Eprp_var &var) : Pass("pass.lec", var) {}

void Inou_slang::work(Eprp_var &var) {
  Lbench      b("inou.SLANG_verilog");
  Inou_slang p(var);

#if 0
    // FIXME: Call a slang block
    Slang converter;
#endif
  for (auto f : absl::StrSplit(p.files, ',')) {

    fmt::print("call slang with {} and return a lnast for each module\n", f);
#if 0
    auto lnast = slang.add_verilog(name);
#endif

  }
#if 0
  // FIXME: read all the lnast created (may be more than 1)
  std::vector<std::unique_ptr<Lnast>> lnast_vector;
  slang.move_lnast(lnast_vector);
  for(auto &lnast:lnast_vector) {
    var.add(std::move(lnast));
  }
#endif
}

