/******************************************************************************
 * find_articulation_points.h
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
#include "data_structure/union_find.h"

class find_articulation_points {
 public:
    explicit find_articulation_points(mutableGraphPtr G)
        : G(G),
          visited(G->n(), false),
          discovered(G->n(), UNDEFINED_NODE),
          parent(G->n(), UNDEFINED_NODE),
          lowest(G->n(), UNDEFINED_NODE),
          step(0) { }

    bool findAllArticulationPoints() {
        for (NodeID vtx : G->nodes()) {
            if (!visited[vtx]) {
                findAllArticulationPointsInCC(vtx);
            }
        }
        return articulation_points.size() > 0;
    }

    std::optional<union_find> terminalsOnBothSides(
        const std::vector<terminal>& terminals) {
        union_find uf(G->n());
        bool return_uf = false;
        for (const auto& n : articulation_points) {
            size_t sum = 0;
            for (const auto& t : terminals) {
                NodeID tpos = t.position;
                bool on_right = false;
                while (true) {
                    if (tpos == n) {
                        on_right = true;
                        break;
                    }

                    if (parent[tpos] == n) {
                        on_right = (lowest[tpos] >= discovered[n]);
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

                std::vector<bool> contracted(G->n(), false);

                std::queue<NodeID> q;
                contracted[n] = true;
                for (EdgeID e : G->edges_of(n)) {
                    NodeID t = G->getEdgeTarget(n, e);

                    bool on_right = false;
                    while (true) {
                        if (t == n) {
                            on_right = true;
                            break;
                        }

                        if (parent[t] == n) {
                            on_right = (lowest[t] >= discovered[n]);
                            break;
                        }

                        if (parent[t] == t) {
                            on_right = false;
                            break;
                        }

                        t = parent[t];
                    }

                    if (on_right != invert_side) {
                        contracted[t] = true;
                        q.push(t);
                        uf.Union(n, t);
                    }
                }

                while (!q.empty()) {
                    NodeID v = q.front();
                    q.pop();
                    for (EdgeID a : G->edges_of(v)) {
                        NodeID tgt = G->getEdgeTarget(v, a);
                        if (!contracted[tgt]) {
                            uf.Union(v, tgt);
                            contracted[tgt] = true;
                            q.push(tgt);
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
            return std::nullopt;
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
                    if (parent[b] != b && lowest[t] >= discovered[b]) {
                        articulation_points.emplace_back(b);
                    }
                } else {
                    if (t != parent[b]) {
                        lowest[b] = std::min(lowest[b], discovered[t]);
                    }
                }
            }
        }
    }

    void findAllArticulationPointsInCC(NodeID vtx) {
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
        // disregard if vtx is an articulation point, as that would require a
        // different contraction mechanism
    }

    mutableGraphPtr G;
    std::vector<bool> visited;
    std::vector<NodeID> discovered;
    std::vector<NodeID> parent;
    std::vector<NodeID> lowest;

    std::stack<NodeID> stack;
    std::vector<NodeID> articulation_points;
    size_t step;
};
