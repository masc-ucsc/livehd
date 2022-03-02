/******************************************************************************
 * cactus_path.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2020 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <queue>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "common/definitions.h"
#include "data_structure/mutable_graph.h"

class cactus_path {
 public:
    cactus_path() { }
    ~cactus_path() { }

    static std::unordered_set<NodeID> findPath(mutableGraphPtr cactus, NodeID s,
                                               NodeID t, EdgeWeight mincut) {
        // BFS from both vertices
        std::vector<std::tuple<NodeID, NodeID, bool> > parents(
            cactus->n(), std::make_tuple(UNDEFINED_NODE, UNDEFINED_NODE, true));
        std::queue<NodeID> nodes;
        nodes.push(s);
        nodes.push(t);
        parents[s] = std::make_tuple(s, s, true);
        parents[t] = std::make_tuple(t, t, true);

        while (!nodes.empty()) {
            NodeID n = nodes.front();
            nodes.pop();
            NodeID myRoot = std::get<1>(parents[n]);
            for (EdgeID e : cactus->edges_of(n)) {
                auto [tgt, wgt] = cactus->getEdge(n, e);
                NodeID tgtRoot = std::get<1>(parents[tgt]);
                if (tgtRoot != UNDEFINED_NODE && tgtRoot != myRoot) {
                    // found path!
                    std::unordered_set<NodeID> pathST;
                    pathST.insert(tgt);
                    pathST.insert(n);
                    NodeID nToS = myRoot == s ? n : tgt;
                    bool onTree = std::get<2>(parents[n]);
                    while (nToS != s) {
                        nToS = std::get<0>(parents[nToS]);
                        if (onTree || std::get<2>(parents[nToS]))
                            pathST.insert(nToS);
                        onTree = std::get<2>(parents[nToS]);
                    }
                    NodeID tgtToT = myRoot == s ? tgt : n;
                    onTree = std::get<2>(parents[tgt]);
                    while (tgtToT != t) {
                        tgtToT = std::get<0>(parents[tgtToT]);
                        if (onTree || std::get<2>(parents[tgtToT]))
                            pathST.insert(tgtToT);
                        onTree = std::get<2>(parents[tgtToT]);
                    }
                    return pathST;
                }

                if (tgtRoot == UNDEFINED_NODE) {
                    parents[tgt] = std::make_tuple(n, myRoot, wgt == mincut);
                    nodes.push(tgt);
                }
            }
        }

        return std::unordered_set<NodeID> { };
    }
};
