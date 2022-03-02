/******************************************************************************
 * algorithms.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <string>

#ifdef PARALLEL
#include "parallel/algorithm/exact_parallel_minimum_cut.h"
#include "parallel/algorithm/parallel_cactus.h"
#endif
#include "algorithms/global_mincut/cactus/cactus_mincut.h"
#include "algorithms/global_mincut/ks_minimum_cut.h"
#include "algorithms/global_mincut/matula_approx.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "algorithms/global_mincut/noi_minimum_cut.h"
#include "algorithms/global_mincut/padberg_rinaldi.h"
#include "algorithms/global_mincut/stoer_wagner_minimum_cut.h"
#include "algorithms/global_mincut/viecut.h"

template <class GraphPtr>
[[maybe_unused]] static minimum_cut * selectMincutAlgorithm(
    const std::string& argv_str) {
#ifndef PARALLEL
    if (argv_str == "ks")
        return new ks_minimum_cut();
    if (argv_str == "noi")
        return new noi_minimum_cut<GraphPtr>();
    if (argv_str == "matula")
        return new matula_approx<GraphPtr>();
    if (argv_str == "vc")
        return new viecut<GraphPtr>();
    if (argv_str == "pr")
        return new padberg_rinaldi<GraphPtr>();
    if (argv_str == "cactus")
        return new cactus_mincut<GraphPtr>();
#endif
#ifdef PARALLEL
    if (argv_str == "inexact")
        return new viecut<GraphPtr>();
    if (argv_str == "exact")
        return new exact_parallel_minimum_cut<GraphPtr>();
    if (argv_str == "cactus")
        return new parallel_cactus<GraphPtr>();
#endif
    return new minimum_cut();
}
