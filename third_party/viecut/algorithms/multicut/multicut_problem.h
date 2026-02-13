/******************************************************************************
 * multiterminal_cut.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "data_structure/mutable_graph.h"
#include "io/graph_io.h"

struct terminal {
    terminal() { }

    terminal(NodeID position_id, NodeID original_id_value)
        : position(position_id),
          original_id(original_id_value),
          invalid_flow(true) { }

    terminal(NodeID position_id, NodeID original_id_value,
             bool invalid_flow_value)
        : position(position_id),
          original_id(original_id_value),
          invalid_flow(invalid_flow_value) { }

    NodeID position;
    NodeID original_id;
    bool   invalid_flow;
};

struct multicut_problem {
    multicut_problem() { }

    explicit multicut_problem(mutableGraphPtr G)
        : multicut_problem(G, std::vector<terminal>()) { }

    multicut_problem(mutableGraphPtr G,
                     std::vector<terminal> term)
        : multicut_problem(G,
                           term,
                           std::vector<
                               std::shared_ptr<std::vector<NodeID> > >(),
                           -1,
                           std::numeric_limits<FlowType>::max(),
                           0,
                           { UNDEFINED_NODE, UNDEFINED_EDGE },
                           std::unordered_set<NodeID>()) { }

    multicut_problem(mutableGraphPtr G,
                     std::vector<terminal> term,
                     std::vector<std::shared_ptr<std::vector<NodeID> > >
                     node_mappings,
                     FlowType lower,
                     FlowType upper,
                     EdgeWeight deleted,
                     std::pair<NodeID, EdgeID> prio,
                     std::unordered_set<NodeID> finished_bp)
        : graph(G),
          terminals(term),
          mappings(node_mappings),
          lower_bound(lower),
          upper_bound(upper),
          deleted_weight(deleted),
          priority_edge(prio),
          finished_blockpairs(finished_bp) { }

    NodeID mapped(NodeID n) const {
        NodeID n_coarse = n;
        for (const auto& map : mappings) {
            n_coarse = (*map)[n_coarse];
        }
        return n_coarse;
    }

    void addFinishedPair(NodeID a, NodeID b, NodeID numOriginalTerminals) {
        if (a == b) {
            exit(1);
        }
        NodeID min = std::min(a, b);
        NodeID max = std::max(a, b);

        NodeID combined = min * numOriginalTerminals + max;
        finished_blockpairs.insert(combined);
    }

    void removeFinishedPair(NodeID a, NodeID b, NodeID numOriginalTerminals) {
        if (a == b) {
            exit(1);
        }
        NodeID min = std::min(a, b);
        NodeID max = std::max(a, b);

        NodeID combined = min * numOriginalTerminals + max;
        finished_blockpairs.extract(combined);
    }

    bool isPairFinished(NodeID a, NodeID b, NodeID numOriginalTerminals) {
        if (a == b) {
            exit(1);
        }
        NodeID min = std::min(a, b);
        NodeID max = std::max(a, b);

        NodeID combined = min * numOriginalTerminals + max;
        return (finished_blockpairs.count(combined) > 0);
    }

    static void writeGraph(problemPointer problem,
                           std::string path) {
        mutableGraphPtr g = std::make_shared<mutable_graph>();

        // bfs around all terminals, print all edges between first 3000 nodes
        std::queue<NodeID> Q;
        std::unordered_set<NodeID> S;
        std::unordered_set<NodeID> terms;
        std::unordered_map<NodeID, NodeID> gMapping;

        for (auto p : problem->terminals) {
            Q.push(p.position);
            S.insert(p.position);
            terms.insert(p.position);
        }

        while (S.size() < UNDEFINED_NODE && !Q.empty()) {
            NodeID f = Q.front();
            Q.pop();
            for (EdgeID e : problem->graph->edges_of(f)) {
                NodeID tgt = problem->graph->getEdgeTarget(f, e);
                if (S.count(tgt) == 0) {
                    S.insert(tgt);
                    Q.push(tgt);
                }
            }
        }

        g->start_construction(S.size());

        size_t gMapIndex = 0;
        for (NodeID n : problem->graph->nodes()) {
            if (S.count(n) > 0) {
                if (gMapping.count(n) == 0) {
                    gMapping.emplace(n, gMapIndex);
                    gMapIndex++;
                }

                if (terms.count(n) > 0) {
                }

                for (EdgeID e : problem->graph->edges_of(n)) {
                    auto [t, w] = problem->graph->getEdge(n, e);
                    if (t > n && S.count(t) > 0) {
                        if (gMapping.count(t) == 0) {
                            gMapping.emplace(t, gMapIndex);
                            gMapIndex++;
                        }

                        g->new_edge_order(gMapping[n], gMapping[t], w);
                    }
                }
            }
        }

        g->finish_construction();

        if (gMapIndex < problem->graph->n()) {
        }

        graph_io::writeGraphWeighted(g->to_graph_access(), path);
    }

    mutableGraphPtr                                     graph;
    std::vector<terminal>                               terminals;
    std::vector<std::shared_ptr<std::vector<NodeID> > > mappings;
    FlowType                                            lower_bound;
    FlowType                                            upper_bound;
    EdgeWeight                                          deleted_weight;
    std::pair<NodeID, EdgeID>                           priority_edge;
    std::unordered_set<NodeID>                          finished_blockpairs;
};
