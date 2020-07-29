// this header file exists becaues the graph lib and iassert both have macros called "I", but we
// only need the one in iassert to exist after both headers are included.

// if this trick is used more than once, annoying header problems start coming up because each header
// should only define "I" once.

#pragma once
#include "Adjacency_list.hpp"
#include "range/v3/all.hpp"
#include "range/v3/algorithm.hpp"
#include "range/v3/view.hpp"
#undef I

#include "iassert.hpp"

