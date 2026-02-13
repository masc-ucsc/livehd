/******************************************************************************
 * mutable_graph.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2018-2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/graph_access.h"

struct RevEdge {
    NodeID     target;
    EdgeWeight weight;
    EdgeID     reverse_edge;
    FlowType   flow;
    size_t     problem_id;

    RevEdge() { }

    explicit RevEdge(NodeID p_target)
        : target(p_target), weight(1), flow(0), problem_id(0) { }

    RevEdge(NodeID p_target, EdgeWeight p_wgt)
        : target(p_target), weight(p_wgt), flow(0), problem_id(0) { }

    RevEdge(NodeID p_target, EdgeWeight p_wgt, EdgeID p_rev)
        : target(p_target),
          weight(p_wgt),
          reverse_edge(p_rev),
          flow(0),
          problem_id(0) { }

    RevEdge(NodeID p_target, EdgeWeight p_wgt, EdgeID p_rev, FlowType p_flow)
        : target(p_target),
          weight(p_wgt),
          reverse_edge(p_rev),
          flow(p_flow),
          problem_id(0) { }
};

class mutable_graph {
 public:
    static constexpr bool debug = false;

    virtual ~mutable_graph() { }

    mutable_graph(const mutable_graph& copy)
        : partition_index(copy.partition_index),
          vertices(copy.vertices),
          weighted_degree(copy.weighted_degree),
          node_in_cut(copy.node_in_cut),
          current_position(copy.current_position),
          contained_in_this(copy.contained_in_this),
          last_node(copy.last_node),
          num_edges(copy.num_edges),
          partition_count(copy.partition_count),
          original_nodes(copy.original_nodes) { }

    mutable_graph() : last_node(0), num_edges(0), partition_count(0) { }

    void start_construction(NodeID n, EdgeID = 0) {
        vertices.resize(n);
        weighted_degree.resize(n, 0);
        partition_index.resize(n, 0);
        node_in_cut.resize(n, 0);
        current_position.resize(n);
        original_nodes = n;

        for (size_t i = 0; i < n; ++i) {
            contained_in_this.emplace_back(1, i);
            current_position[i] = i;
        }
    }

    void resizePositions(NodeID n) {
        current_position.resize(n, 0);
        original_nodes = n;
    }

    NodeID new_node() {
        if (last_node == vertices.size()) {
            contained_in_this.emplace_back(last_node);
            current_position.emplace_back();
            current_position[last_node] = vertices.size();
            vertices.emplace_back();
            partition_index.emplace_back();
            node_in_cut.emplace_back();
            weighted_degree.emplace_back();
        }

        return last_node++;
    }

    NodeID getLastNode() {
        return last_node;
    }

    NodeID new_empty_node() {
        if (last_node == vertices.size()) {
            contained_in_this.emplace_back();
            vertices.emplace_back();
            partition_index.emplace_back();
            node_in_cut.emplace_back();
            weighted_degree.emplace_back();
        }

        return last_node++;
    }

    EdgeID new_edge(NodeID source, NodeID target) {
        return new_edge(source, target, 1);
    }

    EdgeID new_edge(NodeID source, NodeID target, EdgeWeight wgt) {
        if (source < target) {
            EdgeID tgt_id = vertices[target].size();
            EdgeID src_id = vertices[source].size();
            vertices[source].emplace_back(target, wgt, tgt_id);
            vertices[target].emplace_back(source, wgt, src_id);
            num_edges += 2;
            weighted_degree[source] += wgt;
            weighted_degree[target] += wgt;
            return src_id;
        } else {
            return -1;
        }
    }

    void new_edge_order(NodeID source, NodeID target, EdgeWeight wgt) {
        if (source < target) {
            new_edge(source, target, wgt);
        } else {
            if (source > target) {
                new_edge(target, source, wgt);
            }
        }
    }

    void computeDegrees() {
        // do nothing, this exists for compatibility with graph_access in
        // templates
    }

    auto nodes() const {
        return iterator<NodeID>(0, number_of_nodes());
    }

    auto edges_of(NodeID n) const {
        return iterator<EdgeID>(get_first_edge(n), get_first_invalid_edge(n));
    }

    auto edges_of_starting_at(NodeID n, EdgeID e) const {
        return iterator<EdgeID>(e, get_first_invalid_edge(n));
    }

    bool isEmpty(NodeID n) const {
        return (contained_in_this[n].size() == 0);
    }

    NodeID getEdgeSource(NodeID node, EdgeID edge) const {
        RevEdge e = vertices[node][edge];

        return vertices[e.target][e.reverse_edge].target;
    }

    void setEdgeFlow(NodeID node, EdgeID edge, FlowType flow, size_t f_prob) {
        vertices[node][edge].flow = flow;
        vertices[node][edge].problem_id = f_prob;
    }

    void addEdgeFlow(NodeID node, EdgeID edge, FlowType addFlow, size_t f_pro) {
        if (vertices[node][edge].problem_id == f_pro) {
            vertices[node][edge].flow += addFlow;
        } else {
            vertices[node][edge].flow = addFlow;
            vertices[node][edge].problem_id = f_pro;
        }
    }

    FlowType getEdgeFlow(NodeID node, EdgeID edge, size_t f_problem) {
        if (vertices[node][edge].problem_id == f_problem) [[unlikely]] {
            return vertices[node][edge].flow;
        } else {
            vertices[node][edge].flow = 0;
            vertices[node][edge].problem_id = f_problem;
            return 0;
        }
    }

    FlowType getEdgeFlow(NodeID node, EdgeID edge) {
        return vertices[node][edge].flow;
    }

    void finish_construction() {
        last_node = vertices.size();
    }

    /* ============================================================= */
    /* graph access methods */
    /* ============================================================= */
    NodeID number_of_nodes() const {
        return vertices.size();
    }

    NodeID n() const {
        return number_of_nodes();
    }

    EdgeID number_of_edges() const {
        return num_edges;
    }

    EdgeID m() const {
        return number_of_edges();
    }

    EdgeID get_first_edge(NodeID) const {
        return 0;
    }

    EdgeID get_first_invalid_edge(NodeID node) const {
        return vertices[node].size();
    }

    PartitionID get_partition_count() const {
        return partition_count;
    }

    void set_partition_count(PartitionID count) {
        partition_count = count;
    }

    bool getNodeInCut(NodeID n) {
        return node_in_cut[n];
    }

    void setNodeInCut(NodeID n, bool b) {
        node_in_cut[n] = b;
    }

    PartitionID getPartitionIndex(NodeID node) {
        return partition_index[node];
    }

    void setPartitionIndex(NodeID node, PartitionID id) {
        partition_index[node] = id;
    }

    EdgeWeight getUnweightedNodeDegree(NodeID node) const {
        return get_first_invalid_edge(node);
    }

    EdgeWeight getWeightedNodeDegree(NodeID node) const {
        return weighted_degree[node];
    }

    EdgeWeight getMaxDegree() {
        return *std::max_element(weighted_degree.begin(),
                                 weighted_degree.end());
    }

    EdgeID getMaxUnweightedDegree() {
        EdgeID max_size = 0;
        for (NodeID n : nodes()) {
            EdgeID curr_size = vertices[n].size();
            max_size = std::max(max_size, curr_size);
        }
        return max_size;
    }

    NodeID getOriginalNodes() {
        return original_nodes;
    }

    EdgeWeight sumOfEdgeWeights() {
        EdgeWeight sum_weights = 0;
        for (NodeID n : nodes()) {
            sum_weights += weighted_degree[n];
        }
        return sum_weights;
    }

    void setOriginalNodes(NodeID n) {
        original_nodes = n;
        if (n > current_position.size()) {
            current_position.resize(n);
        }
    }

    EdgeWeight getMinDegree() {
        return *std::min_element(weighted_degree.begin(),
                                 weighted_degree.end());
    }

    EdgeWeight getEdgeWeight(NodeID node, EdgeID edge) const {
        return vertices[node][edge].weight;
    }

    size_t numContainedVertices(NodeID node) const {
        return contained_in_this[node].size();
    }

    std::vector<NodeID> containedVertices(NodeID node) const {
        return contained_in_this[node];
    }

    void setContainedVertices(NodeID node, std::vector<NodeID> v) {
        contained_in_this[node] = v;
    }

    void addContainedVertex(NodeID node, NodeID v) {
        contained_in_this[node].emplace_back(v);
    }

    void setEdgeWeight(NodeID node, EdgeID edge, EdgeWeight weight) {
        RevEdge e = vertices[node][edge];

        EdgeWeight curr_weight = e.weight;

        NodeID target = e.target;
        EdgeID rev = e.reverse_edge;

        vertices[node][edge].weight = weight;
        vertices[target][rev].weight = weight;

        weighted_degree[node] = weighted_degree[node] + weight - curr_weight;
        weighted_degree[target] =
            weighted_degree[target] + weight - curr_weight;
    }

    std::pair<NodeID, EdgeWeight> getEdge(NodeID node, EdgeID edge) const {
        return { getEdgeTarget(node, edge), getEdgeWeight(node, edge) };
    }

    NodeID getEdgeTarget(NodeID node, EdgeID edge) const {
        return vertices[node][edge].target;
    }

    EdgeID getReverseEdge(NodeID node, EdgeID edge) {
        return vertices[node][edge].reverse_edge;
    }

    NodeID getCurrentPosition(NodeID node) {
        return current_position[node];
    }

    void setCurrentPosition(NodeID node, NodeID pos) {
        current_position[node] = pos;
    }

    void deleteVertex(NodeID node) {
        last_node--;
        NodeID back = number_of_nodes() - 1;

        if (node < back) {
            for (NodeID n : contained_in_this[back]) {
                current_position[n] = node;
            }

            for (NodeID n : contained_in_this[node]) {
                current_position[n] = back;
            }

            contained_in_this[node] = contained_in_this[back];
        }

        for (EdgeID e : edges_of(node)) {
            NodeID tgt = getEdgeTarget(node, e);
            EdgeID rev = getReverseEdge(node, e);
            EdgeWeight wgt = getEdgeWeight(node, e);
            internalDeleteEdge(tgt, rev);
            num_edges -= 2;
            weighted_degree[tgt] -= wgt;
        }

        if (node < back) {
            for (EdgeID e : edges_of(back)) {
                NodeID tgt = getEdgeTarget(back, e);
                EdgeID rev = getReverseEdge(back, e);
                vertices[tgt][rev].target = node;
            }
        }

        vertices[node] = std::move(vertices.back());
        weighted_degree[node] = std::move(weighted_degree.back());
        partition_index[node] = std::move(partition_index.back());
        node_in_cut[node] = std::move(node_in_cut.back());
        vertices.pop_back();
        weighted_degree.pop_back();
        partition_index.pop_back();
        node_in_cut.pop_back();
    }

    // Contraction methods
    void deleteEdge(NodeID node, EdgeID edge) {
        RevEdge e = vertices[node][edge];
        NodeID target = e.target;
        EdgeWeight weight = e.weight;

        if (!vertices[node].size() || !vertices[target].size()) {
            exit(3);
        }

        if (get_first_invalid_edge(node) > edge + 1) {
            vertices[node][edge] =
                std::move(vertices[node][vertices[node].size() - 1]);
            vertices[vertices[node][edge].target]
            [vertices[node][edge].reverse_edge].reverse_edge = edge;
        }
        vertices[node].pop_back();

        if (get_first_invalid_edge(target) > e.reverse_edge + 1) {
            vertices[target][e.reverse_edge] =
                std::move(vertices[target][vertices[target].size() - 1]);
            vertices[vertices[target][e.reverse_edge].target]
            [vertices[target][e.reverse_edge].reverse_edge].reverse_edge
                = e.reverse_edge;
        }
        vertices[target].pop_back();

        weighted_degree[node] -= weight;
        weighted_degree[target] -= weight;

        num_edges -= 2;
    }

    void mergeEdgeSparse(NodeID node, NodeID target, EdgeID ed) {
        NodeID e_target = getEdgeTarget(target, ed);
        EdgeWeight e_weight = getEdgeWeight(target, ed);
        EdgeID del_rev = getReverseEdge(target, ed);
        bool edge_found = false;
        for (EdgeID src_edge : edges_of(node)) {
            if (getEdgeTarget(node, src_edge) == e_target) {
                EdgeID rev = getReverseEdge(node, src_edge);
                vertices[node][src_edge].weight += e_weight;
                vertices[e_target][rev].weight += e_weight;
                weighted_degree[node] += e_weight;
                num_edges -= 2;
                internalDeleteEdge(e_target, del_rev);
                edge_found = true;
                break;
            }
        }
        if (!edge_found) {
            // create new edge and map reverse edge
            vertices[e_target][del_rev].reverse_edge
                = vertices[node].size();
            vertices[e_target][del_rev].target = node;
            vertices[node].emplace_back(
                e_target, e_weight, del_rev);
            weighted_degree[node] += e_weight;
        }
    }

    NodeID contractSparseTargetNoEdge(NodeID node, NodeID target) {
        if (node == target + 1) {
            std::unordered_set<NodeID> nodes = { node, target };
            contractVertexSet(nodes);
            return target;
        }

        last_node--;
        for (EdgeID ed : edges_of(target)) {
            mergeEdgeSparse(node, target, ed);
        }

        for (NodeID n : contained_in_this[target]) {
            contained_in_this[node].emplace_back(n);
            current_position[n] = node;
        }

        contained_in_this[target].clear();

        for (NodeID n : contained_in_this[vertices.size() - 1]) {
            contained_in_this[target].emplace_back(n);
            current_position[n] = target;
        }

        vertices[target] = std::move(vertices.back());
        weighted_degree[target] = std::move(weighted_degree.back());
        partition_index[target] = std::move(partition_index.back());
        node_in_cut[target] = std::move(node_in_cut.back());
        vertices.pop_back();
        weighted_degree.pop_back();
        partition_index.pop_back();
        node_in_cut.pop_back();
        contained_in_this.pop_back();

        // remap all reverse edges of vertex that was now moved to 'target'
        for (EdgeID ed : edges_of(target)) {
            RevEdge e = vertices[target][ed];
            vertices[e.target][e.reverse_edge].target = target;
        }

        return target;
    }

    NodeID contractEdgeSparseTarget(NodeID node, EdgeID edge) {
        RevEdge e = vertices[node][edge];
        NodeID target = e.target;

        if (node == target + 1) {
            return contractEdge(node, edge);
        }
        last_node--;
        for (EdgeID ed : edges_of(target)) {
            if (ed != e.reverse_edge) {
                mergeEdgeSparse(node, target, ed);
            }
        }

        for (NodeID n : contained_in_this[target]) {
            contained_in_this[node].emplace_back(n);
            current_position[n] = node;
        }

        contained_in_this[target].clear();

        for (NodeID n : contained_in_this[vertices.size() - 1]) {
            contained_in_this[target].emplace_back(n);
            current_position[n] = target;
        }

        // delete edge of 'node' to 'target', remap reverse
        internalDeleteEdge(node, edge);
        num_edges -= 2;
        weighted_degree[node] -= e.weight;

        vertices[target] = std::move(vertices.back());
        weighted_degree[target] = std::move(weighted_degree.back());
        partition_index[target] = std::move(partition_index.back());
        node_in_cut[target] = std::move(node_in_cut.back());
        vertices.pop_back();
        weighted_degree.pop_back();
        partition_index.pop_back();
        node_in_cut.pop_back();
        contained_in_this.pop_back();

        // remap all reverse edges of vertex that was now moved to 'target'
        for (EdgeID ed : edges_of(target)) {
            RevEdge re = vertices[target][ed];
            vertices[re.target][re.reverse_edge].target = target;
        }

        return target;
    }

    NodeID contractEdge(NodeID node, EdgeID edge) {
        last_node--;

        if (getEdgeTarget(node, edge) < node) {
            EdgeID re = getReverseEdge(node, edge);
            node = getEdgeTarget(node, edge);
            edge = re;
        }

        RevEdge e = vertices[node][edge];
        NodeID target = e.target;
        EdgeID del_id = 0;
        EdgeWeight del_wgt = 0;

        std::unordered_map<NodeID, std::tuple<EdgeWeight, EdgeID, EdgeID> > map;
        // store targets (and additional info) of vertices
        // in unordered_map to find vertices that are in shared neighbourhood
        for (EdgeID ed : edges_of(node)) {
            if (ed != edge) {
                RevEdge del_ed = vertices[node][ed];
                map.emplace(getEdgeTarget(node, ed),
                            std::make_tuple(del_ed.target,
                                            del_ed.reverse_edge,
                                            ed));
            } else {
                del_wgt = vertices[node][ed].weight;
                del_id = ed;
            }
        }

        for (EdgeID ed : edges_of(target)) {
            if (ed != e.reverse_edge) {
                RevEdge del_edge = vertices[target][ed];
                NodeID e_target = getEdgeTarget(target, ed);
                // neighbour of 'target' also in neighbourhood of 'node'
                if (map.count(e_target) > 0) {
                    // sum up edge weights
                    auto [tgt, rev, ed2] = map[e_target];
                    vertices[node][ed2].weight += del_edge.weight;
                    vertices[tgt][rev].weight += del_edge.weight;

                    weighted_degree[node] += del_edge.weight;

                    num_edges -= 2;

                    internalDeleteEdge(del_edge.target, del_edge.reverse_edge);
                } else {
                    // create new edge and map reverse edge
                    vertices[e_target][del_edge.reverse_edge].reverse_edge
                        = vertices[node].size();
                    vertices[e_target][del_edge.reverse_edge].target = node;
                    vertices[node].emplace_back(e_target, del_edge.weight,
                                                del_edge.reverse_edge);
                    weighted_degree[node] += del_edge.weight;
                }
            }
        }

        for (NodeID n : contained_in_this[target]) {
            contained_in_this[node].emplace_back(n);
            current_position[n] = node;
        }

        contained_in_this[target].clear();

        for (NodeID n : contained_in_this[vertices.size() - 1]) {
            contained_in_this[target].emplace_back(n);
            current_position[n] = target;
        }

        vertices[target] = std::move(vertices.back());
        weighted_degree[target] = std::move(weighted_degree.back());
        partition_index[target] = std::move(partition_index.back());
        node_in_cut[target] = std::move(node_in_cut.back());
        vertices.pop_back();
        weighted_degree.pop_back();
        partition_index.pop_back();
        node_in_cut.pop_back();
        contained_in_this.pop_back();

        // remap all reverse edges of vertex that was now moved to 'target'
        for (EdgeID ed : edges_of(target)) {
            RevEdge re = vertices[target][ed];
            vertices[re.target][re.reverse_edge].target = target;
        }

        // delete edge of 'node' to 'target', remap reverse
        internalDeleteEdge(node, del_id);
        num_edges -= 2;
        weighted_degree[node] -= del_wgt;

        return target;
    }

    void contractVertexSet(const std::unordered_set<NodeID>& vertex_set) {
        std::vector<NodeID> vertex_set_vec;
        NodeID first = UNDEFINED_NODE;
        size_t idx = 0;

        for (auto i : vertex_set) {
            if (i >= vertices.size())
                continue;

            if (i < first) {
                first = i;
                idx = vertex_set_vec.size();
            }
            vertex_set_vec.emplace_back(i);
        }

        if (vertex_set_vec.size() <= 1)
            return;

        vertex_set_vec.erase(vertex_set_vec.begin() + idx);

        std::vector<RevEdge> edges;
        std::unordered_map<NodeID, std::tuple<NodeID, EdgeID, EdgeID> > map;

        for (EdgeID e : edges_of(first)) {
            NodeID target = getEdgeTarget(first, e);
            if (vertex_set.count(target) == 0) {
                RevEdge edge_to_set = vertices[first][e];
                edges.push_back(vertices[first][e]);
                map.emplace(target, std::make_tuple(edge_to_set.target,
                                                    edge_to_set.reverse_edge,
                                                    edges.size() - 1));
                vertices[edge_to_set.target]
                [edge_to_set.reverse_edge].reverse_edge = edges.size() - 1;
            } else {
                weighted_degree[first] -= vertices[first][e].weight;
                num_edges--;
            }
        }

        vertices[first].swap(edges);

        for (NodeID n : vertex_set_vec) {
            if (n == first)
                continue;

            for (EdgeID e : edges_of(n)) {
                NodeID target = getEdgeTarget(n, e);
                if (vertex_set.count(target) == 0) {
                    RevEdge del_edge = vertices[n][e];
                    // neighbour of 'target' also in neighbourhood of 'node'
                    if (map.count(target) > 0) {
                        // sum up edge weights
                        auto[tgt, rev, ed2] = map[target];

                        VIECUT_ASSERT_EQ(vertices[first][ed2].weight,
                                         vertices[tgt][rev].weight);
                        vertices[first][ed2].weight += del_edge.weight;
                        vertices[tgt][rev].weight += del_edge.weight;

                        weighted_degree[first] += del_edge.weight;
                        num_edges -= 2;

                        if (vertices[tgt].size() - 1 == rev) {
                            // we need to move position of reverse edge in 'map'
                            map[target] = std::make_tuple(
                                tgt, del_edge.reverse_edge, ed2);
                        }

                        internalDeleteEdge(del_edge.target,
                                           del_edge.reverse_edge);
                    } else {
                        VIECUT_ASSERT_EQ(
                            del_edge.weight,
                            vertices[target][del_edge.reverse_edge].weight);

                        // create new edge and map reverse edge
                        vertices[target][del_edge.reverse_edge].reverse_edge =
                            vertices[first].size();
                        vertices[target][del_edge.reverse_edge].target = first;

                        vertices[first].emplace_back(target, del_edge.weight,
                                                     del_edge.reverse_edge);
                        weighted_degree[first] += del_edge.weight;
                        map.emplace(
                            target,
                            std::make_tuple(target,
                                            del_edge.reverse_edge,
                                            vertices[first].size() - 1));
                    }
                } else {
                    --num_edges;
                }
            }
        }

        std::sort(vertex_set_vec.begin(), vertex_set_vec.end(),
                  [](auto v1, auto v2) {
                      return v1 > v2;
                  });

        for (NodeID vtx : vertex_set_vec) {
            for (NodeID n : contained_in_this[vtx]) {
                contained_in_this[first].emplace_back(n);
                current_position[n] = first;
            }
            contained_in_this[vtx].clear();

            if (vtx < vertices.size() - 1) {
                for (NodeID n : contained_in_this[vertices.size() - 1]) {
                    contained_in_this[vtx].emplace_back(n);
                    current_position[n] = vtx;
                }
                vertices[vtx] = std::move(vertices.back());
                weighted_degree[vtx] = std::move(weighted_degree.back());
                partition_index[vtx] = std::move(partition_index.back());
                node_in_cut[vtx] = std::move(node_in_cut.back());

                // remap all rev edges of vertex that was now moved to 'target'
                for (EdgeID ed : edges_of(vtx)) {
                    RevEdge e = vertices[vtx][ed];
                    VIECUT_ASSERT_EQ(vertices[e.target][e.reverse_edge].target,
                                     vertices.size() - 1);
                    vertices[e.target][e.reverse_edge].target = vtx;
                }
            }

            vertices.pop_back();
            weighted_degree.pop_back();
            partition_index.pop_back();
            node_in_cut.pop_back();
            contained_in_this.pop_back();
            last_node--;
        }
    }

    // Graph class translation
    static mutableGraphPtr from_graph_access(
        graphAccessPtr G) {
        mutableGraphPtr m_G = std::make_shared<mutable_graph>();
        std::vector<NodeID> last_incident(G->number_of_nodes(), UNDEFINED_NODE);
        m_G->start_construction(G->number_of_nodes());
        for (NodeID n : G->nodes()) {
            m_G->new_node();
            for (EdgeID e : G->edges_of(n)) {
                NodeID t = G->getEdgeTarget(e);
                if (last_incident[t] == n) {
                    // double edge

                    for (EdgeID e2 : m_G->edges_of(n)) {
                        if (m_G->getEdgeTarget(n, e2) == t) {
                            m_G->setEdgeWeight(n, e2,
                                               m_G->getEdgeWeight(n, e2)
                                               + G->getEdgeWeight(e));
                            break;
                        }
                    }
                } else {
                    last_incident[t] = n;
                    m_G->new_edge(n, G->getEdgeTarget(e), G->getEdgeWeight(e));
                }
            }
        }
        m_G->finish_construction();
        return m_G;
    }

    graphAccessPtr to_graph_access() {
        graphAccessPtr G = std::make_shared<graph_access>();
        G->start_construction(number_of_nodes(), number_of_edges());
        for (NodeID n : G->nodes()) {
            G->new_node();
            for (EdgeID e : edges_of(n)) {
                EdgeID edge = G->new_edge(n, getEdgeTarget(n, e));
                G->setEdgeWeight(edge, getEdgeWeight(n, e));
            }
        }
        G->finish_construction();
        return G;
    }

    static mutableGraphPtr deserialize(std::vector<uint64_t> v) {
        mutableGraphPtr G = std::make_shared<mutable_graph>();
        uint64_t num_nodes = v[0];
        size_t num_edges = v[1];
        uint64_t original_nodes = v[3];
        G->start_construction(num_nodes);
        G->set_partition_count(v[2]);
        G->setOriginalNodes(original_nodes);
        size_t n = 0;
        size_t deserial = 4;
        while (n < num_nodes) {
            uint64_t next = v[deserial++];
            if (next >= num_edges) {
                if (next == UNDEFINED_EDGE) {
                    break;
                }
                n = next - num_edges;
                continue;
            }
            uint64_t wgt = v[deserial++];
            G->new_edge(n, next, wgt);
        }

        for (size_t i = 0; i < num_nodes; ++i) {
            G->setPartitionIndex(i, v[deserial++]);
            G->setContainedVertices(i, { });
        }

        for (size_t i = 0; i < original_nodes; ++i) {
            NodeID pos = static_cast<NodeID>(v[deserial++]);
            G->setCurrentPosition(i, pos);
            G->addContainedVertex(pos, i);
        }

        G->finish_construction();

        return G;
    }

    std::vector<uint64_t> serialize() {
        std::vector<uint64_t> serial(5 + 2 * n() + m() + original_nodes);
        serial[0] = static_cast<uint64_t>(vertices.size());
        serial[1] = static_cast<uint64_t>(num_edges);
        serial[2] = static_cast<uint64_t>(partition_count);
        serial[3] = static_cast<uint64_t>(original_nodes);
        size_t next = 4;

        for (NodeID n : nodes()) {
            serial[next++] = static_cast<uint64_t>(num_edges + n);
            for (const auto& [t, w, r, f, l] : vertices[n]) {
                // I am deeply sorry for this ugly code, but structured bindings
                // seem to not work in combination with maybe_unused to suppress
                // unintended unused warnings.
                (void)r;
                (void)f;
                (void)l;
                if (t > n) {
                    serial[next++] = static_cast<uint64_t>(t);
                    serial[next++] = static_cast<uint64_t>(w);
                }
            }
        }

        serial[next++] = UNDEFINED_EDGE;

        for (const auto& p : partition_index) {
            serial[next++] = static_cast<uint64_t>(p);
        }

        for (const auto& c : current_position) {
            serial[next++] = static_cast<uint64_t>(c);
        }

        return serial;
    }

    mutableGraphPtr simplify() {
        mutableGraphPtr G = std::make_shared<mutable_graph>();
        G->start_construction(number_of_nodes());
        for (NodeID n : G->nodes()) {
            G->new_node();
            for (EdgeID e : edges_of(n)) {
                G->new_edge(n, getEdgeTarget(n, e), getEdgeWeight(n, e));
            }
            G->setPartitionIndex(n, getPartitionIndex(n));
        }
        G->finish_construction();
        return G;
    }

    void resetContainedvertices() {
        original_nodes = vertices.size();
        for (NodeID n : nodes()) {
            current_position[n] = n;
            contained_in_this[n] = { n };
        }
    }

 private:
    void internalDeleteEdge(NodeID n, EdgeID e) {
        if (vertices[n].size() > e + 1) {
            vertices[n][e] = std::move(vertices[n][vertices[n].size() - 1]);
            vertices[n].pop_back();

            NodeID tgt = vertices[n][e].target;
            EdgeID rev = vertices[n][e].reverse_edge;
            VIECUT_ASSERT_EQ(vertices[tgt][rev].reverse_edge,
                             vertices[n].size());
            vertices[tgt][rev].reverse_edge = e;
        } else {
            if (vertices[n].size()) {
                vertices[n].pop_back();
            } else {
                exit(-1);
            }
        }
    }

    std::vector<PartitionID> partition_index;
    std::vector<std::vector<RevEdge> > vertices;
    std::vector<EdgeWeight> weighted_degree;
    std::vector<bool> node_in_cut;
    std::vector<NodeID> current_position;
    std::vector<std::vector<NodeID> > contained_in_this;

    NodeID last_node;
    EdgeID num_edges;
    PartitionID partition_count;
    NodeID original_nodes;
};

