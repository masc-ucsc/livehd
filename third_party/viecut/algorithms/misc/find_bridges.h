/******************************************************************************
 * find_bridges.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <memory>
#include <queue>
#include <stack>
#include <utility>
#include <variant>
#include <vector>

#include "algorithms/multicut/multicut_problem.h"
#include "data_structure/mutable_graph.h"

class find_bridges {
 public:
    explicit find_bridges(mutableGraphPtr G)
        : G(G),
          visited(G->n(), false),
          discovered(G->n(), UNDEFINED_NODE),
          parent(G->n(), UNDEFINED_NODE),
          lowest(G->n(), UNDEFINED_NODE),
          step(0) { }

    bool findAllBridges() {
        for (NodeID vtx : G->nodes()) {
            if (!visited[vtx]) {
                findAllBridgesInCC(vtx);
            }
        }
        return bridges.size() > 0;
    }

    std::variant<union_find, std::pair<NodeID, EdgeID> > terminalsOnBothSides(
        const std::vector<terminal>& terminals) {
        union_find uf(G->n());
        bool return_uf = false;
        for (const auto& [n, e] : bridges) {
            size_t sum = 0;
            for (const auto& t : terminals) {
                NodeID b_tgt = G->getEdgeTarget(n, e);
                NodeID tpos = t.position;
                bool on_right = false;
                while (true) {
                    if (tpos == b_tgt) {
                        on_right = true;
                        break;
                    }

                    if (parent[tpos] == tpos) {
                        on_right = false;
                        break;
                    }

                    tpos = parent[tpos];
                }
                sum += on_right;
            }

            if (sum == 0 || sum == terminals.size()) {
                // all terminals on one side, contract the other
                return_uf = true;
                bool invert_side = (sum == terminals.size()) ? true : false;
                NodeID ctr_n = n;
                EdgeID ctr_e = e;
                if (invert_side) {
                    ctr_n = G->getEdgeTarget(n, e);
                    ctr_e = G->getReverseEdge(n, e);
                }
                std::vector<bool> contracted(G->n(), false);
                NodeID t = G->getEdgeTarget(ctr_n, ctr_e);
                uf.Union(ctr_n, t);
                contracted[ctr_n] = true;
                contracted[t] = true;
                std::queue<NodeID> q;
                q.push(t);

                while (!q.empty()) {
                    NodeID v = q.front();
                    q.pop();
                    for (EdgeID a : G->edges_of(v)) {
                        NodeID tgt = G->getEdgeTarget(v, a);
                        if (!contracted[tgt]) {
                            uf.Union(v, tgt);
                            contracted[tgt] = true;
                        }
                    }
                }

                for (auto term : terminals) {
                    if (contracted[term.position] && term.position != n) {
                        exit(1);
                    }
                }
            }
        }

        if (return_uf) {
            return uf;
        } else {
            return bridges[0];
        }
    }

 private:
    void backtrackTo(std::vector<NodeID>* path, NodeID n) {
        while (path->size() > 0 && path->back() != parent[n]) {
            // explicit backtracking to avoid recursion
            NodeID b = path->back();
            path->pop_back();
            for (EdgeID e : G->edges_of(b)) {
                NodeID t = G->getEdgeTarget(b, e);
                if (parent[t] == b) {
                    lowest[b] = std::min(lowest[b], lowest[t]);
                    if (lowest[t] > discovered[b]) {
                        bridges.emplace_back(b, e);
                    }
                } else {
                    if (t != parent[b]) {
                        lowest[b] = std::min(lowest[b], discovered[t]);
                    }
                }
            }
        }
    }

    void findAllBridgesInCC(NodeID vtx) {
        std::vector<NodeID> path;
        parent[vtx] = vtx;
        stack.push(vtx);
        while (!stack.empty()) {
            NodeID n = stack.top();
            stack.pop();
            if (visited[n])
                continue;

            backtrackTo(&path, n);

            path.emplace_back(n);
            // DFS, check whether alternative path exists when backtracking
            visited[n] = true;
            step++;
            discovered[n] = step;
            lowest[n] = step;
            for (EdgeID e : G->edges_of(n)) {
                NodeID t = G->getEdgeTarget(n, e);
                if (!visited[t]) {
                    parent[t] = n;
                    stack.push(t);
                }
            }
        }

        backtrackTo(&path, vtx);
        for (EdgeID e : G->edges_of(vtx)) {
            NodeID t = G->getEdgeTarget(vtx, e);
            if (parent[t] == vtx) {
                if (lowest[t] > discovered[vtx]) {
                    bridges.emplace_back(vtx, e);
                }
            }
        }
    }

    mutableGraphPtr G;
    std::vector<bool> visited;
    std::vector<NodeID> discovered;
    std::vector<NodeID> parent;
    std::vector<NodeID> lowest;

    std::stack<NodeID> stack;
    std::vector<std::pair<NodeID, EdgeID> > bridges;
    size_t step;
};
