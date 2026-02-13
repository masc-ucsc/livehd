/******************************************************************************
 * balanced_cut_dfs.h
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
#include <tuple>
#include <vector>

#include "common/configuration.h"
#include "common/definitions.h"
#include "data_structure/mutable_graph.h"
#include "tools/random_functions.h"

template <class GraphPtr>
class balanced_cut_dfs {
 public:
    static constexpr bool debug = false;

    balanced_cut_dfs() = delete;
    balanced_cut_dfs(GraphPtr original_graph_ptr,
                     mutableGraphPtr graph_ptr, EdgeWeight mincut_value)
        : original_graph(original_graph_ptr),
          G(graph_ptr),
          mincut(mincut_value),
          status(graph_ptr->n(), UNDISCOVERED),
          subtree_weight(graph_ptr->n(), UNDEFINED_EDGE),
          parent(graph_ptr->n(), UNDEFINED_NODE),
          outgoing_cycles(graph_ptr->n()) { }

    std::tuple<NodeID, EdgeID, NodeID, EdgeID, bool> runDFS() {
        start_vertex = random_functions::nextInt(0, G->n() - 1);
        optimize_conductance =
            configuration::getConfig()->find_lowest_conductance;
        if (optimize_conductance) {
            sum_of_weights = original_graph->sumOfEdgeWeights();
        }
        processVertex(start_vertex);

        // Return cut edge(s) for most balanced mincut.
        // If most balanced mincut is on a cycle, we return both edges.
        // If it is a tree edge, we return it twice (simpler handling outside)
        if (best_in_cycle) {
            NodeID best_n2_r = G->getEdgeTarget(best_n2, best_e2);
            EdgeID best_e2_r = G->getReverseEdge(best_n2, best_e2);
            return std::make_tuple(best_n, best_e, best_n2_r, best_e2_r, true);
        } else {
            return std::make_tuple(best_n, best_e, best_n, best_e, false);
        }
    }

 private:
    void processVertex(NodeID n) {
        status[n] = ACTIVE;

        if (G->get_first_invalid_edge(n) == 1 && n != start_vertex) {
            // leaf
            subtree_weight[n] = findVertexWeight(n);
            status[n] = FINISHED;
            return;
        }

        EdgeWeight children_weight = 0;
        for (EdgeID e : G->edges_of(n)) {
            NodeID t = G->getEdgeTarget(n, e);
            switch (status[t]) {
            case UNDISCOVERED:
                parent[t] = n;
                processVertex(t);
                if (lighterBlock(subtree_weight[t]) > best_weight
                    && G->getEdgeWeight(n, e) == mincut) {
                    best_in_cycle = false;
                    best_n = n;
                    best_e = e;
                    best_weight = lighterBlock(subtree_weight[t]);
                }
                children_weight += subtree_weight[t];
                break;

            case ACTIVE:
            case CYCLE:
                if (parent[n] != t) {
                    // found a cycle, mark it, process it when we are back at t
                    status[t] = CYCLE;
                    outgoing_cycles[t].emplace_back(n);
                }
                break;
            default:
                break;
                // do nothing
            }
        }

        if (status[n] == CYCLE) {
            checkForCycles(n);
        }

        subtree_weight[n] = children_weight + findVertexWeight(n);

        status[n] = FINISHED;
    }

    EdgeWeight findVertexWeight(NodeID n) {
        if (optimize_conductance) {
            EdgeWeight vertex_weight = 0;
            for (const auto& v : G->containedVertices(n)) {
                vertex_weight += original_graph->getWeightedNodeDegree(v);
            }
            return vertex_weight;
        } else {
            return G->containedVertices(n).size();
        }
    }

    EdgeWeight totalWeight() {
        if (optimize_conductance) {
            return sum_of_weights;
        } else {
            return G->getOriginalNodes();
        }
    }

    EdgeWeight lighterBlock(EdgeWeight w) {
        return std::min(w, totalWeight() - w);
    }

    void investigateCycle(NodeID t, NodeID start) {
        std::vector<NodeID> cycle_vertices;
        std::vector<EdgeWeight> cycle_weight;
        cycle_vertices.emplace_back(t);
        cycle_weight.emplace_back(subtree_weight[t]);

        while (cycle_vertices.back() != start) {
            NodeID par = parent[cycle_vertices.back()];
            cycle_weight.emplace_back(
                subtree_weight[par] - subtree_weight[cycle_vertices.back()]);
            cycle_vertices.emplace_back(par);
        }

        cycle_weight.back() = totalWeight()
                              - subtree_weight[cycle_vertices[
                                                   cycle_vertices.size() - 2]];

        NodeID length = cycle_weight.size();

        NodeID back = length;
        NodeID front = 1;
        EdgeWeight in_weight = cycle_weight[0];

        while (back <= 2 * length) {
            if (lighterBlock(in_weight) >= best_weight) {
                best_weight = lighterBlock(in_weight);
                best_in_cycle = true;

                // front edge
                best_n = cycle_vertices[(front - 1) % length];
                for (EdgeID e : G->edges_of(best_n)) {
                    if (G->getEdgeTarget(best_n, e)
                        == cycle_vertices[front % length]) {
                        best_e = e;
                        break;
                    }
                }

                // back edge
                best_n2 = cycle_vertices[(back - 1) % length];
                for (EdgeID e : G->edges_of(best_n2)) {
                    if (G->getEdgeTarget(best_n2, e)
                        == cycle_vertices[back % length]) {
                        best_e2 = e;
                        break;
                    }
                }
            }

            if (in_weight * 2 >= totalWeight()) {
                in_weight -= cycle_weight[back % length];
                back++;
            } else {
                in_weight += cycle_weight[front % length];
                front++;
            }
        }
    }

    void checkForCycles(NodeID n) {
        for (NodeID t : outgoing_cycles[n]) {
            investigateCycle(t, n);
        }
    }

    GraphPtr original_graph;
    mutableGraphPtr G;
    EdgeWeight mincut;
    NodeID start_vertex;
    std::vector<DFSVertexStatus> status;
    std::vector<EdgeWeight> subtree_weight;
    std::vector<NodeID> parent;
    std::vector<std::vector<NodeID> > outgoing_cycles;
    EdgeWeight sum_of_weights = 0;

    bool best_in_cycle = false;
    bool optimize_conductance = false;
    NodeID best_n = 0;
    EdgeID best_e = 0;

    NodeID best_n2 = 0;
    EdgeID best_e2 = 0;

    EdgeWeight best_weight = 0;
};
