/******************************************************************************
 * all_cut_local_red.h
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
#include <memory>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/mutable_graph.h"

#ifdef PARALLEL
#include "parallel/data_structure/union_find.h"
#else
#include "data_structure/union_find.h"
#endif

class all_cut_local_red {
 public:
    static union_find allCutsPrTests12(mutableGraphPtr G,
                                       EdgeWeight limit) {
        union_find uf(G->number_of_nodes());
        std::vector<EdgeWeight> degrees;
        std::vector<bool> contracted(G->number_of_nodes(), false);

        for (NodeID n : G->nodes()) {
            degrees.push_back(G->getWeightedNodeDegree(n));
        }

        for (NodeID n : G->nodes()) {
            for (EdgeID e : G->edges_of(n)) {
                NodeID target = uf.Find(G->getEdgeTarget(n, e));
                NodeID source = uf.Find(n);
                EdgeWeight wgt = G->getEdgeWeight(n, e);

                if (target != source
                    && (wgt > limit
                        || 2 * wgt > degrees[source]
                        || 2 * wgt > degrees[target])
                    && degrees[source] > limit
                    && degrees[target] > limit) {
                    EdgeWeight new_w =
                        degrees[source] + degrees[target] - (2 * wgt);

                    degrees[source] = new_w;
                    degrees[target] = new_w;
                    uf.Union(source, target);
                }
            }
        }
        return uf;
    }

    static union_find allCutsPrTests34(mutableGraphPtr G,
                                       EdgeWeight weight_limit) {
        union_find uf(G->number_of_nodes());
        std::vector<EdgeID> marked(G->number_of_nodes(), UNDEFINED_EDGE);
        std::vector<bool> finished(G->number_of_nodes(), false);
        std::vector<bool> contracted(G->number_of_nodes(), false);

        for (NodeID n : G->nodes()) {
            if (finished[n])
                continue;

            finished[n] = true;
            for (EdgeID e : G->edges_of(n)) {
                NodeID tgt = G->getEdgeTarget(n, e);
                if (tgt > n) {
                    marked[tgt] = e;
                }
            }

            EdgeWeight deg_n = G->getWeightedNodeDegree(n);
            for (EdgeID e1 : G->edges_of(n)) {
                NodeID tgt = G->getEdgeTarget(n, e1);
                EdgeWeight deg_tgt = G->getWeightedNodeDegree(tgt);
                if (finished[tgt]) {
                    marked[tgt] = UNDEFINED_EDGE;
                    continue;
                }

                finished[tgt] = true;
                EdgeWeight wgt_sum = G->getEdgeWeight(n, e1);
                if (tgt > n) {
                    for (EdgeID e2 : G->edges_of(tgt)) {
                        NodeID tgt2 = G->getEdgeTarget(tgt, e2);
                        if (marked[tgt2] == UNDEFINED_EDGE)
                            continue;

                        if (marked[tgt2] >= G->get_first_invalid_edge(n)
                            || marked[tgt2] < G->get_first_edge(n)) {
                            continue;
                        }

                        EdgeWeight w1 = G->getEdgeWeight(n, e1);
                        EdgeWeight w2 = G->getEdgeWeight(tgt, e2);
                        EdgeWeight w3 = G->getEdgeWeight(n, marked[tgt2]);

                        wgt_sum += std::min(w2, w3);

                        bool contractible =
                            2 * (w1 + w3) > deg_n && 2 * (w1 + w2) > deg_tgt
                            && deg_n > weight_limit && deg_tgt > weight_limit;

                        if (contractible
                            && !contracted[n] && !contracted[tgt]) {
                            uf.Union(n, tgt);
                            contracted[n] = true;
                            contracted[tgt] = true;
                        }
                    }

                    if (wgt_sum > weight_limit) {
                        uf.Union(n, tgt);
                        contracted[n] = true;
                        contracted[tgt] = true;
                    }
                    marked[tgt] = UNDEFINED_EDGE;
                }
            }
        }
        return uf;
    }
};
