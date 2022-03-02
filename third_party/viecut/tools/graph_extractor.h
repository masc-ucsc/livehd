/******************************************************************************
 * graph_extractor.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@kit.edu>
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"

class graph_extractor {
 public:
    graph_extractor() { }
    virtual ~graph_extractor() { }

    std::pair<graphAccessPtr, std::vector<NodeID> >
    extract_block(graphAccessPtr G,
                  PartitionID block) {
        auto extracted_block = std::make_shared<graph_access>();
        // build reverse mapping
        std::vector<NodeID> reverse_mapping;
        NodeID nodes = 0;
        NodeID dummy_node = G->number_of_nodes() + 1;
        for (NodeID node : G->nodes()) {
            if (G->getPartitionIndex(node) == block) {
                reverse_mapping.push_back(nodes++);
            } else {
                reverse_mapping.push_back(dummy_node);
            }
        }

        extracted_block->start_construction(nodes, G->number_of_edges());

        for (NodeID node : G->nodes()) {
            if (G->getPartitionIndex(node) == block) {
                NodeID new_node = extracted_block->new_node();
                for (EdgeID e : G->edges_of(node)) {
                    NodeID target = G->getEdgeTarget(e);
                    if (G->getPartitionIndex(target) == block) {
                        EdgeID new_edge = extracted_block->new_edge(
                            new_node, reverse_mapping[target]);
                        extracted_block->setEdgeWeight(new_edge,
                                                       G->getEdgeWeight(e));
                    }
                }
            }
        }

        extracted_block->finish_construction();
        return std::make_pair(extracted_block, reverse_mapping);
    }

    std::tuple<mutableGraphPtr, std::vector<NodeID>,
               std::vector<NodeID> > extract_block(
        mutableGraphPtr G,
        PartitionID block,
        const std::vector<int>& components) {
        auto extracted_block = std::make_shared<mutable_graph>();

        // build reverse mapping
        std::vector<NodeID> mapping;
        std::vector<NodeID> reverse_mapping;
        NodeID nodes = 0;
        NodeID dummy_node = G->number_of_nodes() + 1;
        for (NodeID node : G->nodes()) {
            if (components[node] == static_cast<int>(block)) {
                reverse_mapping.push_back(nodes++);
                mapping.push_back(node);
            } else {
                reverse_mapping.push_back(dummy_node);
            }
        }

        extracted_block->start_construction(nodes);

        for (NodeID node : G->nodes()) {
            if (components[node] == static_cast<int>(block)) {
                NodeID new_node = extracted_block->new_node();
                for (EdgeID e : G->edges_of(node)) {
                    NodeID target = G->getEdgeTarget(node, e);
                    EdgeWeight wgt = G->getEdgeWeight(node, e);
                    if (components[target] == static_cast<int>(block)) {
                        extracted_block->new_edge(new_node,
                                                  reverse_mapping[target],
                                                  wgt);
                    }
                }
            }
        }

        extracted_block->finish_construction();
        return std::make_tuple(extracted_block, mapping, reverse_mapping);
    }
};
