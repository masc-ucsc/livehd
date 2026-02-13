/******************************************************************************
 * ks_minimum_cut.h
 *
 * Source of VieCut.
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
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "algorithms/global_mincut/minimum_cut.h"
#include "algorithms/global_mincut/noi_minimum_cut.h"
#include "data_structure/graph_access.h"
#include "tools/random_functions.h"
#include "tools/timer.h"

#ifdef PARALLEL
#include "parallel/coarsening/contract_graph.h"
#include "parallel/data_structure/union_find.h"
#else
#include "coarsening/contract_graph.h"
#include "data_structure/union_find.h"
#endif
#include "algorithms/global_mincut/minimum_cut_helpers.h"
#include "common/definitions.h"

class ks_minimum_cut : public minimum_cut {
 public:
    typedef graphAccessPtr GraphPtrType;
    static const bool debug = false;
    static const bool timing = true;

    ks_minimum_cut() { }

    ~ks_minimum_cut() { }

    EdgeWeight perform_minimum_cut(graphAccessPtr G) {
        if (!G) {
            return -1;
        }
        EdgeWeight mincut = std::numeric_limits<EdgeWeight>::max();
        std::vector<PartitionID> best_partition(G->number_of_nodes());
        timer t;
        const bool save_cut = configuration::getConfig()->save_cut;
        for (size_t i = 0; i < std::log2(G->number_of_nodes())
             && mincut > configuration::getConfig()->optimal; ++i) {
            graphs.push_back(G);
            EdgeWeight curr_cut = recurse(0, i).first;

            if (save_cut) {
                if (curr_cut < mincut) {
                    for (NodeID n : G->nodes()) {
                        best_partition[n] = G->getNodeInCut(n);
                    }
                }
            }
            mincut = std::min(mincut, curr_cut);

            graphs.clear();
        }

        for (NodeID n : G->nodes()) {
            G->setNodeInCut(n, best_partition[n]);
        }

        return mincut;
    }

    NodeID sample_contractible(graphAccessPtr G,
                               NodeID currentN,
                               union_find* uf,
                               double reduction,
                               size_t iteration = 0) {
        NodeID n_reduce = std::min(
            static_cast<NodeID>(static_cast<double>(currentN) * reduction),
            currentN - 2);

        EdgeID num_edges = G->number_of_edges();

        std::mt19937_64 local_mt(iteration);

        size_t reduced = 0;

        while (reduced < n_reduce) {
            EdgeWeight e_rand = local_mt() % num_edges;
            NodeID src = G->getEdgeSource(e_rand);
            NodeID tgt = G->getEdgeTarget(e_rand);
            if (uf->Union(src, tgt))
                ++reduced;
        }

        return G->number_of_nodes() - reduced;
    }

    NodeID sample_contractible_weighted(graphAccessPtr G,
                                        NodeID currentN,
                                        union_find* uf,
                                        double reduction,
                                        size_t iteration = 0) {
        NodeID n_reduce = std::min(
            static_cast<NodeID>(static_cast<double>(currentN) * reduction),
            currentN - 2);

        timer t;
        std::vector<EdgeWeight> prefixsum;
        prefixsum.reserve(G->number_of_edges());
        size_t wgt = 0;

        for (NodeID n : G->nodes()) {
            for (EdgeID e : G->edges_of(n)) {
                wgt += G->getEdgeWeight(e);
                prefixsum.emplace_back(wgt);
            }
        }

        EdgeWeight num_edges = wgt;

        size_t contracted = 0;
        std::mt19937_64 local_mt(iteration);

        while (contracted < n_reduce) {
            EdgeWeight e_rand = local_mt() % num_edges;

            auto edge =
                std::lower_bound(prefixsum.begin(), prefixsum.end(), e_rand,
                                 [](const auto& in1, const EdgeWeight& e) {
                                     return in1 < e;
                                 });

            EdgeID e = edge - prefixsum.begin();

            NodeID src = G->getEdgeSource(e);
            NodeID tgt = G->getEdgeTarget(e);

            if (uf->Union(src, tgt)) {
                ++contracted;
            }
        }
        return currentN - contracted;
    }

    std::pair<EdgeWeight, size_t> recurse(size_t current,
                                          size_t iteration) {
        graphAccessPtr G = graphs[current];

        NodeID currentN = G->number_of_nodes();

        union_find uf(G->number_of_nodes());

        if (current > 0) {
            for (NodeID n : G->nodes()) {
                for (EdgeID e : G->edges_of(n)) {
                    NodeID tgt = G->getEdgeTarget(e);
                    EdgeWeight wgte = G->getEdgeWeight(e);
                    if (tgt > n && currentN > 2) {
                        NodeWeight degn = G->getWeightedNodeDegree(n);
                        NodeWeight degt = G->getWeightedNodeDegree(tgt);
                        if (wgte > G->getMinDegree() || wgte * 2 > degn
                            || wgte * 2 > degt) {
                            NodeID first = uf.Find(n);
                            NodeID second = uf.Find(G->getEdgeTarget(e));

                            if (first != second) {
                                currentN--;
                                uf.Union(first, second);
                            }
                        }
                    }
                }
            }
        }

        sample_contractible(G, currentN, &uf, 0.4, iteration);
        // sample_contractible_weighted(G, currentN, uf, 0.4, iteration);

        graphAccessPtr G2 = contraction::fromUnionFind(G, &uf);

        graphs.push_back(G2);
        size_t current_position = graphs.size() - 1;
        EdgeWeight mincut_to_return = 0;

        if (graphs.back()->number_of_nodes() > 50) {
            auto w1 = recurse(current_position, iteration);
            /* pseudo-random seed */
            auto w2 = recurse(current_position, iteration + 9273);

            if (configuration::getConfig()->save_cut) {
                size_t u_from = w1.first < w2.first ? w1.second : w2.second;
                for (NodeID n : G2->nodes()) {
                    NodeID ctr = G2->getPartitionIndex(n);
                    PartitionID pid = graphs[u_from]->getNodeInCut(ctr);
                    G2->setNodeInCut(n, pid);
                }
            }
            mincut_to_return = std::min(w1.first, w2.first);
        } else {
            noi_minimum_cut<graphAccessPtr> mc;
            mincut_to_return = mc.perform_minimum_cut(G2, true);
        }

        if (!current) {
            // last graph, propagate to top.
            std::vector<size_t> up(2, 0);
            for (NodeID n : G->nodes()) {
                NodeID ctr = G->getPartitionIndex(n);
                PartitionID pid = G2->getNodeInCut(ctr);
                G->setNodeInCut(n, pid);
                up[pid]++;
            }
        }
        return std::make_pair(mincut_to_return, current_position);
    }

 private:
    std::vector<graphAccessPtr> graphs;
};
