/******************************************************************************
 * edge_selection.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2019 Alexander Noe <alexander.noe@univie.ac.at>
 */

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "algorithms/multicut/multicut_problem.h"
#include "data_structure/mutable_graph.h"
#include "tools/random_functions.h"

[[maybe_unused]] static std::tuple<NodeID, EdgeID> findHeavyEdge(
    problemPointer problem) {
    EdgeWeight max_wgt = 0;
    NodeID max_id_t = 0;
    EdgeID max_id_e = 0;
    for (const auto& term : problem->terminals) {
        NodeID t = term.position;

        for (EdgeID e : problem->graph->edges_of(t)) {
            EdgeWeight curr_wgt = problem->graph->getEdgeWeight(t, e);
            if (curr_wgt > max_wgt) {
                max_wgt = curr_wgt;
                max_id_t = t;
                max_id_e = e;
            }
        }
    }
    return std::make_tuple(max_id_t, max_id_e);
}

[[maybe_unused]] static std::tuple<NodeID, EdgeID> findVertexWithHighDistance(
    problemPointer problem) {
    EdgeWeight max_wgt = 0;
    NodeID max_id_t = 0;
    EdgeID max_id_e = 0;
    for (const auto& term : problem->terminals) {
        NodeID t = term.position;
        for (EdgeID e : problem->graph->edges_of(t)) {
            NodeID tgt = problem->graph->getEdgeTarget(t, e);
            EdgeWeight curr_wgt = problem->graph->getWeightedNodeDegree(tgt)
                                  - (problem->graph->getEdgeWeight(t, e));
            if (curr_wgt > max_wgt) {
                max_wgt = curr_wgt;
                max_id_t = t;
                max_id_e = e;
            }
        }
    }
    return std::make_tuple(max_id_t, max_id_e);
}

[[maybe_unused]] static std::tuple<NodeID, EdgeID> findHeavyVertex(
    problemPointer problem) {
    EdgeWeight max_wgt = 0;
    NodeID max_id_t = 0;
    EdgeID max_id_e = 0;
    for (const auto& term : problem->terminals) {
        NodeID t = term.position;

        if (problem->graph->get_first_invalid_edge(t) == 1) {
            // return std::make_tuple(t, 0);
        }

        for (EdgeID e : problem->graph->edges_of(t)) {
            NodeID tgt = problem->graph->getEdgeTarget(t, e);
            EdgeWeight curr_wgt = problem->graph->getWeightedNodeDegree(tgt);
            if (curr_wgt > max_wgt) {
                max_wgt = curr_wgt;
                max_id_t = t;
                max_id_e = e;
            }
        }
    }

    return std::make_tuple(max_id_t, max_id_e);
}

[[maybe_unused]] static std::tuple<NodeID, EdgeID> findGlobalHeaviestEdge(
    problemPointer problem) {
    EdgeWeight max_wgt = 0;
    NodeID max_id_t = 0;
    EdgeID max_id_e = 0;
    for (NodeID n : problem->graph->nodes()) {
        for (EdgeID e : problem->graph->edges_of(n)) {
            EdgeWeight curr_wgt = problem->graph->getEdgeWeight(n, e);
            if (curr_wgt > max_wgt) {
                max_wgt = curr_wgt;
                max_id_t = n;
                max_id_e = e;
            }
        }
    }
    return std::make_tuple(max_id_t, max_id_e);
}

[[maybe_unused]] static std::tuple<NodeID, EdgeID> findEdgeWithManyTerminals(
    const problemPointer problem) {
    std::unordered_map<NodeID, uint16_t> terms;
    NodeID max_num = 0;
    NodeID max_ngbr = 0;
    std::unordered_set<NodeID> termset;
    for (const auto& term : problem->terminals) {
        NodeID t = term.position;
        termset.emplace(t);
        for (EdgeID e : problem->graph->edges_of(t)) {
            EdgeWeight wgt = problem->graph->getEdgeWeight(t, e);
            NodeID ngbr = problem->graph->getEdgeTarget(t, e);
            if (terms.find(ngbr) == terms.end()) {
                terms[ngbr] = wgt;
            } else {
                terms[ngbr] += wgt;
            }
            if (terms[ngbr] > max_num) {
                max_ngbr = ngbr;
                max_num = terms[ngbr];
            }
        }
    }

    EdgeWeight max_wgt = 0;
    NodeID max_id_t = 0;
    EdgeID max_id_e = 0;

    for (EdgeID e : problem->graph->edges_of(max_ngbr)) {
        EdgeWeight curr_wgt = problem->graph->getEdgeWeight(max_ngbr, e);
        NodeID ngbr = problem->graph->getEdgeTarget(max_ngbr, e);
        if (curr_wgt > max_wgt && termset.count(ngbr) > 0) {
            max_wgt = curr_wgt;
            max_id_t = ngbr;
            max_id_e = problem->graph->getReverseEdge(max_ngbr, e);
        }
    }
    return std::make_tuple(max_id_t, max_id_e);
}

