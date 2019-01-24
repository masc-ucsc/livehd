//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LIVE_STITCHER_H_
#define LIVE_STITCHER_H_

#include "invariant.hpp"
#include "stitch_options.hpp"

class Live_stitcher {
private:
  LGraph *original;

  Invariant_boundaries *boundaries;

public:
  Live_stitcher(LGraph *original, Invariant_boundaries *boundaries) : original(original), boundaries(boundaries) {}

  Live_stitcher(Stitch_pass_options &pack);

  void stitch(const std::string &nsynth, const std::set<Net_ID> &diffs) {
    LGraph *synth = LGraph::open(nsynth, boundaries->top);
    stitch(synth, diffs);
  }

  void stitch(LGraph *nsynth, const std::set<Net_ID> &diffs);
};

#endif
