// this header file exists becaues the graph lib and iassert both have macros called "I", but we
// only need the one in iassert to exist after both headers are included.

// if this trick is used more than once, annoying header problems start coming up because each header
// should only define "I" once.

// anything coming from the graph lib or the ranges library that graph includes should be from here.

#pragma once
#include "Adjacency_list.hpp" // for graph type
#include "Stable_adjacency_list.hpp" // for dag type
#include "range/v3/view.hpp"
#undef I

#include "iassert.hpp"

// redefine "I" in case iassert was already included
#define I(...) \
  do{ _Pragma("GCC diagnostic push"); _Pragma("GCC diagnostic ignored \"-Wsign-compare\""); \
    IX_X(,##__VA_ARGS__,\
        I_3(__VA_ARGS__),\
        I_2(__VA_ARGS__),\
        I_1(__VA_ARGS__),\
        I_0(__VA_ARGS__)\
        ); \
    _Pragma("GCC diagnostic pop"); }while(0)

