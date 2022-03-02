/******************************************************************************
 * sparsify.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "algorithms/global_mincut/ks_minimum_cut.h"
#include "data_structure/graph_access.h"
#include "tools/random_functions.h"

class sparsify {
 public:
    graphAccessPtr one_ks(
        graphAccessPtr G_in) {
        double contraction = configuration::getConfig()->contraction_factor;
        size_t iteration = configuration::getConfig()->seed;
        union_find uf(G_in->number_of_nodes());
        ks_minimum_cut subroutine;
        timer t;

        constexpr bool weighted = true;

        if (weighted) {
            subroutine.sample_contractible_weighted(
                G_in, G_in->number_of_nodes(), &uf, contraction, iteration);
        } else {
            subroutine.sample_contractible(
                G_in, G_in->number_of_nodes(), &uf, contraction, iteration);
        }

        auto G2 = contraction::fromUnionFind(G_in, &uf);

        return G2;
    }

    graphAccessPtr random_matching(
        graphAccessPtr G_in) {
        graphAccessPtr G_out = std::make_shared<graph_access>();

        const size_t MAX_TRIES = 50;
        size_t no_of_coarse_vertices = 0;
        std::vector<NodeID> edge_matching(G_in->number_of_nodes());

        for (NodeID n : G_in->nodes()) {
            edge_matching[n] = n;
        }

        std::vector<NodeID> coarse_mapping(G_in->number_of_nodes());

        for (NodeID n : G_in->nodes()) {
            if (edge_matching[n] == n) {
                size_t no_try = 0;
                NodeID matchingPartner = n;
                while (no_try < MAX_TRIES) {
                    // match with a random neighbor
                    EdgeID s = G_in->get_first_edge(n);
                    EdgeID modulo = G_in->getUnweightedNodeDegree(n);
                    EdgeID r = s + (random_functions::next() % modulo);
                    NodeID tgt = G_in->getEdgeTarget(r);
                    if (edge_matching[tgt] == tgt) {
                        matchingPartner = tgt;
                        break;
                    }
                    no_try++;
                }

                if (no_try == MAX_TRIES) {
                    coarse_mapping[n] = no_of_coarse_vertices;
                    edge_matching[n] = n;
                    no_of_coarse_vertices++;
                } else {
                    coarse_mapping[matchingPartner] = no_of_coarse_vertices;
                    coarse_mapping[n] = no_of_coarse_vertices;
                    edge_matching[matchingPartner] = n;
                    edge_matching[n] = matchingPartner;
                    no_of_coarse_vertices++;
                }
            }
        }

        G_out->start_construction(no_of_coarse_vertices,
                                  G_in->number_of_edges());

        std::vector<std::pair<NodeID, NodeID> > edge_positions(
            no_of_coarse_vertices,
            std::make_pair(UNDEFINED_NODE, UNDEFINED_EDGE));

        NodeID cur_no_vertices = 0;

        size_t edgerino = 0;
        for (NodeID n : G_in->nodes()) {
            // we look only at the coarser nodes
            if (coarse_mapping[n] < cur_no_vertices)
                continue;

            NodeID coarseNode = G_out->new_node();

            // do something with all outgoing edges (in auxillary graph)
            for (EdgeID e : G_in->edges_of(n)) {
                edgerino++;
                EdgeID new_coarse_edge_target = coarse_mapping[
                    G_in->getEdgeTarget(e)];

                if (new_coarse_edge_target == coarseNode) continue;

                auto pos = edge_positions[new_coarse_edge_target];

                if (pos.first != coarseNode) {
                    EdgeID coarseEdge = G_out->new_edge(coarseNode,
                                                        new_coarse_edge_target);
                    G_out->setEdgeWeight(coarseEdge, G_in->getEdgeWeight(e));
                    edge_positions[new_coarse_edge_target] =
                        std::make_pair(coarseNode, coarseEdge);
                } else {
                    EdgeWeight new_weight = G_out->getEdgeWeight(pos.second)
                                            + G_in->getEdgeWeight(e);

                    G_out->setEdgeWeight(pos.second, new_weight);
                }
            }

            // this node was really matched
            NodeID matched_neighbor = edge_matching[n];
            if (n != matched_neighbor) {
                for (EdgeID e : G_in->edges_of(matched_neighbor)) {
                    EdgeID new_coarse_edge_target = coarse_mapping[
                        G_in->getEdgeTarget(e)];

                    if (new_coarse_edge_target == coarseNode) continue;

                    auto pos = edge_positions[new_coarse_edge_target];

                    if (pos.first != coarseNode) {
                        EdgeID coarseEdge = G_out->new_edge(
                            coarseNode, new_coarse_edge_target);
                        G_out->setEdgeWeight(coarseEdge,
                                             G_in->getEdgeWeight(e));
                        edge_positions[new_coarse_edge_target] =
                            std::make_pair(coarseNode, coarseEdge);
                    } else {
                        EdgeWeight new_weight = G_out->getEdgeWeight(pos.second)
                                                + G_in->getEdgeWeight(e);

                        G_out->setEdgeWeight(pos.second, new_weight);
                    }
                }
            }

            cur_no_vertices++;
        }
        G_out->finish_construction();
        return G_out;
    }

    graphAccessPtr remove_heavy_vertices(
        graphAccessPtr G_in, double percentile,
        std::vector<NodeID>* p) {
        std::vector<NodeID>& prefixsum = *p;

        graphAccessPtr G_out = std::make_shared<graph_access>();

        EdgeWeight bound_deg = G_in->getPercentile(percentile);

        // new vertex ids in sparser graph
        prefixsum.resize(G_in->number_of_nodes());

        size_t ctr = 0;
        // this is an upper bound, as edges to dense vectors are also counted.
        // we can resize the edge vector afterwards though, no need to check
        // each edge (todo: if slow, this needs to be changed for parallelism)
        size_t existing_edges = 0;
        for (NodeID n : G_in->nodes()) {
            if (G_in->getWeightedNodeDegree(n) < bound_deg) {
                prefixsum[n] = ctr++;
                existing_edges += G_in->getUnweightedNodeDegree(n);
            } else {
                prefixsum[n] = G_in->number_of_nodes();
            }
        }

        G_out->start_construction(ctr, existing_edges);

        size_t actually_edges = 0;
        for (NodeID n : G_in->nodes()) {
            size_t pre = prefixsum[n];
            if (pre < G_in->number_of_nodes()) {
                G_out->new_node();
                for (EdgeID e : G_in->edges_of(n)) {
                    NodeID target = G_in->getEdgeTarget(e);
                    size_t pretgt = prefixsum[target];
                    if (prefixsum[target] < G_in->number_of_nodes()) {
                        G_out->new_edge(pre, pretgt);
                        actually_edges++;
                    }
                }
            }
        }

        G_out->finish_construction();

        return G_out;
    }
};
