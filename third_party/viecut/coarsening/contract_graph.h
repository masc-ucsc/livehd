/******************************************************************************
 * contract_graph.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "algorithms/misc/graph_algorithms.h"
#include "common/configuration.h"
#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"
#include "data_structure/union_find.h"
#include "tools/string.h"
#include "tools/vector.h"

class contraction {
 public:
    static constexpr bool debug = false;

    static graphAccessPtr deleteEdge(
        graphAccessPtr G, EdgeID e_del) {
        graphAccessPtr G_out = std::make_shared<graph_access>();

        G_out->start_construction(G->number_of_nodes(),
                                  G->number_of_edges() - 2);
        EdgeID e_del_rev = G->find_reverse_edge(e_del);

        for (NodeID n : G->nodes()) {
            for (EdgeID e : G->edges_of(n)) {
                if (e != e_del && e != e_del_rev && G->getEdgeWeight(e)) {
                    EdgeID new_e = G_out->new_edge(n, G->getEdgeTarget(e));
                    G_out->setEdgeWeight(new_e, G->getEdgeWeight(e));
                }
            }
            G_out->new_node();
        }
        G_out->finish_construction();
        return G_out;
    }

    static std::pair<graphAccessPtr, std::vector<NodeID> >
    contractEdge(graphAccessPtr G,
                 std::vector<NodeID> terminals,
                 EdgeID e_ctr) {
        NodeID src = G->getEdgeSource(e_ctr);
        NodeID tgt = G->getEdgeTarget(e_ctr);
        union_find uf(G->number_of_nodes());
        uf.Union(src, tgt);

        auto G_out = contraction::fromUnionFind(G, &uf);
        std::vector<NodeID> terminals_out;
        for (NodeID t : terminals) {
            terminals_out.emplace_back(G->getPartitionIndex(t));
        }

        return std::make_pair(G_out, terminals_out);
    }

    template <class GraphPtr>
    static void findTrivialCuts(GraphPtr G,
                                std::vector<NodeID>* m,
                                std::vector<std::vector<NodeID> >* rm,
                                int target_mindeg) {
        // non-const references for better syntax
        std::vector<NodeID>& mapping = *m;
        std::vector<std::vector<NodeID> >& reverse_mapping = *rm;

        for (NodeID p = 0; p < reverse_mapping.size(); ++p) {
            NodeID bestNode;
            int64_t improve = 0;
            int64_t node_degree = 0;
            int64_t block_degree = 0;
            if (reverse_mapping[p].size() < std::log2(G->number_of_nodes())) {
                NodeID improve_idx;
                for (NodeID node = 0;
                     node < reverse_mapping[p].size(); ++node) {
                    NodeID vtx = reverse_mapping[p][node];
                    for (EdgeID e : G->edges_of(vtx)) {
                        NodeID contracted_target =
                            mapping[G->getEdgeTarget(vtx, e)];
                        if (contracted_target == p) {
                            node_degree += G->getEdgeWeight(vtx, e);
                            continue;
                        }

                        node_degree -= G->getEdgeWeight(vtx, e);
                        block_degree += G->getEdgeWeight(vtx, e);
                    }

                    if (improve > node_degree) {
                        improve = node_degree;
                        bestNode = reverse_mapping[p][node];
                        improve_idx = node;
                    }
                    node_degree = 0;
                }
                if (improve < 0 &&
                    block_degree + improve < target_mindeg &&
                    reverse_mapping[p].size() > 1) {
                    target_mindeg = block_degree + improve;
                    reverse_mapping[p].erase(reverse_mapping[p].begin() +
                                             improve_idx);
                    assert(bestNode < G->number_of_nodes());
                    reverse_mapping.push_back({ bestNode });
                    mapping[bestNode] = reverse_mapping.size() - 1;
                    p--;
                }
            }
        }

    }

    static mutableGraphPtr fromUnionFind(mutableGraphPtr G, union_find* uf,
                                         bool copy = false) {
        bool save_cut = configuration::getConfig()->save_cut;
        if (uf->n() == G->n() && !copy) {
            // no contraction
            return G;
        }

        std::vector<std::vector<NodeID> > reverse_mapping(uf->n());
        std::vector<NodeID> part(G->number_of_nodes(), UNDEFINED_NODE);
        std::vector<NodeID> mapping(G->n(), UNDEFINED_NODE);
        NodeID current_pid = 0;
        for (NodeID n : G->nodes()) {
            NodeID part_id = uf->Find(n);
            if (part[part_id] == UNDEFINED_NODE) {
                part[part_id] = current_pid++;
            }
            mapping[n] = part[part_id];
            if (save_cut) {
                G->setPartitionIndex(n, part[part_id]);
            }
            reverse_mapping[part[part_id]].push_back(
                G->containedVertices(n)[0]);
        }
        return contractGraph(G, mapping, reverse_mapping, copy);
    }

    static mutableGraphPtr contractGraph(
        mutableGraphPtr G,
        const std::vector<NodeID>& mapping,
        const std::vector<std::vector<NodeID> >& reverse_mapping,
        bool copy = true) {
        NodeID cn = reverse_mapping.size();

        if (cn * 2 > G->n() || !copy) {
            return contractGraphVertexset(G, mapping, reverse_mapping, copy);
        } else {
            return contractGraphSparse(G, mapping, reverse_mapping);
        }
    }

    static mutableGraphPtr contractGraphVertexset(
        mutableGraphPtr G,
        const std::vector<NodeID>&,
        const std::vector<std::vector<NodeID> >& reverse_mapping,
        bool copy = true) {
        mutableGraphPtr H;
        if (copy) {
            H = std::make_shared<mutable_graph>(*G);
        } else {
            H = G;
        }
        for (size_t i = 0; i < reverse_mapping.size(); ++i) {
            if (reverse_mapping[i].size() > 1) {
                std::unordered_set<NodeID> vtx_to_ctr;
                for (auto v : reverse_mapping[i]) {
                    vtx_to_ctr.emplace(H->getCurrentPosition(v));
                }
                H->contractVertexSet(vtx_to_ctr);
            }
        }

        if (copy) {
            for (size_t i = 0; i < reverse_mapping.size(); ++i) {
                for (auto v : reverse_mapping[i]) {
                    G->setPartitionIndex(v, H->getCurrentPosition(v));
                }
            }
            H->resetContainedvertices();
        }

        if (debug) {
            graph_algorithms::checkGraphValidity(H);
        }
        return H;
    }

    static graphAccessPtr fromUnionFind(graphAccessPtr G, union_find* uf,
                                        bool = false) {
        std::vector<std::vector<NodeID> > reverse_mapping(uf->n());

        std::vector<NodeID> mapping(G->number_of_nodes());
        std::vector<NodeID> part(G->number_of_nodes(), UNDEFINED_NODE);
        NodeID current_pid = 0;

        const bool save_cut = configuration::getConfig()->save_cut;
        for (NodeID n : G->nodes()) {
            NodeID part_id = uf->Find(n);

            if (part[part_id] == UNDEFINED_NODE) {
                part[part_id] = current_pid++;
            }

            mapping[n] = part[part_id];
            if (save_cut) {
                G->setPartitionIndex(n, part[part_id]);
            }
            reverse_mapping[part[part_id]].push_back(n);
        }

        return contractGraph(G, mapping, reverse_mapping);
    }

    static graphAccessPtr contractGraph(
        graphAccessPtr G,
        const std::vector<NodeID>& mapping,
        const std::vector<std::vector<NodeID> >& reverse_mapping,
        bool = false) {
        return contractGraphSparse(G, mapping, reverse_mapping);
    }

    // altered version of KaHiPs matching contraction
    template <class GraphPtr>
    static GraphPtr contractGraphSparse(
        GraphPtr G,
        const std::vector<NodeID>& mapping,
        const std::vector<std::vector<NodeID> >& reverse_mapping) {
        // first: coarse vertex which set this (to avoid total invalidation)
        // second: edge id in contracted graph
        std::vector<EdgeWeight> weights(reverse_mapping.size());
        auto contracted = std::make_shared<typename GraphPtr::element_type>();
        contracted->start_construction(reverse_mapping.size(),
                                       G->number_of_edges());

        for (NodeID p = 0; p < reverse_mapping.size(); ++p) {
            std::unordered_set<NodeID> edge_positions;
            contracted->new_node();
            for (NodeID node = 0; node < reverse_mapping[p].size(); ++node) {
                NodeID n = reverse_mapping[p][node];
                for (EdgeID e : G->edges_of(reverse_mapping[p][node])) {
                    if (G->getEdgeWeight(n, e) == 0)
                        continue;

                    auto [tgt, wgt] = G->getEdge(n, e);

                    NodeID contracted_target = mapping[tgt];

                    if constexpr (
                        std::is_same<GraphPtr, mutableGraphPtr>::value) {
                        if (contracted_target <= p) {
                            continue;
                        }
                    }

                    if (contracted_target == p)
                        continue;

                    if (edge_positions.count(contracted_target) > 0) {
                        weights[contracted_target] += wgt;
                    } else {
                        edge_positions.insert(contracted_target);
                        weights[contracted_target] = wgt;
                    }
                }
            }

            for (const auto& tgt : edge_positions) {
                contracted->new_edge(p, tgt, weights[tgt]);
            }
        }

        contracted->finish_construction();

        return contracted;
    }
};
