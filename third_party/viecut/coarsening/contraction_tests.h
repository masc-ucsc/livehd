/******************************************************************************
 * contraction_tests.h
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
#include <memory>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/union_find.h"

class tests {
 private:
    static void sort3(EdgeWeight w1, EdgeWeight w2, EdgeWeight
                      w3) {
        if (w1 < w2) {
            if (w2 < w3) {
                return;
            } else if (w1 < w3) {
                std::swap(w2, w3);
            } else {
                EdgeWeight tmp = std::move(w1);
                w1 = std::move(w3);
                w3 = std::move(w2);
                w2 = std::move(tmp);
            }
        } else {
            if (w1 < w3) {
                std::swap(w1, w2);
            } else if (w3 < w2) {
                std::swap(w1, w3);
            } else {
                EdgeWeight tmp = std::move(w1);
                w1 = std::move(w2);
                w2 = std::move(w3);
                w3 = std::move(tmp);
            }
        }
    }

 public:
    template <class GraphPtr>
    static union_find prTests12(GraphPtr G,
                                EdgeWeight limit,
                                bool find_all_cuts = false) {
        union_find uf(G->number_of_nodes());
        G->computeDegrees();
        std::vector<bool> contracted(G->number_of_nodes(), false);
        for (NodeID n : G->nodes()) {
            NodeWeight n_wgt = G->getWeightedNodeDegree(n);
            for (EdgeID e : G->edges_of(n)) {
                auto [t, wgt] = G->getEdge(n, e);
                NodeID target = uf.Find(t);
                NodeID t_wgt = G->getWeightedNodeDegree(t);
                NodeID source = uf.Find(n);

                if (wgt >= limit) {
                    uf.Union(source, target);
                    contracted[n] = true;
                    contracted[t] = true;
                }

                // if we want to find all cuts
                // we are not allowed to contract an edge
                // if an incident vertex has degree mincut
                // (as the singleton cut might be important)
                if (!find_all_cuts || (n_wgt >= limit && t_wgt >= limit)) {
                    if (2 * wgt > n_wgt && (!find_all_cuts || !contracted[n])) {
                        contracted[n] = true;
                        contracted[t] = true;
                        uf.Union(n, t);
                    }
                    if (2 * wgt > t_wgt && (!find_all_cuts || !contracted[t])) {
                        contracted[n] = true;
                        contracted[t] = true;
                        uf.Union(n, t);
                    }
                }
            }
        }
        return uf;
    }

    template <class GraphPtr>
    static union_find prTests34(GraphPtr G,
                                EdgeWeight weight_limit,
                                bool find_all_cuts = false) {
        union_find uf(G->number_of_nodes());
        std::vector<std::pair<NodeID, EdgeID> > marked(
            G->number_of_nodes(),
            std::make_pair(UNDEFINED_NODE, UNDEFINED_EDGE));
        std::vector<bool> finished(G->number_of_nodes(), false);
        std::vector<bool> contracted(G->number_of_nodes(), false);

        for (NodeID n : G->nodes()) {
            if (finished[n])
                continue;

            finished[n] = true;
            for (EdgeID e : G->edges_of(n)) {
                NodeID tgt = G->getEdgeTarget(n, e);
                if (tgt > n) {
                    marked[tgt] = std::make_pair(n, e);
                }
            }

            EdgeWeight deg_n = G->getWeightedNodeDegree(n);
            for (EdgeID e1 : G->edges_of(n)) {
                NodeID tgt = G->getEdgeTarget(n, e1);
                EdgeWeight deg_tgt = G->getWeightedNodeDegree(tgt);
                if (finished[tgt]) {
                    marked[tgt] =
                        std::make_pair(UNDEFINED_NODE, UNDEFINED_EDGE);
                    continue;
                }

                EdgeWeight w1 = G->getEdgeWeight(n, e1);
                finished[tgt] = true;
                EdgeWeight wgt_sum = w1;
                if (tgt > n) {
                    for (EdgeID e2 : G->edges_of(tgt)) {
                        NodeID tgt2 = G->getEdgeTarget(tgt, e2);
                        if (marked[tgt2].second == UNDEFINED_EDGE)
                            continue;

                        if (marked[tgt2].first != n) {
                            continue;
                        }

                        EdgeWeight w2 = G->getEdgeWeight(tgt, e2);
                        EdgeWeight w3 =
                            G->getEdgeWeight(n, marked[tgt2].second);

                        wgt_sum += std::min(w2, w3);

                        bool contractible_one_cut =
                            !find_all_cuts && 2 * (w1 + w3) >= deg_n
                            && 2 * (w1 + w2) >= deg_tgt;

                        // if we want to find all cuts
                        // we are not allowed to contract an edge
                        // when an incident vertex has degree mincut
                        // (as the singleton cut might be important)
                        // also the triangle having exactly half the weight
                        // of n or tgt ir not enough any more
                        bool contractible_all_cuts =
                            find_all_cuts
                            && 2 * (w1 + w3) > deg_n && 2 * (w1 + w2) > deg_tgt
                            && deg_n >= weight_limit && deg_tgt >= weight_limit;

                        if ((contractible_one_cut || contractible_all_cuts)
                            && !contracted[n] && !contracted[tgt]) {
                            uf.Union(n, tgt);
                            contracted[n] = true;
                            contracted[tgt] = true;
                        }
                    }

                    if (wgt_sum >= weight_limit) {
                        uf.Union(n, tgt);
                        contracted[n] = true;
                        contracted[tgt] = true;
                    }
                    marked[tgt] =
                        std::make_pair(UNDEFINED_NODE, UNDEFINED_EDGE);
                }
            }
        }
        return uf;
    }

    static void findHeavyEdges(graphAccessPtr G,
                               union_find* uf,
                               EdgeWeight weight_limit) {
        for (NodeID n : G->nodes()) {
            for (EdgeID e : G->edges_of(n)) {
                if (G->getEdgeWeight(e) > weight_limit) {
                    uf->Union(n, G->getEdgeTarget(e));
                }
            }
        }
    }

    static void findHeavyTriangles(graphAccessPtr G,
                                   union_find* uf,
                                   EdgeWeight weight_limit) {
        std::vector<bool> marked(G->number_of_nodes(), false);

        for (NodeID n : G->nodes()) {
            for (EdgeID e : G->edges_of(n)) {
                NodeID tgt = G->getEdgeTarget(e);
                if (tgt > n) {
                    marked[tgt] = true;
                }
            }

            for (EdgeID e1 : G->edges_of(n)) {
                NodeID tgt = G->getEdgeTarget(e1);
                if (tgt > n) {
                    for (EdgeID e2 : G->edges_of(tgt)) {
                        NodeID tgt2 = G->getEdgeTarget(e2);
                        if (marked[tgt2]) {
                            for (EdgeID e3 : G->edges_of(n)) {
                                if (G->getEdgeTarget(e3) == tgt2) {
                                    EdgeWeight w1 = G->getEdgeWeight(e1);
                                    EdgeWeight w2 = G->getEdgeWeight(e2);
                                    EdgeWeight w3 = G->getEdgeWeight(e3);

                                    sort3(w1, w2, w3);

                                    if (w1 + w2 > weight_limit) {
                                        uf->Union(n, tgt);
                                        uf->Union(n, tgt2);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    marked[tgt] = false;
                }
            }
        }
    }
};
