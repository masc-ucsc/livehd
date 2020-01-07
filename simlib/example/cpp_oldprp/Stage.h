#ifndef STAGE_H
#define STAGE_H

#include "Snippets.h"
#include "nanassert.h"


class Stage {
protected:
  uint32_t padding[16]; // To avoid false sharing between threads

  bool once_every(TimeDelta_t t) const;

public:
  Stage();
  ~Stage() {
  };

  // reset cycle (can be called many times)
  virtual void reset_cycle() = 0;

  // cycle, not called during reset
  virtual void cycle() = 0;

  // called after reset or cycle
  virtual void update() = 0;

};


#endif

