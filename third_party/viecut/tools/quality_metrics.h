/******************************************************************************
 * quality_metrics.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@kit.edu>
 *
 *****************************************************************************/

#pragma once

#include "data_structure/graph_access.h"

class quality_metrics {
 public:
    quality_metrics() { }

    ~quality_metrics() { }

    EdgeWeight edge_cut(const graph_access& G) {
        EdgeWeight edgeCut = 0;
        for (NodeID n : G.nodes()) {
            PartitionID partitionIDSource = G.getPartitionIndex(n);
            for (EdgeID e : G.edges_of(n)) {
                NodeID targetNode = G.getEdgeTarget(e);
                PartitionID partitionIDTarget = G.getPartitionIndex(targetNode);

                if (partitionIDSource != partitionIDTarget) {
                    edgeCut += G.getEdgeWeight(e);
                }
            }
        }
        return edgeCut / 2;
    }
};
