/******************************************************************************
 * minimum_cut_helpers.h
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
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/configuration.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"

template <class GraphPtr>
class minimum_cut_helpers {
 private:
    static constexpr bool debug = false;
    // Get index of minimum degree vertex
    static size_t minimumIndex(GraphPtr G) {
        size_t minimum_index = 0;
        EdgeWeight mindeg = G->getMinDegree();
        for (NodeID n : G->nodes()) {
            if (G->getWeightedNodeDegree(n) == mindeg) {
                minimum_index = n;
                break;
            }
        }
        return minimum_index;
    }

 public:
    // set minimum cut to initial value (one minimum degree vertex)
    // - this cut will be updated later in the global_mincut
    static void setInitialCutValues(
        const std::vector<GraphPtr>& graphs) {
        if (configuration::getConfig()->save_cut) {
            size_t minimum_index = minimumIndex(graphs.back());
            for (NodeID idx : graphs[0]->nodes()) {
                if (idx == minimum_index) {
                    graphs[0]->setNodeInCut(idx, true);
                } else {
                    graphs[0]->setNodeInCut(idx, false);
                }
            }
        }
    }

    static EdgeWeight updateCut(
        const std::vector<GraphPtr>& graphs,
        EdgeWeight previous_mincut) {
        if (configuration::getConfig()->save_cut) {
            GraphPtr new_graph = graphs.back();
            if (new_graph->number_of_nodes() > 1) {
                if (new_graph->getMinDegree() < previous_mincut) {
                    size_t minimum_index = minimumIndex(graphs.back());

                    for (NodeID idx : graphs[0]->nodes()) {
                        NodeID coarseID = idx;
                        for (size_t lv = 0; lv < graphs.size() - 1; ++lv) {
                            coarseID = graphs[lv]->getPartitionIndex(coarseID);
                        }
                        if (coarseID == minimum_index) {
                            graphs[0]->setNodeInCut(idx, true);
                        } else {
                            graphs[0]->setNodeInCut(idx, false);
                        }
                    }
                }
            }
        }
        if (graphs.back()->number_of_nodes() > 1) {
            return std::min(previous_mincut, graphs.back()->getMinDegree());
        } else {
            return previous_mincut;
        }
    }

    static void retrieveMinimumCut(
        std::vector<GraphPtr> graphs) {
        GraphPtr G = graphs[0];
        size_t inside = 0, outside = 0;
        for (NodeID n : G->nodes()) {
            if (G->getNodeInCut(n)) {
                inside++;
                G->setPartitionIndex(n, 0);
            } else {
                outside++;
                G->setPartitionIndex(n, 1);
            }
        }

        [[maybe_unused]] size_t smaller = 0;
        if (inside > outside) {
            smaller = 1;
        }
#ifndef NDEBUG
        for (NodeID n : G->nodes()) {
            if (G->getPartitionIndex(n) == smaller) {
                std::cout << "n " << n << std::endl;
            }
        }
#endif

    }

    static std::pair<std::vector<NodeID>,
                     std::vector<std::vector<NodeID> > > remap_cluster(
        GraphPtr G,
        const std::vector<NodeID>& cluster_id) {
        std::vector<NodeID> mapping;
        std::vector<std::vector<NodeID> > reverse_mapping;

        PartitionID cur_no_clusters = 0;
        std::unordered_map<PartitionID, PartitionID> remap;

        std::vector<NodeID> part(G->number_of_nodes(), UNDEFINED_NODE);

        const bool save_cut = configuration::getConfig()->save_cut;
        for (NodeID node : G->nodes()) {
            PartitionID cur_cluster = cluster_id[node];
            // check whether we already had that
            if (part[cur_cluster] == UNDEFINED_NODE) {
                part[cur_cluster] = cur_no_clusters++;
                reverse_mapping.emplace_back();
            }

            mapping.emplace_back(part[cur_cluster]);

            if (save_cut) {
                G->setPartitionIndex(node, part[cur_cluster]);
            }
            reverse_mapping[part[cur_cluster]].push_back(node);
        }

        return std::make_pair(mapping, reverse_mapping);
    }

    static void setVertexLocations(
        mutableGraphPtr out_graph,
        const std::vector<GraphPtr>& graphs,
        const std::vector<size_t>& ge_ids,
        const std::vector<std::vector<std::pair<NodeID, NodeID> > >& g_edges,
        const EdgeWeight mincut) {
        out_graph->setOriginalNodes(graphs[0]->number_of_nodes());
        std::vector<NodeID> final;

        for (NodeID n : out_graph->nodes()) {
            std::vector<NodeID> empty;
            out_graph->setContainedVertices(n, empty);
        }

        for (NodeID n = 0; n < graphs.back()->number_of_nodes(); ++n) {
            graphs.back()->setPartitionIndex(
                n, out_graph->getCurrentPosition(n));
        }

        int32_t g_id = ge_ids.size() - 1;
        for (auto i = graphs.size(); i-- > 0 && graphs.size() > 1; ) {
            if (i < graphs.size() - 1) {
#ifdef PARALLEL
#pragma omp parallel for
#endif
                for (NodeID n = 0; n < graphs[i]->number_of_nodes(); ++n) {
                    NodeID index = graphs[i]->getPartitionIndex(n);
                    NodeID id_new = graphs[i + 1]->getPartitionIndex(index);
                    graphs[i]->setPartitionIndex(n, id_new);
                }
            }

            if (g_id != -1 && i == ge_ids[g_id]) {
                for (auto e : g_edges[g_id]) {
                    NodeID new_node = out_graph->new_empty_node();
                    NodeID neighbour = graphs[i]->getPartitionIndex(e.second);
                    out_graph->new_edge_order(new_node, neighbour, mincut);
                    graphs[i]->setPartitionIndex(e.first, new_node);
                }
                --g_id;
            }
        }

        if (configuration::getConfig()->save_cut) {
            std::vector<NodeID> b(out_graph->n(), 0);
            for (NodeID n : graphs[0]->nodes()) {
                NodeID position = graphs[0]->getPartitionIndex(n);
                out_graph->setCurrentPosition(n, position);
                out_graph->addContainedVertex(position, n);
                ++b[position];
            }

            bool cut_logs = configuration::getConfig()->verbose;

            if (cut_logs) {
                std::sort(b.begin(), b.end());
                printLogs(b);
            } else {
                std::sort(b.begin(), b.end());
                bool verbose = configuration::getConfig()->verbose;
                NodeID id0 = b.end() - std::upper_bound(b.begin(), b.end(), 0);
                NodeID id1 = b.end() - std::upper_bound(b.begin(), b.end(), 1);
            }
        }
    }

    static void printLogs(const std::vector<NodeID>& b) {
        NodeID empty = std::lower_bound(b.begin(), b.end(), 1) - b.begin();
        NodeID id1 = b.end() - std::upper_bound(b.begin(), b.end(), 1);
        NodeID id10 = b.end() - std::upper_bound(b.begin(), b.end(), 9);
        NodeID id100 = b.end() - std::upper_bound(b.begin(), b.end(), 99);
        NodeID id1000 = b.end() - std::upper_bound(b.begin(), b.end(), 999);
        NodeID id10000 = b.end() - std::upper_bound(b.begin(), b.end(), 9999);
    }
};
