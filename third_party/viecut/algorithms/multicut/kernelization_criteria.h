/******************************************************************************
 * kernelization_criteria.h
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
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "algorithms/global_mincut/noi_minimum_cut.h"
#include "algorithms/misc/equal_neighborhood.h"
#include "algorithms/misc/find_articulation_points.h"
#include "algorithms/misc/find_bridges.h"
#include "algorithms/misc/maximal_clique.h"
#include "algorithms/multicut/graph_contraction.h"
#include "algorithms/multicut/maximum_flow.h"
#include "algorithms/multicut/multicut_problem.h"
#include "data_structure/union_find.h"

class kernelization_criteria {
 public:
    static constexpr bool debug = false;

    explicit kernelization_criteria(std::vector<NodeID> original_terminals)
        : original_terminals(original_terminals),
          mf(original_terminals) { }

    ~kernelization_criteria() { }

    // performs kernelization.
    // if we find a bridge that separates terminal set, return it to branch on
    std::optional<std::pair<NodeID, EdgeID> > kernelization(
        problemPointer problem,
        size_t global_upper_bound, bool parallel) {
        NodeID num_vtcs = problem->graph->n();
        std::vector<bool> active_c(problem->graph->getOriginalNodes(), true);
        std::vector<bool> active_n(problem->graph->getOriginalNodes(), false);
        do {
            num_vtcs = problem->graph->n();
            auto uf_lowdegree = lowDegreeContraction(problem);
            contractIfImproved(&uf_lowdegree, problem, "lowdeg", &active_n);

            union_find uf_high = highDegreeContraction(problem, active_c);
            contractIfImproved(&uf_high, problem, "high_degree", &active_n);

            auto uf_tri = triangleDetection(problem, active_c);
            contractIfImproved(&uf_tri, problem, "triangle", &active_n);

            std::vector<EdgeWeight> flow_values;
            EdgeWeight sum = 0;
            for (size_t i = 0; i < problem->terminals.size(); ++i) {
                NodeID orig_id = problem->terminals[i].original_id;
                NodeID term_id = problem->mapped(
                    original_terminals[orig_id]);
                NodeID pos = problem->graph->getCurrentPosition(term_id);
                EdgeWeight deg = problem->graph->getWeightedNodeDegree(pos);
                flow_values.emplace_back(deg);
                sum += deg;
            }

            std::sort(flow_values.begin(), flow_values.end());

            FlowType contr_flow = 0;

            if (flow_values.size() >= 2)
                contr_flow = sum - flow_values[flow_values.size() - 1]
                             - flow_values[flow_values.size() - 2];
            noi_minimum_cut<mutableGraphPtr> noi;
            EdgeWeight noi_limit = global_upper_bound
                                   - problem->deleted_weight
                                   - ((contr_flow + 4 - 1) / 4); // tlx::div_ceil(contr_flow, 4);
            auto uf_noi = noi.modified_capforest(problem->graph, noi_limit);
            contractIfImproved(&uf_noi, problem, "noi", &active_n);

            find_articulation_points find_aps(problem->graph);
            if (find_aps.findAllArticulationPoints()) {
                auto uf = find_aps.terminalsOnBothSides(problem->terminals);
                if (uf.has_value()) {
                    contractIfImproved(
                        &uf.value(), problem, "aps", &active_n);
                }
            }

            equal_neighborhood en;
            union_find uf_en = en.findEqualNeighborhoods(problem, active_c);
            contractIfImproved(&uf_en, problem, "equal_nbrhd", &active_n);

            auto uf_mf = mf.nonTerminalFlow(problem, parallel, active_c);
            contractIfImproved(&uf_mf, problem, "flow", &active_n);

            active_n.swap(active_c);
            std::fill(active_n.begin(), active_n.end(), false);
        } while (problem->graph->n() < num_vtcs);
        return std::nullopt;
    }

 private:
    void contractIfImproved(union_find* uf,
                            problemPointer problem,
                            const std::string& str,
                            std::vector<bool>* active_vertices) {
        std::vector<bool>& active = *active_vertices;

        if (uf->n() < problem->graph->number_of_nodes()) {

            std::vector<std::vector<NodeID> > reverse_mapping(uf->n());
            // code duplicated from contract graph
            // as we need access to vertex sets
            std::vector<NodeID> part(
                problem->graph->number_of_nodes(), UNDEFINED_NODE);
            NodeID current_pid = 0;
            for (NodeID n : problem->graph->nodes()) {
                NodeID part_id = uf->Find(n);
                if (part[part_id] == UNDEFINED_NODE) {
                    part[part_id] = current_pid++;
                }
                reverse_mapping[part[part_id]].push_back(
                    problem->graph->containedVertices(n)[0]);
            }

            for (size_t i = 0; i < reverse_mapping.size(); ++i) {
                if (reverse_mapping[i].size() > 1) {
                    std::unordered_set<NodeID> vtx_to_ctr;
                    for (auto v : reverse_mapping[i]) {
                        vtx_to_ctr.emplace(
                            problem->graph->getCurrentPosition(v));
                    }
                    problem->graph->contractVertexSet(vtx_to_ctr);

                    NodeID c = problem->graph->getCurrentPosition(
                        reverse_mapping[i][0]);

                    for (const NodeID& n :
                         problem->graph->containedVertices(c)) {
                        active[n] = true;
                    }

                    for (const EdgeID& e : problem->graph->edges_of(c)) {
                        NodeID v = problem->graph->getEdgeTarget(c, e);
                        for (const NodeID& n :
                             problem->graph->containedVertices(v)) {
                            active[n] = true;
                        }
                    }
                }
            }

            problem->priority_edge = { UNDEFINED_NODE, UNDEFINED_EDGE };
            graph_contraction::setTerminals(problem, original_terminals);
            graph_contraction::deleteTermEdges(problem, original_terminals);
            if (debug)
                graph_algorithms::checkGraphValidity(problem->graph);
        }
    }

    union_find lowDegreeContraction(problemPointer problem) {
        auto graph = problem->graph;
        union_find uf(graph->number_of_nodes());
        graph_contraction::setTerminals(problem, original_terminals);
        std::vector<bool> terminals(graph->number_of_nodes(), false);

        for (const auto& p : problem->terminals) {
            terminals[p.position] = true;
        }

        for (NodeID n : graph->nodes()) {
            if (terminals[n])
                continue;

            if (graph->getUnweightedNodeDegree(n) == 1) {
                NodeID tgt = graph->getEdgeTarget(n, 0);
                uf.Union(n, tgt);
                if (graph->getUnweightedNodeDegree(tgt) == 3) {
                    // this will become a degree 2 vertex
                    // so we run the degree 2 contraction
                    EdgeID reverse = graph->getReverseEdge(n, 0);
                    EdgeID non_n_1 = 0 + (reverse == 0);
                    EdgeID non_n_2 = 1 + (reverse <= 1);
                    auto [n1, e1] = graph->getEdge(tgt, non_n_1);
                    auto [n2, e2] = graph->getEdge(tgt, non_n_2);
                    // merge with stronger connected neighbour
                    NodeID larger = e1 >= e2 ? n1 : n2;
                    uf.Union(tgt, larger);
                }
                continue;
            }

            if (graph->getUnweightedNodeDegree(n) == 2) {
                auto [n1, e1] = graph->getEdge(n, 0);
                auto [n2, e2] = graph->getEdge(n, 1);
                // merge with stronger connected neighbour
                NodeID larger = e1 >= e2 ? n1 : n2;
                uf.Union(n, larger);
            }
        }
        return uf;
    }

    union_find highDegreeContraction(problemPointer problem,
                                     const std::vector<bool>& active) {
        graph_contraction::setTerminals(problem, original_terminals);
        union_find uf(problem->graph->number_of_nodes());
        std::vector<bool> terminals(problem->graph->number_of_nodes(), false);

        for (const auto& p : problem->terminals) {
            terminals[p.position] = true;
        }

        auto graph = problem->graph;

        for (NodeID n : graph->nodes()) {
            NodeID in = graph->containedVertices(n)[0];
            if (!active[in]) {
                continue;
            }

            if (terminals[n] || uf.Find(n) != n)
                continue;

            EdgeWeight nonterminal_weight = 0;
            EdgeWeight maxwgt = 0;
            NodeID maxterm = 0;

            EdgeWeight secondwgt = 0;

            EdgeWeight node_weight = graph->getWeightedNodeDegree(n);
            bool already_contracted = false;
            for (EdgeID e : graph->edges_of(n)) {
                NodeID tgt = graph->getEdgeTarget(n, e);
                EdgeWeight wgt = graph->getEdgeWeight(n, e);

                if (terminals[tgt]) {
                    if (wgt > maxwgt) {
                        secondwgt = maxwgt;
                        maxwgt = wgt;
                        maxterm = tgt;
                    } else {
                        if (wgt > secondwgt) {
                            secondwgt = wgt;
                        }
                    }
                }

                if (wgt * 2 >= node_weight) {
                    uf.Union(n, tgt);
                    already_contracted = true;
                    break;
                }

                if (!terminals[tgt]) {
                    nonterminal_weight += wgt;
                }
            }

            if (!already_contracted) {
                if (maxwgt > nonterminal_weight + secondwgt) {
                    uf.Union(n, maxterm);
                }
            }
        }

        return uf;
    }

    union_find triangleDetection(problemPointer problem,
                                 const std::vector<bool>& active) {
        auto graph = problem->graph;

        graph_contraction::setTerminals(problem, original_terminals);
        union_find uf(problem->graph->number_of_nodes());
        std::vector<bool> terminals(problem->graph->number_of_nodes(), false);

        for (const auto& p : problem->terminals) {
            terminals[p.position] = true;
        }

        std::vector<EdgeID> marked(problem->graph->n(), UNDEFINED_EDGE);
        std::vector<bool> done(problem->graph->n(), false);

        for (NodeID v1 : graph->nodes()) {
            NodeID in = graph->containedVertices(v1)[0];
            if (!active[in]) {
                continue;
            }

            EdgeWeight maxwgt = 0;
            if (!done[v1] && !terminals[v1]) {
                for (EdgeID e : graph->edges_of(v1)) {
                    NodeID tgt = graph->getEdgeTarget(v1, e);
                    EdgeWeight wgt = graph->getEdgeWeight(v1, e);
                    if (tgt > v1) {
                        marked[tgt] = e;
                    }

                    if (wgt > maxwgt) {
                        maxwgt = wgt;
                    }
                }

                // no triangle can be heavier than half of v1's degree
                if (maxwgt * 4 < graph->getWeightedNodeDegree(v1)) {
                    for (EdgeID e : graph->edges_of(v1)) {
                        NodeID tgt = graph->getEdgeTarget(v1, e);
                        marked[tgt] = UNDEFINED_EDGE;
                    }
                    continue;
                }

                for (EdgeID e1 : graph->edges_of(v1)) {
                    NodeID v2 = graph->getEdgeTarget(v1, e1);
                    if (v2 > v1 && !terminals[v2] && !done[v2]) {
                        for (EdgeID e2 : graph->edges_of(v2)) {
                            NodeID v3 = graph->getEdgeTarget(v2, e2);
                            if (v3 > v2 && marked[v3] != UNDEFINED_EDGE
                                && !terminals[v3] && !done[v3]) {
                                EdgeID e3 = marked[v3];
                                if (graph->getEdgeTarget(v1, e3) == v3) {
                                    EdgeWeight weight_v1 =
                                        graph->getWeightedNodeDegree(v1);
                                    EdgeWeight weight_v2 =
                                        graph->getWeightedNodeDegree(v2);
                                    EdgeWeight weight_v3 =
                                        graph->getWeightedNodeDegree(v3);

                                    EdgeWeight weight_e1 =
                                        graph->getEdgeWeight(v1, e1);
                                    EdgeWeight weight_e2 =
                                        graph->getEdgeWeight(v2, e2);
                                    EdgeWeight weight_e3 =
                                        graph->getEdgeWeight(v1, e3);

                                    bool heavy_v1 =
                                        ((weight_v1 <=
                                          (weight_e1 + weight_e3) * 2)
                                         && (uf.Find(v1) == v1));
                                    bool heavy_v2 =
                                        ((weight_v2 <=
                                          (weight_e1 + weight_e2) * 2)
                                         && (uf.Find(v2) == v2));
                                    bool heavy_v3 =
                                        ((weight_v3 <=
                                          (weight_e2 + weight_e3) * 2)
                                         && (uf.Find(v3) == v3));

                                    if (heavy_v1 && heavy_v2)
                                        uf.Union(v1, v2);

                                    if (heavy_v1 && heavy_v3)
                                        uf.Union(v1, v3);

                                    if (heavy_v2 && heavy_v3)
                                        uf.Union(v2, v3);
                                } else {
                                    exit(1);
                                }
                            }
                        }
                        marked[v2] = UNDEFINED_EDGE;
                    }
                }
            }
        }
        return uf;
    }

    std::vector<NodeID> original_terminals;
    maximum_flow mf;
    constexpr static bool logs = false;
};
