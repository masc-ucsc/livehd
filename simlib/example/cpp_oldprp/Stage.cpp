
#include "Stage.h"

#include <stdlib.h>

Stage::Stage() {}

bool Stage::once_every(TimeDelta_t t) const {
  I(t);

  TimeDelta_t orig = random();
  TimeDelta_t rnd  = orig / t;

  if (rnd == (orig * t)) {
    return true;
  }

  return false;
}
