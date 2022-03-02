/******************************************************************************
 * graph_modification.h
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
#include <map>
#include <memory>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/mutable_graph.h"

class graph_modification {
 public:
    static mutableGraphPtr mergeGraphs(
        mutableGraphPtr G1, NodeID v1,
        mutableGraphPtr G2, NodeID v2, EdgeWeight mincut) {
        NodeID v1_n = G1->number_of_nodes();
        if (G2->number_of_nodes() == 1) {
            return G1;
        }
        if (G1->number_of_nodes() == 1) {
            return G2;
        }

        std::vector<NodeID> empty;
        G1->setContainedVertices(v1, empty);
        for (size_t i = 0; i < G2->number_of_nodes(); ++i) {
            if (i == v2) {
                for (NodeID c : G2->containedVertices(i)) {
                    if (G1->getCurrentPosition(c) == v1) {
                        G1->setCurrentPosition(c, v1);
                        G1->addContainedVertex(v1, c);
                    }
                }
            } else {
                NodeID v = G1->new_empty_node();
                for (NodeID c : G2->containedVertices(i)) {
                    G1->setCurrentPosition(c, v);
                }
                G1->setContainedVertices(v, G2->containedVertices(i));
            }
        }

        for (NodeID n : G2->nodes()) {
            NodeID new_id = n + v1_n - (n > v2);
            if (n == v2)
                new_id = v1;
            for (EdgeID e : G2->edges_of(n)) {
                NodeID t = G2->getEdgeTarget(n, e);
                if (t != v2) {
                    NodeID t_id = t + v1_n - (t > v2);
                    G1->new_edge(new_id, t_id, G2->getEdgeWeight(n, e));
                }
            }
        }
        if (G1->isEmpty(v1))
            canonizeCactus(G1, v1, mincut);
        return G1;
    }

    static bool canonizeCactus(mutableGraphPtr cactus,
                               NodeID vertex, EdgeWeight mincut) {
        size_t junctions = 0;
        std::vector<std::tuple<NodeID, EdgeID, EdgeID> > neighbors_tree;
        std::vector<std::pair<NodeID, EdgeID> > neighbors_cycle;
        for (EdgeID e : cactus->edges_of(vertex)) {
            EdgeWeight wgt = cactus->getEdgeWeight(vertex, e);
            if (wgt == mincut) {
                neighbors_tree.emplace_back(cactus->getEdgeTarget(vertex, e),
                                            cactus->getReverseEdge(vertex, e),
                                            e);
            } else {
                VIECUT_ASSERT_EQ(mincut % 2, 0);
                VIECUT_ASSERT_EQ(mincut / 2, wgt);
                neighbors_cycle.emplace_back(cactus->getEdgeTarget(vertex, e),
                                             cactus->getReverseEdge(vertex, e));
            }
            junctions += wgt;
        }

        if (junctions % mincut != 0) {
            exit(1);
        }
        junctions /= mincut;
        if (junctions == 2 && !neighbors_tree.empty()) {
            EdgeID ed = std::get<2>(neighbors_tree[0]);
            cactus->contractEdge(vertex, ed);
            return true;
        }

        if (junctions == 3) {
            std::vector<std::pair<std::pair<NodeID, EdgeID>,
                                  std::pair<NodeID, EdgeID> > > pairs;
            while (neighbors_cycle.size() > 2) {
                // find matching vertices in cycle, remove from array
                // (only up to 6 elements in vector, so O(n) delete is fine)
                std::map<NodeID, size_t> neighbors;
                for (size_t i = 1; i < neighbors_cycle.size(); ++i) {
                    neighbors.emplace(neighbors_cycle[i].first, i);
                }

                std::queue<NodeID> q;
                std::vector<bool> seen(cactus->number_of_nodes(), false);
                // if cactus is actually a cactus,
                // the cycles are only connected through 'vertex'
                seen[vertex] = true;
                q.push(neighbors_cycle[0].first);
                while (!q.empty()) {
                    NodeID n = q.front();
                    q.pop();
                    for (EdgeID e : cactus->edges_of(n)) {
                        NodeID tgt = cactus->getEdgeTarget(n, e);
                        if (neighbors.count(tgt)) {
                            pairs.emplace_back(neighbors_cycle[0],
                                               neighbors_cycle[neighbors[tgt]]);
                            std::queue<NodeID> empty;
                            std::swap(q, empty);
                            neighbors_cycle.erase(neighbors_cycle.begin()
                                                  + neighbors[tgt]);
                            neighbors_cycle.erase(neighbors_cycle.begin());
                            break;
                        }
                        if (!seen[tgt]) {
                            q.push(tgt);
                        }
                    }
                }
            }
            if (neighbors_cycle.size() == 2) {
                pairs.emplace_back(neighbors_cycle[0], neighbors_cycle[1]);
            }
            VIECUT_ASSERT_EQ(pairs.size() + neighbors_tree.size(), 3);
            // delete vertex and replace with 3 new empty 2-junction vertices
            // merge the ones that are in a 2-cycle
            std::vector<NodeID> new_nodes;
            for (auto p : pairs) {
                new_nodes.emplace_back(cactus->new_empty_node());
                cactus->new_edge_order(new_nodes.back(),
                                       p.first.first, mincut / 2);
                cactus->new_edge_order(new_nodes.back(),
                                       p.second.first, mincut / 2);
            }
            for (auto n : neighbors_tree) {
                new_nodes.emplace_back(std::get<0>(n));
            }
            cactus->new_edge_order(new_nodes[0], new_nodes[1], mincut / 2);
            cactus->new_edge_order(new_nodes[0], new_nodes[2], mincut / 2);
            cactus->new_edge_order(new_nodes[1], new_nodes[2], mincut / 2);
            cactus->deleteVertex(vertex);
            return true;
        }
        return false;
    }

    static bool isCNCR(mutableGraphPtr G, EdgeWeight mincut) {
        for (NodeID n : G->nodes()) {
            if (G->isEmpty(n)) {
                EdgeWeight deg = 0;
                NodeID num = 0;
                for (EdgeID e : G->edges_of(n)) {
                    deg += G->getEdgeWeight(n, e);
                    num++;
                }

                if (deg % mincut != 0) {
                    return false;
                }

                if (deg / mincut == 3) {
                    return false;
                }

                if (deg / mincut == 2) {
                    if (num < 4) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
};
