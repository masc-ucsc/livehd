/******************************************************************************
 * viecut.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2017-2018 Alexander Noe <alexander.noe@univie.ac.at>
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

#include "algorithms/flow/excess_scaling.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "algorithms/global_mincut/minimum_cut_helpers.h"
#include "algorithms/global_mincut/noi_minimum_cut.h"
#include "algorithms/misc/strongly_connected_components.h"
#include "common/definitions.h"
#include "data_structure/flow_graph.h"
#include "data_structure/graph_access.h"
#include "tools/graph_extractor.h"
#include "tools/timer.h"

#ifdef PARALLEL
#include "parallel/coarsening/contract_graph.h"
#include "parallel/coarsening/contraction_tests.h"
#include "parallel/coarsening/label_propagation.h"
#else
#include "coarsening/contract_graph.h"
#include "coarsening/contraction_tests.h"
#include "coarsening/label_propagation.h"
#endif

template <class GraphPtr>
class viecut : public minimum_cut {
 public:
    typedef GraphPtr GraphPtrType;
    static constexpr bool debug = false;
    bool timing = configuration::getConfig()->verbose;
    viecut() { }

    virtual ~viecut() { }

    EdgeWeight perform_minimum_cut(GraphPtr G) {
        return perform_minimum_cut(G, false);
    }

    EdgeWeight perform_minimum_cut(GraphPtr G,
                                   bool indirect) {
        if (!G) {
            return -1;
        }

        EdgeWeight cut = G->getMinDegree();
        std::vector<GraphPtr> graphs;
        graphs.push_back(G);

        minimum_cut_helpers<GraphPtr>::setInitialCutValues(graphs);

        while (graphs.back()->number_of_nodes() > 10000 &&
               (graphs.size() == 1 ||
                (graphs.back()->number_of_nodes() <
                 graphs[graphs.size() - 2]->number_of_nodes()))) {
            timer t;
            G = graphs.back();
            label_propagation<GraphPtr> lp;
            std::vector<NodeID> cluster_mapping = lp.propagate_labels(G);
            auto [mapping, reverse_mapping] =
                minimum_cut_helpers<GraphPtr>::remap_cluster(
                    G, cluster_mapping);
            contraction::findTrivialCuts(G, &mapping, &reverse_mapping, cut);

            auto H = contraction::contractGraph(G, mapping, reverse_mapping);
            graphs.push_back(H);
            cut = minimum_cut_helpers<GraphPtr>::updateCut(graphs, cut);

            union_find uf = tests::prTests12(graphs.back(), cut);
            graphs.push_back(
                contraction::fromUnionFind(graphs.back(), &uf, true));
            cut = minimum_cut_helpers<GraphPtr>::updateCut(graphs, cut);
            union_find uf2 = tests::prTests34(graphs.back(), cut);
            graphs.push_back(
                contraction::fromUnionFind(graphs.back(), &uf2, true));
            cut = minimum_cut_helpers<GraphPtr>::updateCut(graphs, cut);
        }

        if (graphs.back()->number_of_nodes() > 1) {
            timer t;
            noi_minimum_cut<GraphPtr> noi;
            cut = std::min(cut, noi.perform_minimum_cut(graphs.back(), true));
        }

        if (!indirect && configuration::getConfig()->save_cut)
            minimum_cut_helpers<GraphPtr>::retrieveMinimumCut(graphs);

        return cut;
    }
};
