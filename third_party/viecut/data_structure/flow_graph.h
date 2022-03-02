/******************************************************************************
 * flow_graph.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@kit.edu>
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <vector>

#include "common/definitions.h"
#include "data_structure/graph_access.h"

struct rEdge {
    NodeID   source;
    NodeID   target;
    FlowType capacity;
    FlowType flow;
    EdgeID   reverse_edge_index;

    rEdge(NodeID source, NodeID target, FlowType capacity,
          FlowType flow, EdgeID reverse_edge_index) {
        this->source = source;
        this->target = target;
        this->capacity = capacity;
        this->flow = flow;
        this->reverse_edge_index = reverse_edge_index;
    }
};

// this is a adjacency list implementation of the residual graph
// for each edge we create, we create a rev edge with cap 0
// zero capacity edges are residual edges
class flow_graph {
 public:
    flow_graph() {
        m_num_edges = 0;
        m_num_nodes = 0;
    }

    virtual ~flow_graph() { }

    void start_construction(NodeID nodes, EdgeID edges = 0) {
        m_adjacency_lists.resize(nodes);
        m_capacity.resize(nodes);
        m_num_nodes = nodes;
        m_num_edges = edges;
    }

    void finish_construction() { }

    NodeID number_of_nodes() const { return m_num_nodes; }
    EdgeID number_of_edges() { return m_num_edges; }

    NodeID getEdgeTarget(NodeID source, EdgeID e);
    FlowType getEdgeCapacity(NodeID source, EdgeID e) const;

    FlowType getEdgeFlow(NodeID source, EdgeID e);
    void setEdgeFlow(NodeID source, EdgeID e, FlowType flow);

    EdgeID getReverseEdge(NodeID source, EdgeID e);

    void new_edge(NodeID source, NodeID target, FlowType capacity) {
        m_adjacency_lists[source].push_back(
            rEdge(source, target, capacity, 0,
                  m_adjacency_lists[target].size()));
        // for each edge we add a reverse edge
        m_adjacency_lists[target].push_back(
            rEdge(target, source, 0, 0, m_adjacency_lists[source].size() - 1));
        m_capacity[source] += capacity;
        m_num_edges += 2;
    }

    FlowType getCapacity(NodeID node) const {
        return m_capacity[node];
    }

    EdgeID get_first_edge(NodeID /* node */) { return 0; }
    EdgeID get_first_invalid_edge(NodeID node) {
        return m_adjacency_lists[node].size();
    }

    auto nodes() {
        return iterator<NodeID>(0, number_of_nodes());
    }

    auto edges_of(NodeID n) {
        return iterator<EdgeID>(get_first_edge(n), get_first_invalid_edge(n));
    }

 private:
    std::vector<std::vector<rEdge> > m_adjacency_lists;
    std::vector<FlowType> m_capacity;
    NodeID m_num_nodes;
    NodeID m_num_edges;
};

inline
FlowType flow_graph::getEdgeCapacity(NodeID source, EdgeID e) const {
#ifdef NDEBUG
    return m_adjacency_lists[source][e].capacity;
#else
    return m_adjacency_lists.at(source).at(e).capacity;
#endif
}

inline
void flow_graph::setEdgeFlow(NodeID source, EdgeID e, FlowType flow) {
#ifdef NDEBUG
    m_adjacency_lists[source][e].flow = flow;
#else
    m_adjacency_lists.at(source).at(e).flow = flow;
#endif
}

inline
FlowType flow_graph::getEdgeFlow(NodeID source, EdgeID e) {
#ifdef NDEBUG
    return m_adjacency_lists[source][e].flow;
#else
    return m_adjacency_lists.at(source).at(e).flow;
#endif
}

inline
NodeID flow_graph::getEdgeTarget(NodeID source, EdgeID e) {
#ifdef NDEBUG
    return m_adjacency_lists[source][e].target;
#else
    return m_adjacency_lists.at(source).at(e).target;
#endif
}

inline
EdgeID flow_graph::getReverseEdge(NodeID source, EdgeID e) {
#ifdef NDEBUG
    return m_adjacency_lists[source][e].reverse_edge_index;
#else
    return m_adjacency_lists.at(source).at(e).reverse_edge_index;
#endif
}
