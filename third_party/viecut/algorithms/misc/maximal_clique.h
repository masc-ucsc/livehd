/******************************************************************************
 * maximal_clique.h
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
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "algorithms/misc/equal_neighborhood.h"
#include "algorithms/multicut/multicut_problem.h"
#include "data_structure/mutable_graph.h"

class maximal_clique {
 public:
    maximal_clique() { }

    std::vector<std::vector<NodeID> > findCliques(
        mutableGraphPtr graph) {
        for (NodeID n : graph->nodes()) {
            std::vector<std::pair<NodeID, EdgeWeight> > P;
            std::vector<NodeID> R = { n };
            std::vector<std::pair<NodeID, EdgeWeight> > X;

            size_t external = 0;
            for (EdgeID e : graph->edges_of(n)) {
                auto [t, w] = graph->getEdge(n, e);
                if (t > n) {
                    P.emplace_back(graph->getEdge(n, e));
                } else {
                    X.emplace_back(graph->getEdge(n, e));
                    external += w;
                }
            }

            std::sort(P.begin(), P.end(), pairs);
            std::sort(X.begin(), X.end(), pairs);

            bronKerbosch(graph, &P, &R, &X, 0, external, UNDEFINED_EDGE);
        }
        return cliques;
    }

    std::tuple<std::vector<std::pair<NodeID, EdgeWeight> >,
               std::vector<std::pair<NodeID, EdgeWeight> >, size_t, EdgeWeight>
    neighborhoodIntersection(std::vector<std::pair<NodeID, EdgeWeight> >* N,
                             std::vector<std::pair<NodeID, EdgeWeight> >* P,
                             std::vector<NodeID>* R,
                             std::vector<std::pair<NodeID, EdgeWeight> >* X,
                             size_t external_weight,
                             EdgeWeight lightest) {
        EdgeWeight new_lightest = lightest;
        size_t n_id = 0;
        size_t p_id = 0;
        size_t r_id = 0;
        size_t x_id = 0;
        std::vector<std::pair<NodeID, EdgeWeight> > intersect_p;
        std::vector<std::pair<NodeID, EdgeWeight> > intersect_x;
        while (n_id < N->size()) {
            NodeID n = (*N)[n_id].first;
            NodeID p;
            if (p_id == P->size()) {
                p = UNDEFINED_NODE;
            } else {
                p = (*P)[p_id].first;
            }
            NodeID x;
            if (x_id == X->size()) {
                x = UNDEFINED_NODE;
            } else {
                x = (*X)[x_id].first;
            }

            if (n <= p && n <= x) {
                ++n_id;
            }

            if (n < p && n < x) {
                while (r_id < R->size() && (*R)[r_id] < n) {
                    ++r_id;
                }

                if (r_id >= R->size() || (*R)[r_id] != n) {
                    external_weight += (*N)[n_id - 1].second;
                }
                continue;
            }

            if (n == x) {
                auto new_w = (*N)[n_id - 1].second + (*X)[x_id].second;
                intersect_x.emplace_back(n, new_w);
                ++x_id;
            }

            if (n > x) {
                ++x_id;
            }

            if (n == p) {
                auto new_w = (*N)[n_id - 1].second + (*P)[p_id].second;
                intersect_p.emplace_back(n, new_w);
                new_lightest = std::min((*N)[n_id - 1].second, new_lightest);
                ++p_id;
            }

            if (n > p) {
                ++p_id;
            }
        }

        return std::make_tuple(intersect_p, intersect_x,
                               external_weight, new_lightest);
    }

    bool bronKerbosch(mutableGraphPtr graph,
                      std::vector<std::pair<NodeID, EdgeWeight> >* P,
                      std::vector<NodeID>* R,
                      std::vector<std::pair<NodeID, EdgeWeight> >* X,
                      size_t depth,
                      EdgeWeight ex_weight,
                      EdgeWeight lightest) {
        if (ex_weight > (lightest * 2 * (P->size() + R->size() - 1))) {
            return false;
        }

        if (P->size() == 0) {
            if (X->size() == 0) {
                cliques.emplace_back(*R);
            }
            return true;
        }

        NodeID n = (*P)[0].first;
        std::vector<std::pair<NodeID, EdgeWeight> > neighborhood;
        for (EdgeID e : graph->edges_of(n)) {
            neighborhood.emplace_back(graph->getEdge(n, e));
        }
        std::sort(neighborhood.begin(), neighborhood.end(), pairs);
        size_t n_id = 0;
        size_t p_id = 0;
        std::vector<std::pair<NodeID, EdgeWeight> > intersect;
        while (p_id < P->size()) {
            if (n_id == neighborhood.size() ||
                neighborhood[n_id].first > (*P)[p_id].first) {
                intersect.emplace_back((*P)[p_id]);
                ++p_id;
                continue;
            }

            if (neighborhood[n_id].first < (*P)[p_id].first) {
                ++n_id;
                continue;
            }

            n_id++;
            p_id++;
        }

        for (size_t i = 0; i < intersect.size(); ++i) {
            n = intersect[i].first;

            neighborhood.clear();
            for (EdgeID e : graph->edges_of(n)) {
                neighborhood.emplace_back(graph->getEdge(n, e));
            }
            std::sort(neighborhood.begin(), neighborhood.end(), pairs);

            auto [nextP, nextX, current_ex_weight, next_lightest] =
                neighborhoodIntersection(&neighborhood, P, R, X,
                                         ex_weight, lightest);
            lightest = next_lightest;

            if (current_ex_weight >=
                2 * lightest * (P->size() + R->size() - 1)) {
                return false;
            }

            std::vector<NodeID> nextR = *R;
            nextR.emplace_back(n);

            if (!bronKerbosch(graph, &nextP, &nextR,
                              &nextX, depth + 1, current_ex_weight, lightest)) {
                return false;
            }

            // remove n from P
            auto f = std::find_if(P->begin(), P->end(),
                                  [=](const auto el1) {
                                      return (el1.first == n);
                                  });

            if (f != P->end()) {
                P->erase(f);
            }

            auto f2 = std::lower_bound(X->begin(), X->end(), n,
                                       [](const auto& p1, const auto& p2) {
                                           return p1.first < p2;
                                       });

            if (f2 == X->end() || (*f2).first != n) {
                X->insert(f2, std::make_pair(n, 0));
            }
        }
        return true;
    }

    union_find contractSemiIsolatedCliques(
        problemPointer problem) {
        std::unordered_set<NodeID> terms;
        union_find uf(problem->graph->n());
        for (const auto& t : problem->terminals) {
            terms.emplace(t.position);
        }

        for (const auto& c : cliques) {
            std::unordered_set<NodeID> clique_set;
            bool finished = false;
            for (const NodeID& n : c) {
                clique_set.emplace(n);
                if (terms.count(n) > 0) {
                    finished = true;
                    break;
                }
            }

            if (finished)
                continue;

            EdgeWeight min_edgeweight = UNDEFINED_EDGE;

            EdgeWeight external = 0;
            for (const NodeID& n : c) {
                EdgeWeight node_external = 0;
                for (const EdgeID& e : problem->graph->edges_of(n)) {
                    auto [tgt, wgt] = problem->graph->getEdge(n, e);
                    if (clique_set.count(tgt) > 0) {
                        min_edgeweight = std::min(min_edgeweight, wgt);
                    } else {
                        node_external += wgt;
                    }
                }
                EdgeWeight node_internal =
                    problem->graph->getWeightedNodeDegree(n) - node_external;

                if (node_internal < c.size() - 1) {
                    exit(1);
                }
                external += node_external;
            }

            if (finished)
                continue;

            EdgeWeight external_limit = 2 * min_edgeweight * (c.size() - 1);

            if (external <= external_limit && c.size() > 1) {
                for (const NodeID& n : c) {
                    std::cout << n << ": ";
                    for (EdgeID e : problem->graph->edges_of(n)) {
                        auto [t, w] = problem->graph->getEdge(n, e);
                        std::cout << "(" << t << "," << w << ") ";
                    }

                    std::cout << std::endl;
                    uf.Union(n, c[0]);
                }
            }
        }
        if (problem->graph->n() > uf.n())
        return uf;
    }

    std::vector<std::vector<NodeID> > cliques;
};
