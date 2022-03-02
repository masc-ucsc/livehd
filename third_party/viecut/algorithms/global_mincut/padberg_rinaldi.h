/******************************************************************************
 * padberg_rinaldi.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "algorithms/global_mincut/minimum_cut.h"
#include "algorithms/global_mincut/minimum_cut_helpers.h"
#include "common/definitions.h"

#ifdef PARALLEL
#include "parallel/coarsening/contract_graph.h"
#include "parallel/coarsening/contraction_tests.h"
#else
#include "coarsening/contract_graph.h"
#include "coarsening/contraction_tests.h"
#endif

template <class GraphPtr>
class padberg_rinaldi : public minimum_cut {
 public:
    typedef GraphPtr GraphPtrType;
    padberg_rinaldi() { }
    virtual ~padberg_rinaldi() { }
    static constexpr bool debug = false;

    EdgeWeight perform_minimum_cut(GraphPtr G) {
        if (!G) {
            return -1;
        }
        EdgeWeight cut = G->getMinDegree();
        std::vector<GraphPtr> graphs;
        graphs.push_back(G);
        NodeID last_nodes = G->number_of_nodes() + 1;
        timer t;
        minimum_cut_helpers<GraphPtr>::setInitialCutValues(graphs);

        while (graphs.back()->number_of_nodes() > 2
               && graphs.back()->number_of_nodes() < last_nodes) {
            last_nodes = graphs.back()->number_of_nodes();
            union_find uf_34 = tests::prTests34(graphs.back(), cut);
            auto G_34 = contraction::fromUnionFind(graphs.back(), &uf_34);
            graphs.push_back(G_34);
            cut = minimum_cut_helpers<GraphPtr>::updateCut(graphs, cut);

            union_find uf_12 = tests::prTests12(graphs.back(), cut);
            auto G_12 = contraction::fromUnionFind(graphs.back(), &uf_12);
            graphs.push_back(G_12);
            cut = minimum_cut_helpers<GraphPtr>::updateCut(graphs, cut);
        }

        if (configuration::getConfig()->save_cut) {
            minimum_cut_helpers<GraphPtr>::retrieveMinimumCut(graphs);
        }


        return cut;
    }
};
