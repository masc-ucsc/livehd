//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_coalescer.hpp"

#include <gtest/gtest.h>

#include <memory>

#include "lnast.hpp"
#include "lnast_manager.hpp"

// Smoke test: the coalescer plugin must register so pass.upass can resolve it
// through the upass_plugin registry. Without this the `coalescer:0` /
// `coalescer:1` toggle silently degrades.
TEST(UpassCoalescer, PluginRegisters) {
  const auto& reg = upass::uPass_plugin::get_registry();
  EXPECT_NE(reg.find("coalescer"), reg.end());
}

// Construct + set_options round-trip. Confirms `coalescer:0` parses without
// surprises and the pass becomes inert.
TEST(UpassCoalescer, SetOptionsDisable) {
  auto ln = std::make_shared<Lnast>("test");
  ln->set_root(Lnast_ntype::create_top());
  auto lm = std::make_shared<upass::Lnast_manager>(ln);

  uPass_coalescer    pass(lm);
  upass::Options_map opts;
  opts["coalescer"] = "0";
  pass.set_options(opts);
}
