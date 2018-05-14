#ifndef LIVE_STITCHER_H_
#define LIVE_STITCHER_H_

#include "invariant.hpp"

class Live_stitcher {

private:
  LGraph *original;

  Invariant_boundaries *boundaries;

public:
  Live_stitcher(LGraph *original, Invariant_boundaries *boundaries) : original(original), boundaries(boundaries) {
  }

  void stitch(LGraph *nsynth, std::set<Net_ID> diffs);
};

#endif
