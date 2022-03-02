/******************************************************************************
 * graph_contraction.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "algorithms/misc/graph_algorithms.h"
#include "algorithms/multicut/multicut_problem.h"

class graph_contraction {
 public:
    static constexpr bool debug = false;
    graph_contraction() { }
    ~graph_contraction() { }

    static void contractIsolatingBlocks(
        problemPointer mcp,
        const std::vector<std::vector<NodeID> >& isolating_blocks) {
        bool contracted = false;
        std::vector<std::vector<NodeID> > contraction_base;
        std::vector<bool> already_contracted;
        for (const std::vector<NodeID>& isolating_block : isolating_blocks) {
            contraction_base.emplace_back();
            if (isolating_block.size() > 1 && !contracted) {
                already_contracted.resize(mcp->graph->n(), false);
                contracted = true;
            }

            if (isolating_block.size() > 1) {
                for (const NodeID& n : isolating_block) {
                    if (!already_contracted[n]) {
                        contraction_base.back().emplace_back(
                            mcp->graph->containedVertices(n)[0]);
                        already_contracted[n] = true;
                    }
                }
            }
        }

        if (contracted) {
            for (size_t i = 0; i < contraction_base.size(); ++i) {
                if (contraction_base[i].size() > 1) {
                    std::unordered_set<NodeID> vtx_to_ctr;
                    for (auto v : contraction_base[i]) {
                        NodeID n = mcp->graph->getCurrentPosition(v);
                        vtx_to_ctr.emplace(n);
                    }
                    mcp->graph->contractVertexSet(vtx_to_ctr);
                }
            }

            if (debug) {
                graph_algorithms::checkGraphValidity(mcp->graph);
            }
        }
    }

    static void deleteTermEdges(
        problemPointer mcp,
        const std::vector<NodeID>& original_terminals) {
        // Setting edge weights to 0 and adding their weight
        // to the deleted weight. As edges connecting terminals are always
        // in minimum multicut those can be safely added to the cut.
        EdgeWeight del_weight = 0;
        setTerminals(mcp, original_terminals);
        std::unordered_set<NodeID> terminal_set;
        for (const auto& t : mcp->terminals) {
            terminal_set.emplace(t.position);
        }

        for (const auto& termpair : mcp->terminals) {
            NodeID term = termpair.position;
            if (mcp->graph->get_first_invalid_edge(term) > 0) {
                NodeID e = mcp->graph->get_first_invalid_edge(term) - 1;
                do {
                    NodeID tgt = mcp->graph->getEdgeTarget(term, e);
                    if (terminal_set.count(tgt) > 0) {
                        FlowType wgt = static_cast<FlowType>(
                            mcp->graph->getEdgeWeight(term, e));
                        del_weight += wgt;
                        mcp->graph->deleteEdge(term, e);

                        for (auto& t : mcp->terminals) {
                            if (t.position != term && t.position != tgt) {
                                t.invalid_flow = true;
                            }
                        }
                    }
                } while (e-- > 0);
            }
        }

        mcp->deleted_weight += del_weight;
        if (del_weight > 0) {
            mcp->priority_edge = { UNDEFINED_NODE, UNDEFINED_EDGE };
        }
        setTerminals(mcp, original_terminals);
    }

    static void setTerminals(problemPointer problem,
                             const std::vector<NodeID>& original_terminals) {
        std::unordered_map<NodeID, bool> invalid_flows;

        for (const auto& t : problem->terminals) {
            invalid_flows[t.position] = t.invalid_flow;
        }

        problem->terminals.clear();

        for (size_t i = 0; i < original_terminals.size(); ++i) {
            NodeID o = problem->mapped(original_terminals[i]);
            NodeID node = problem->graph->getCurrentPosition(o);
            if ((problem->graph->getUnweightedNodeDegree(node) > 0)
                && (node < problem->graph->number_of_nodes())) {
                NodeID n = invalid_flows.count(node);
                bool flow_invalid = n == 0 || invalid_flows[node];
                problem->terminals.emplace_back(node, i, flow_invalid);
            }
        }
    }
};
