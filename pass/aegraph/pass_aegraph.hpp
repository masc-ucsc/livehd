//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "aegraph.hpp"
#include "cprop.hpp"
#include "hhds/graph.hpp"
#include "pass.hpp"
#include "runtime/match.hpp"

// pass.aegraph: the e-graph alternative to pass.cprop. The kernel selects it
// with `--set compile.cprop.engine=aegraph|aegraph+classic` and swaps the
// method in the compile schedule; there is no user-facing way to run it alone.
class Pass_aegraph : public Pass {
protected:
  static void optimize(Eprp_var& var);

public:
  Pass_aegraph(const Eprp_var& var);
  static void setup();
};

namespace livehd::aegraph {
// What pass.aegraph does to ONE graph body: the engine (with the classic scalar
// rewrites as its false-cycle cutter), then the classic region passes
// (Cprop::do_post) over what it elaborated. The loop-invariant hoist is the
// library-wide step the caller runs afterwards. Tests drive the engine through
// this so they exercise exactly the production per-body path.
Stats optimize_body(Cprop& cp, const std::shared_ptr<hhds::Graph>& g, const Options& options = {});
}  // namespace livehd::aegraph
