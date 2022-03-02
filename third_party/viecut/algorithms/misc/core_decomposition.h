/******************************************************************************
 * core_decomposition.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <stdlib.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "algorithms/misc/strongly_connected_components.h"
#include "common/definitions.h"
#include "data_structure/graph_access.h"

struct k_cores {
    std::vector<NodeID> degrees;
    std::vector<NodeID> vertices;
    std::vector<NodeID> position;
    std::vector<NodeID> buckets;

    explicit k_cores(NodeID n) {
        degrees.resize(n);
        vertices.resize(n);
        position.resize(n);
        buckets.resize(1);
    }
};

class core_decomposition {
 public:
// Implementation of k-core decomposition global_mincut
// of Batagelj and Zaversnik (https://arxiv.org/abs/cs/0310049)
    static k_cores batagelj_zaversnik(graphAccessPtr G) {
        k_cores kCores(G->number_of_nodes());

        EdgeWeight max_degree = 0;

        for (NodeID n : G->nodes()) {
            EdgeWeight deg = G->getUnweightedNodeDegree(n);
            if (deg > max_degree) {
                kCores.buckets.resize(deg + 1);
                max_degree = deg;
            }
            kCores.degrees[n] = static_cast<unsigned int>(deg);
            kCores.buckets[deg]++;
        }

        NodeID start = 0;
        for (EdgeWeight i = 0; i < max_degree + 1; ++i) {
            // prefix sum for bucket sort
            NodeID num = kCores.buckets[i];
            kCores.buckets[i] = start;
            start += num;
        }

        for (NodeID n : G->nodes()) {
            EdgeWeight n_deg = G->getUnweightedNodeDegree(n);
            kCores.position[n] = kCores.buckets[n_deg];
            kCores.vertices[kCores.position[n]] = n;
            ++kCores.buckets[n_deg];
        }

        for (EdgeWeight i = max_degree; i > 0; --i) {
            kCores.buckets[i] = kCores.buckets[i - 1];
        }
        kCores.buckets[0] = 0;

        for (NodeID n : G->nodes()) {
            NodeID v = kCores.vertices[n];
            for (EdgeID e : G->edges_of(v)) {
                NodeID target = G->getEdgeTarget(e);
                if (kCores.degrees[target] > kCores.degrees[v]) {
                    uint32_t degree_t = kCores.degrees[target];
                    uint32_t pos_t = kCores.position[target];
                    uint32_t pos_swap = kCores.buckets[degree_t];
                    NodeID vert_swap = kCores.vertices[pos_swap];
                    if (target != vert_swap) {
                        kCores.position[target] = pos_swap;
                        kCores.position[vert_swap] = pos_t;
                        kCores.vertices[pos_t] = vert_swap;
                        kCores.vertices[pos_swap] = target;
                    }
                    ++kCores.buckets[degree_t];
                    --kCores.degrees[target];
                }
            }
        }

        return kCores;
    }

    static graphAccessPtr createCoreGraph(
        k_cores kCores, NodeID k, graphAccessPtr G) {
        size_t min_degree = kCores.degrees[kCores.vertices[kCores.buckets[k]]];
        std::vector<NodeID> reverse(G->number_of_nodes(), G->number_of_nodes());
        std::vector<NodeID> core;
        uint64_t num_edges = 0;

        for (NodeID node = 0; node < G->number_of_nodes(); ++node) {
            if (kCores.position[node] >= kCores.buckets[k]) {
                reverse[node] = static_cast<unsigned int>(core.size());
                core.push_back(node);
                num_edges += kCores.degrees[
                    kCores.vertices[kCores.position[node]]];
            }
        }
        num_edges *= 2;

        auto core_graph = std::make_shared<graph_access>();

        core_graph->start_construction((NodeID)core.size(), num_edges);

        for (uint32_t i = 0; i < core.size(); ++i) {
            NodeID node = core[i];
            core_graph->new_node();
            for (EdgeID e : G->edges_of(node)) {
                NodeID target = reverse[G->getEdgeTarget(e)];
                if (target != G->number_of_nodes()) {
                    EdgeID new_e = core_graph->new_edge(i, target);
                    core_graph->setEdgeWeight(new_e, G->getEdgeWeight(e));
                }
            }
        }

        core_graph->finish_construction();

        if (core_graph->getMinDegree() != min_degree) {
            exit(1);
        }

        std::vector<int> components(core_graph->number_of_nodes());
        strongly_connected_components scc;

        return scc.largest_scc(core_graph);
    }
};