[[maybe_unused]] static std::string toStringUnweighted(
    mutableGraphPtr G) {
    std::ostringstream oss;

    for (NodeID n : G->nodes()) {
        oss << n << ": [";
        for (EdgeID e : G->edges_of(n)) {
            oss << G->getEdgeTarget(n, e);
            if (e < G->get_first_invalid_edge(n) - 1) {
                oss << ",";
            }
        }
        oss << "] ";
    }
    return oss.str();
}

[[maybe_unused]] static std::string toStringCompact(
    mutableGraphPtr G) {
    std::ostringstream oss;

    for (NodeID n : G->nodes()) {
        oss << " [";
        for (EdgeID e : G->edges_of(n)) {
            oss << G->getEdgeTarget(n, e);
            if (e < G->get_first_invalid_edge(n) - 1) {
                oss << ",";
            }
        }
        oss << "]";
    }
    return oss.str();
}

[[maybe_unused]] static std::string toStringWeighted(
    mutableGraphPtr G) {
    std::ostringstream oss;

    oss << "Graph n " << G->n() << " m " << G->m() << "\n";
    for (NodeID n : G->nodes()) {
        oss << n << ": [";
        for (EdgeID e : G->edges_of(n)) {
            oss << G->getEdgeTarget(n, e) << "("
                << G->getEdgeWeight(n, e) << ")";
            if (e < G->get_first_invalid_edge(n) - 1) {
                oss << ",";
            }
        }
        oss << "]\n";
    }
    return oss.str();
}

[[maybe_unused]] static std::string toStringContains(
    mutableGraphPtr G) {
    std::ostringstream oss;

    for (NodeID n : G->nodes()) {
        oss << n << ": [";
        for (NodeID v : G->containedVertices(n)) {
            oss << v;
            oss << ",";
        }
        oss << "]\n";
    }
    return oss.str();
}

[[maybe_unused]] static std::ostream& operator << (
    std::ostream& os, mutableGraphPtr G) {
    // return os << toStringUnweighted(G);
    return os << toStringWeighted(G);
    // return os << toStringCompact(G);
    // return os << toStringContains(G);
}
