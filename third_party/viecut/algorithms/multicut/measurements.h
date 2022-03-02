/******************************************************************************
 * measurements.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2020 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/
#pragma once

#include <memory>
#include <vector>

#include "common/definitions.h"
#include "data_structure/mutable_graph.h"

class measurements {
 private:
#ifndef NDEBUG
    static const bool debug = true;
#else
    static const bool debug = false;
#endif
    const mutable_graph& original_graph;
    const std::vector<NodeID>& original_terminals;

 public:
    measurements(const mutable_graph& original_graph,
                 const std::vector<NodeID>& original_terminals)
        : original_graph(original_graph),
          original_terminals(original_terminals) { }

    std::vector<NodeID> getSolution(problemPointer problem) {
        std::vector<NodeID> current_solution(original_graph.n(), 0);
        std::vector<size_t> blocksize(original_terminals.size(), 0);
        for (NodeID n = 0; n < current_solution.size(); ++n) {
            NodeID n_coarse = problem->mapped(n);
            auto t = problem->graph->getCurrentPosition(n_coarse);
            current_solution[n] = problem->graph->getPartitionIndex(t);
            blocksize[current_solution[n]]++;
        }

        if (debug) {
            std::vector<size_t> term_in_block(original_terminals.size(), 0);
            for (const auto& t : original_terminals) {
                ++term_in_block[current_solution[t]];
            }

            for (size_t i = 0; i < term_in_block.size(); ++i) {
                if (term_in_block[i] != 1) {
                    exit(1);
                }
            }
        }
        return current_solution;
    }

    FlowType flowValue(bool verbose, const std::vector<NodeID>& sol) {
        std::vector<size_t> block_sizes(original_terminals.size(), 0);

        EdgeWeight total_weight = 0;
        for (NodeID n : original_graph.nodes()) {
            block_sizes[sol[n]]++;
            for (EdgeID e : original_graph.edges_of(n)) {
                NodeID tgt = original_graph.getEdgeTarget(n, e);
                EdgeWeight wgt = original_graph.getEdgeWeight(n, e);
                if (sol[n] != sol[tgt]) {
                    total_weight += wgt;
                }
            }
        }
        total_weight /= 2;

        for (size_t i = 0; i < block_sizes.size(); ++i) {
            size_t terminals = 0;
            if (block_sizes[i] == 0) {
                exit(1);
            }

            for (NodeID ot : original_terminals) {
                if (sol[ot] == i) {
                    terminals++;
                }
            }

            if (terminals != 1) {
                exit(1);
            }
        }
        return total_weight;
    }
};
