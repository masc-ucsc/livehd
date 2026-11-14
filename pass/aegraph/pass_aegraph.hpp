//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "aegraph.hpp"
#include "cprop.hpp"
#include "hhds/graph.hpp"
#include "pass.hpp"
#include "runtime/match.hpp"

// pass.aegraph: the constant-propagation pass of the compile schedule (both fold
// slots) and of every fold site (fold_body.hpp).
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
// this so they exercise exactly the production per-body path. With
// Options::preflight an engine that would create a cell it cannot stamp keeps
// only what needs no new cell (aegraph::optimize) and reports Stats::refused.
Stats optimize_body(Cprop& cp, const std::shared_ptr<hhds::Graph>& g, const Options& options = {});

// The whole per-body fold (what livehd::fold_body runs) with `cp`'s settings: a `cp` built with low_lanes
// means a bitwidth pass follows, so cells may stay unstamped; otherwise every created cell is stamped (the
// preflight contract). A guard scope on `cp` also turns on the engine's accumulation-guard rule.
Stats fold_with(Cprop& cp, const std::shared_ptr<hhds::Graph>& g);
inline Stats fold_with(const std::shared_ptr<hhds::Graph>& g, bool low_lanes = false) {
  Cprop cp{low_lanes};
  return fold_with(cp, g);
}
}  // namespace livehd::aegraph