[[maybe_unused]] static std::tuple<NodeID, EdgeID> findRandomEdge(
    problemPointer problem) {
    std::vector<EdgeWeight> prefixsum(problem->terminals.size() + 1, 0);
    EdgeWeight s = 0;
    for (size_t i = 0; i < prefixsum.size() - 1; ++i) {
        s += problem->graph->getUnweightedNodeDegree(
            problem->terminals[i].position);
        prefixsum[i + 1] = s;
    }

    EdgeID random_edge = random_functions::nextInt(0, s - 1);
    NodeID t = 0;

    while (random_edge >= prefixsum[t + 1]) {
        t++;
    }

    EdgeID e = random_edge - prefixsum[t];

    return std::make_tuple(problem->terminals[t].position, e);
}

auto compare = [](const std::pair<size_t, size_t>& p1,
                  const std::pair<size_t, size_t>& p2) {
                   if (p1.first == p2.first) {
                       return p1.second > p2.second;
                   } else {
                       return p1.first > p2.first;
                   }
               };

[[maybe_unused]] static NodeID mostTerminalNeighbours(
    problemPointer problem) {
    std::vector<std::pair<size_t, size_t> > neighbours(problem->graph->n(),
                                                       { 0, 0 });
    std::pair<size_t, size_t> maxPair = { 0, 0 };
    NodeID maxID = UNDEFINED_NODE;
    for (const auto& t : problem->terminals) {
        NodeID term = t.position;
        for (EdgeID e : problem->graph->edges_of(term)) {
            NodeID neighbour = problem->graph->getEdgeTarget(term, e);
            EdgeWeight edgeweight = problem->graph->getEdgeWeight(term, e);
            auto [num, weight] = neighbours[neighbour];
            neighbours[neighbour] = { num + 1, weight + edgeweight };
            if (compare(neighbours[neighbour], maxPair)) {
                maxPair = neighbours[neighbour];
                maxID = neighbour;
            }
        }
    }

    return maxID;
}

static std::tuple<NodeID, std::vector<size_t> >
findEdgeMultiBranch(problemPointer problem) {
    // NodeID vtx = mostTerminalNeighbours(problem);
    auto [n, e] = findHeavyVertex(problem);
    NodeID vtx = problem->graph->getEdgeTarget(n, e);

    std::vector<std::pair<size_t, EdgeWeight> > neighbouring_terminals;
    EdgeWeight heavy = 0;
    EdgeWeight sumToTerminals = 0;
    for (size_t i = 0; i < problem->terminals.size(); ++i) {
        NodeID p = problem->terminals[i].position;
        for (EdgeID e : problem->graph->edges_of(p)) {
            auto [tgt, wgt] = problem->graph->getEdge(p, e);
            if (tgt == vtx) {
                neighbouring_terminals.emplace_back(p, wgt);
                heavy = std::max(heavy, wgt);
                sumToTerminals += wgt;
                break;
            }
        }
    }

    std::vector<size_t> possible_terminals;
    EdgeWeight nodeDegree = problem->graph->getWeightedNodeDegree(vtx);
    EdgeWeight nonTerminalWeight = nodeDegree - sumToTerminals;

    // if 'vtx' is not connected to all terminals and non-terminal neighbors
    // are heavier than heaviest edge to terminal neighbour
    // we also need to include case in which 'vtx' is in neither neighbors
    // block
    if (neighbouring_terminals.size() < problem->terminals.size()
        && nonTerminalWeight > heavy) {
        neighbouring_terminals.emplace_back(UNDEFINED_NODE, nonTerminalWeight);
    }

    std::sort(neighbouring_terminals.begin(), neighbouring_terminals.end(),
              [](const auto& n1, const auto& n2) {
                  return n1.second > n2.second;
              });

    bool inexact = configuration::getConfig()->inexact;
    size_t branching_f = configuration::getConfig()->maximumBranchingFactor;

    for (const auto& nt : neighbouring_terminals) {
        // only when weight to this terminal + nonterminal is heavier than
        // heaviest edge to terminal this could be better than that.
        // second part is to make sure that if nonTerminalWeight is zero, at
        // least one value thats maximum gets returned
        if ((nonTerminalWeight + nt.second > heavy || heavy == nt.second) &&
            (!inexact || branching_f > possible_terminals.size())) {
            possible_terminals.emplace_back(nt.first);
        }
    }

    return std::make_pair(vtx, possible_terminals);
}

static std::tuple<NodeID, EdgeID> findEdgeSingleBranch(
    problemPointer problem) {
    std::string edge_selection = configuration::getConfig()->edge_selection;
    std::pair<NodeID, EdgeID> undefined = { UNDEFINED_NODE, UNDEFINED_EDGE };

    if (problem->priority_edge != undefined) {
        auto p = problem->priority_edge;
        problem->priority_edge = undefined;
        return p;
    }

    if (edge_selection == "random")
        return findRandomEdge(problem);

    if (edge_selection == "heavy")
        return findHeavyEdge(problem);

    if (edge_selection == "heavy_global")
        return findGlobalHeaviestEdge(problem);

    if (edge_selection == "connection")
        return findEdgeWithManyTerminals(problem);

    if (edge_selection == "distance")
        return findVertexWithHighDistance(problem);

    if (edge_selection == "heavy_vertex")
        return findHeavyVertex(problem);

    return findHeavyVertex(problem);
}
