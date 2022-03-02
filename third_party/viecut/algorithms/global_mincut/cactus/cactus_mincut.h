/******************************************************************************
 * cactus_mincut.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "algorithms/global_mincut/cactus/most_balanced_minimum_cut.h"
#include "algorithms/global_mincut/cactus/recursive_cactus.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "algorithms/global_mincut/noi_minimum_cut.h"
#include "algorithms/global_mincut/viecut.h"
#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/priority_queues/maxNodeHeap.h"
#include "io/graph_io.h"
#include "tools/string.h"
#include "tools/timer.h"

#ifdef PARALLEL
#include "parallel/coarsening/contract_graph.h"
#else
#include "coarsening/contract_graph.h"
#endif

template <class GraphPtr>
class cactus_mincut : public minimum_cut {
 public:
    typedef GraphPtr GraphPtrType;
    cactus_mincut() { }
    virtual ~cactus_mincut() { }
    static constexpr bool debug = false;

    bool timing = configuration::getConfig()->verbose;

    EdgeWeight perform_minimum_cut(GraphPtr G) {
        // compatibility with min cut interface
        return std::get<0>(findAllMincuts(G));
    }

    std::tuple<EdgeWeight, mutableGraphPtr,
               std::vector<std::pair<NodeID, EdgeID> > >
    findAllMincuts(GraphPtr G, EdgeWeight known_mincut = UNDEFINED_NODE) {
        std::vector<GraphPtr> v = { G };
        return findAllMincuts(v, known_mincut);
    }

    std::tuple<EdgeWeight, mutableGraphPtr,
               std::vector<std::pair<NodeID, EdgeID> > >
    findAllMincuts(std::vector<GraphPtr> graphs,
                   EdgeWeight known_mincut = UNDEFINED_NODE) {
        if (graphs.size() == 0 || !graphs.back()) {
            mutableGraphPtr empty;
            return std::make_tuple(
                -1, empty, std::vector<std::pair<NodeID, EdgeID> > { });
        }
        recursive_cactus<GraphPtr> rc;
        EdgeWeight mincut = graphs.back()->getMinDegree();
        timer t;
        if (known_mincut == UNDEFINED_NODE) {
            viecut<GraphPtr> vc;
            mincut = vc.perform_minimum_cut(graphs.back());
        } else {
            mincut = known_mincut;
        }
        noi_minimum_cut<GraphPtr> noi;
        std::vector<std::vector<std::pair<NodeID, NodeID> > > guaranteed_edges;
        std::vector<size_t> ge_ids;

        minimum_cut_helpers<GraphPtr>::setInitialCutValues(graphs);

        NodeID previous_size = UNDEFINED_NODE;
        while (graphs.back()->number_of_nodes() * 1.01 < previous_size) {
            previous_size = graphs.back()->number_of_nodes();
            auto current_graph = graphs.back();
            EdgeWeight current_mincut = mincut;
            ge_ids.emplace_back(graphs.size() - 1);
            guaranteed_edges.emplace_back();

            std::vector<std::pair<NodeID, NodeID> > contractable;

            timer time;
            auto uf = noi.modified_capforest(current_graph, mincut + 1);

            for (NodeID n : current_graph->nodes()) {
                EdgeID e = current_graph->get_first_edge(n);
                if (current_graph->get_first_invalid_edge(n) - e == 1) {
                    if ((current_graph->getEdgeWeight(n, e) == mincut)
                        && uf.n() > 1) {
                        NodeID t = current_graph->getEdgeTarget(n, e);
                        uf.Union(n, t);
                        guaranteed_edges.back().emplace_back(n, t);
                    }
                }
            }

            if (uf.n() < current_graph->number_of_nodes()) {
                auto newg =
                    contraction::fromUnionFind(current_graph, &uf, true);
                graphs.emplace_back(newg);
                mincut = minimum_cut_helpers<GraphPtr>::updateCut(
                    graphs, mincut);
            }

            union_find uf12 = tests::prTests12(
                graphs.back(), mincut + 1, true);
            if (uf12.n() < graphs.back()->number_of_nodes()) {
                auto g12 = contraction::fromUnionFind(
                    graphs.back(), &uf12, true);
                graphs.push_back(g12);
                mincut = minimum_cut_helpers<GraphPtr>::updateCut(
                    graphs, mincut);
            }

            union_find uf34 = tests::prTests34(
                graphs.back(), mincut + 1, true);
            if (uf34.n() < graphs.back()->number_of_nodes()) {
                auto g34 = contraction::fromUnionFind(
                    graphs.back(), &uf34, true);
                graphs.push_back(g34);
                mincut = minimum_cut_helpers<GraphPtr>::updateCut(
                    graphs, mincut);
            }

            if (current_mincut > mincut) {
                // mincut has improved
                // so all the edges that were in guaranteed edges before
                // are now _not_ cactus edges as there is a lighter cut
                guaranteed_edges.clear();
                ge_ids.clear();
            }
        }

        if (graphs.back()->number_of_nodes() > 1)
            mincut = noi.perform_minimum_cut(graphs.back());

        rc.setMincut(mincut);
        auto out_graph = rc.flowMincut(graphs);

        minimum_cut_helpers<GraphPtr>::setVertexLocations(
            out_graph, graphs, ge_ids, guaranteed_edges, mincut);

        std::vector<std::pair<NodeID, EdgeID> > mb_edges;
        if (configuration::getConfig()->find_most_balanced_cut) {
            most_balanced_minimum_cut<GraphPtr> mbmc;
            mb_edges = mbmc.findCutFromCactus(out_graph, mincut, graphs[0]);
        }

        return std::make_tuple(mincut, out_graph, mb_edges);
    }
};
