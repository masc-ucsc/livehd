/******************************************************************************
 * stoer_wagner_minimum_cut.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "data_structure/adjlist_graph.h"
#include "data_structure/priority_queues/bucket_pq.h"

class stoer_wagner_minimum_cut {
 public:
    stoer_wagner_minimum_cut() { }
    virtual ~stoer_wagner_minimum_cut() { }

    /**
     * perform minimum cut: right now wagner stoer algorithm
     */
    uint64_t perform_minimum_cut(adjlist_graph G) {
        srand(time(NULL));
        uint64_t global_mincut = UINT64_MAX;
        union_find uf(G.number_of_nodes());
        for (uint64_t i = G.number_of_nodes(); i > 2; --i) {
            uint64_t mincut = contract(G, i, uf);
            if (mincut < global_mincut) {
                global_mincut = mincut;
            }
        }

        return global_mincut;
    }

 private:
    uint64_t contract(adjlist_graph G, uint64_t size, union_find uf) {
        bucket_pq pq(G.getMaxDegree());

        std::vector<bool> visited(G.number_of_nodes());
        NodeID starting_node =
            random_functions::nextInt(0, G.number_of_nodes() - 1);

        NodeID current_node = uf.Find(starting_node);

        visited[current_node] = true;

        size_t num_inserts = 0, num_deletes = 0;

        for (uint64_t i = 0; i < size - 2; ++i) {
            assert(uf.Find(current_node) == current_node);

            for (size_t e = 0; e < G.number_of_edges(); ++e) {
                NodeID tgt = uf.Find(G.getEdgeTarget(e));
                if (!visited[tgt]) {
                    Gain gain = pq.gain(tgt);
                    if (gain > 0) {
                        pq.increaseKey(tgt, gain + G.getEdgeWeight(e));
                    } else {
                        num_inserts++;
                        assert(G.getEdgeWeight(e) > 0);
                        pq.insert(tgt, G.getEdgeWeight(e));
                    }
                }
            }
            num_deletes++;
            current_node = pq.deleteMax();

            assert(uf.Find(current_node) == current_node);
            visited[current_node] = true;
        }

        for (size_t e = 0; e < G.number_of_edges(); ++e) {
            NodeID tgt = uf.Find(G.getEdgeTarget(e));
            if (!visited[tgt]) {
                Gain gain = pq.gain(tgt);
                if (gain > 0) {
                    pq.increaseKey(tgt, gain + G.getEdgeWeight(e));
                } else {
                    num_inserts++;
                    assert(G.getEdgeWeight(e) > 0);
                    pq.insert(tgt, G.getEdgeWeight(e));
                }
            }
        }

        uf.Union(current_node, pq.maxElement());

        if (uf.Find(current_node) == current_node) {
            G.merge_nodes(current_node, pq.maxElement());
        } else {
            G.merge_nodes(pq.maxElement(), current_node);
        }

        assert(uf.Find(pq.maxElement()) == current_node);

        assert(pq.size() == 1);

        return pq.maxValue();
    }
};
