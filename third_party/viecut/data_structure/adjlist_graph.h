/******************************************************************************
 * adjlist_graph.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <bitset>
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/graph_access.h"

class adjlist_graph {
 public:
    adjlist_graph() { }
    virtual ~adjlist_graph() { }

    void start_construction(NodeID n, EdgeID m) {
        m_adjacencies.resize(n);
        m_num_nodes = n;
        m_num_nodes_merged = n;
        m_num_edges = m;
        nodenum = 0;
        deadnodes.resize(n, false);
    }

    void finish_construction() {
        assert(nodenum == m_num_nodes);
    }

    NodeID number_of_nodes() { return m_num_nodes; }
    EdgeID number_of_edges() { return m_num_edges; }

    NodeID number_of_merged_nodes() { return m_num_nodes_merged; }

    NodeID new_node() {
        return new_node((NodeWeight)1);
    }

    NodeID new_node(NodeWeight wgt) {
        m_adjacencies[nodenum].first = wgt;
        return nodenum++;
    }

    void setNodeWeight(NodeID node, NodeWeight wgt) {
        m_adjacencies[node].first = wgt;
    }

    void new_edge(NodeID source, NodeID target, EdgeWeight wgt) {
        m_adjacencies[source].second.emplace_back(target, wgt);
    }

    EdgeID get_first_edge(NodeID node) {
        assert(deadnodes[node] == false);
        return (EdgeID)node * (EdgeID)m_num_nodes;
    }

    EdgeID get_first_invalid_edge(NodeID node) {
        return get_first_edge(node) + m_adjacencies[node].second.size();
    }

    NodeID getEdgeTarget(EdgeID edge) {
        return m_adjacencies[edge / m_num_nodes].second[
            edge % m_num_nodes].target;
    }

    EdgeWeight getEdgeWeight(EdgeID edge) {
        return m_adjacencies[edge / m_num_nodes].second[
            edge % m_num_nodes].weight;
    }

    EdgeWeight getNodeDegree(NodeID node) {
        return m_adjacencies[node].second.size();
    }

    EdgeWeight getWeightedNodeDegree(NodeID node) {
        EdgeWeight wgt = 0;
        for (const Edge& e : m_adjacencies[node].second) {
            wgt += e.weight;
        }
        return wgt;
    }

    bool HasEdgeTo(NodeID node1, NodeID node2) {
        for (const Edge& e : m_adjacencies[node1].second) {
            if (e.target == node2) {
                return true;
            }
        }
        return false;
    }

    void construct_from_graph(const graph_access& G) {
        start_construction(G.number_of_nodes(), G.number_of_edges());
        for (NodeID n : G.nodes()) {
            new_node(G.getNodeWeight(n));
            for (EdgeID e : G.edges_of(n)) {
                new_edge(n, G.getEdgeTarget(e), G.getEdgeWeight(e));
            }
        }
        finish_construction();
    }

    void merge_nodes(NodeID node1, NodeID node2) {
        std::unordered_map<NodeID, EdgeWeight> edges;

        assert(deadnodes[node2] == false);
        assert(deadnodes[node1] == false);

        EdgeWeight new_wgt = 0;
        for (Edge e : m_adjacencies[node1].second) {
            if (e.target != node2) {
                edges.emplace(e.target, e.weight);
                new_wgt += e.weight;
            }
        }

        for (Edge e : m_adjacencies[node2].second) {
            if (e.target != node1) {
                new_wgt += e.weight;
                if (edges.find(e.target) == edges.end()) {
                    edges.emplace(e.target, e.weight);
                } else {
                    edges[e.target] += e.weight;
                }
            }
        }

        m_adjacencies[node1].second.clear();
        for (auto e : edges) {
            m_adjacencies[node1].second.emplace_back(e.first, e.second);
        }
        m_adjacencies[node1].first += m_adjacencies[node2].first;
        m_adjacencies[node2].first = 0;

        std::vector<Edge>().swap(m_adjacencies[node2].second);

        assert(new_wgt == getWeightedNodeDegree(node1));
        deadnodes[node2] = true;

        if (new_wgt > m_max_degree) {
            m_max_degree = new_wgt;
        }

        if (new_wgt < m_min_degree) {
            m_min_degree = new_wgt;
        }

        m_num_nodes_merged--;
    }

    EdgeWeight getMaxDegree() {
        if (m_max_degree == 0) {
            for (NodeID node = 0; node < m_num_nodes; ++node) {
                EdgeWeight deg = getWeightedNodeDegree(node);
                if (deg > m_max_degree) {
                    m_max_degree = deg;
                }
            }
        }
        return m_max_degree;
    }

    EdgeWeight getMinDegree() {
        if (m_min_degree == UNDEFINED_NODE) {
            for (NodeID node = 0; node < m_num_nodes; ++node) {
                EdgeWeight deg = getWeightedNodeDegree(node);
                if (deg < m_min_degree) {
                    m_min_degree = deg;
                }
            }
        }
        return m_min_degree;
    }

 private:
    std::vector<std::pair<NodeWeight, std::vector<Edge> > > m_adjacencies;
    std::vector<bool> deadnodes;

    NodeID nodenum;
    NodeID m_num_nodes;
    NodeID m_num_nodes_merged;
    EdgeID m_num_edges;
    EdgeWeight m_max_degree = 0;
    EdgeWeight m_min_degree = UNDEFINED_NODE;
};
