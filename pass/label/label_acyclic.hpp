// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"

class Label_acyclic {
private:
  const bool        verbose;
  const bool        hier;

public:
  void label(LGraph *g);

  Label_acyclic(bool _verbose, bool _hier);
};
