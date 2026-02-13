/******************************************************************************
 * graph_access.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@univie.ac.at>
 * Copyright (C) 2017-2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <sstream>
#include <algorithm>
#include <bitset>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/definitions.h"

struct vNode {
    EdgeID firstEdge;
    bool   in_cut;
};

struct Edge {
    NodeID     target;
    EdgeWeight weight;

    Edge() { }
    explicit Edge(NodeID p_target) : target(p_target), weight(1) { }
    Edge(NodeID p_target, EdgeWeight p_wgt)
        : target(p_target), weight(p_wgt) { }
};

struct refinementNode {
    PartitionID partitionIndex;
    // Count queueIndex;
};

struct coarseningEdge {
    EdgeRatingType rating;
};

class graph_access;

// construction etc. is encapsulated in basicGraph / access to properties etc.
// is encapsulated in graph_access
class basicGraph {
    friend class graph_access;

 public:
    basicGraph() : m_building_graph(false) { }

    // methods only to be used by friend class
    EdgeID number_of_edges() {
        return m_edges.size();
    }

    NodeID number_of_nodes() {
        if (m_nodes.size())
            return m_nodes.size() - 1;
        else
            return 0;
    }

    inline EdgeID get_first_edge(const NodeID& n) {
        return m_nodes[n].firstEdge;
    }

    inline EdgeID get_first_invalid_edge(const NodeID& n) {
        return m_nodes[n + 1].firstEdge;
    }

    // construction of the graph
    void start_construction(NodeID n, EdgeID m) {
        m_building_graph = true;
        node = 0;
        e = 0;
        m_last_source = -1;

        // resizes property arrays
        m_nodes.resize(n + 1);
        m_refinement_node_props.resize(n + 1);
        m_edges.reserve(m);
        // m_coarsening_edge_props.resize(m);

        m_nodes[node].firstEdge = e;
    }

    void resize_m(EdgeID m) {
        m_edges.resize(m, Edge(0));
    }

    NodeID new_node_hacky(EdgeID edge) {
        m_nodes[++node].firstEdge = edge;
        ++m_last_source;
        return node;
    }

    EdgeID new_edge_and_reverse(NodeID source, NodeID target,
                                EdgeID e_for, EdgeID e_rev, EdgeWeight wgt) {
        m_edges[e_for] = Edge(target, wgt);
        m_edges[e_rev] = Edge(source, wgt);

        e = e + 2;
        return e;
    }

    EdgeID new_edge(NodeID source, NodeID target) {
        return new_edge(source, target, 1);
    }

    EdgeID new_edge(NodeID source, NodeID target, EdgeWeight weight) {
        VIECUT_ASSERT_TRUE(m_building_graph);

        if (source == target) {
            return e;
        }

        m_edges.emplace_back(target, weight);
        EdgeID e_bar = e;
        ++e;

        VIECUT_ASSERT_TRUE(source + 1 < m_nodes.size());
        VIECUT_ASSERT_TRUE(target < m_nodes.size());
        m_nodes[source + 1].firstEdge = e;

        // fill isolated sources at the end
        if ((NodeID)(m_last_source + 1) < source) {
            for (NodeID i = source; i > (NodeID)(m_last_source + 1); i--) {
                m_nodes[i].firstEdge = m_nodes[m_last_source + 1].firstEdge;
            }
        }
        m_last_source = source;
        return e_bar;
    }

    NodeID new_node() {
        VIECUT_ASSERT_TRUE(m_building_graph);
        return node++;
    }

    void finish_construction() {
        // inert dummy node
        // m_nodes.resize(node+1);
        // m_refinement_node_props.resize(node+1);
        m_edges.shrink_to_fit();

        // m_edges.resize(e);
        // m_coarsening_edge_props.resize(e);

        m_building_graph = false;

        // fill isolated sources at the end
        if ((unsigned int)(m_last_source) != node - 1) {
            // in that case at least the last node was an isolated node
            for (NodeID i = node; i > (unsigned int)(m_last_source + 1); i--) {
                m_nodes[i].firstEdge = m_nodes[m_last_source + 1].firstEdge;
            }
        }
    }

    // %%%%%%%%%%%%%%%%%%% DATA %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    // split properties for coarsening and uncoarsening

    std::vector<vNode> m_nodes;
    std::vector<Edge> m_edges;

    std::vector<refinementNode> m_refinement_node_props;
    std::vector<coarseningEdge> m_coarsening_edge_props;

    // construction properties
    bool m_building_graph;
    int m_last_source;
    NodeID node;  // current node that is constructed
    EdgeID e;     // current edge that is constructed
};

class complete_boundary;

// adapted from https://codereview.stackexchange.com/a/200013,
// + added operators for random access
template <typename T>
class iterator {
 private:
    class iterator_base {
 public:  // NOLINT
        friend class iterator;

        T m_current;

        explicit iterator_base(T current) : m_current(current) { }

        bool operator != (iterator_base const& other_iter) const {
            return m_current != other_iter.m_current;
        }

        bool operator == (iterator_base const& other_iter) const {
            return m_current == other_iter.m_current;
        }

        T const& operator * () const { return m_current; }

        iterator_base& operator ++ () {  // NOLINT
            ++m_current;
            return *this;
        }

        iterator_base& operator -- () {  // NOLINT
            --m_current;
            return *this;
        }

        friend bool operator < (const iterator_base& i1,
                                const iterator_base& i2) {
            return i1 < i2;
        }

        friend bool operator > (const iterator_base& i1,
                                const iterator_base& i2) {
            return i1 > i2;
        }

        friend bool operator <= (const iterator_base& i1,
                                 const iterator_base& i2) {
            return i1 <= i2;
        }

        friend bool operator >= (const iterator_base& i1,
                                 const iterator_base& i2) {
            return i1 >= i2;
        }

        iterator_base& operator += (size_t s) {
            m_current += s;
            return *this;
        }

        friend iterator_base operator + (const iterator_base& i, size_t s) {
            i.m_current += s;
            return *i;
        }

        friend iterator_base operator + (size_t s, const iterator_base& i) {
            i.m_current += s;
            return *i;
        }

        iterator_base& operator -= (size_t s) {
            m_current -= s;
            return *this;
        }

        friend iterator_base operator - (const iterator_base& i, size_t s) {
            i.m_current -= s;
            return *i;
        }

        friend iterator_base operator - (size_t s, const iterator_base& i) {
            i.m_current -= s;
            return *i;
        }

        iterator_base& operator [] (size_t) const {  // NOLINT
            return m_current;
        }
    };

    T m_begin;
    T m_end;

 public:
    iterator(T begin_value, T end_value)
        : m_begin(begin_value), m_end(end_value) { }
    iterator_base begin() { return iterator_base(m_begin); }
    iterator_base end() { return iterator_base(m_end); }
};

class graph_access {
    friend class complete_boundary;

 public:
    graph_access() {
        graphref = new basicGraph();
        m_separator_block_ID = 2;
    }

    virtual ~graph_access() {
        delete graphref;
    }

    /* ============================================================= */
    /* build methods */
    /* ============================================================= */
    void start_construction(NodeID nodes, EdgeID edges) {
        m_degrees_computed = false;
        graphref->start_construction(nodes, edges);
    }

    NodeID new_node() {
        return graphref->new_node();
    }

    EdgeID new_edge(NodeID source, NodeID target) {
        return graphref->new_edge(source, target);
    }

    EdgeID new_edge(NodeID source, NodeID target, EdgeWeight weight) {
        return graphref->new_edge(source, target, weight);
    }

    auto nodes() const {
        return iterator<NodeID>(0, number_of_nodes());
    }

    auto edges() const {
        return iterator<EdgeID>(0, number_of_edges());
    }

    auto edges_of(NodeID n) const {
        return iterator<EdgeID>(get_first_edge(n), get_first_invalid_edge(n));
    }

    auto edges_of_starting_at(NodeID n, EdgeID e) const {
        return iterator<EdgeID>(e, get_first_invalid_edge(n));
    }

    NodeID getEdgeSource(EdgeID edge) const {
        return std::lower_bound(
            graphref->m_nodes.begin(),
            graphref->m_nodes.end(), edge,
            [](auto it, const EdgeID& edgecmp) {
                return it.firstEdge <= edgecmp;
            }) - graphref->m_nodes.begin() - 1;
    }

    bool currentlyBuildingGraph() const {
        return graphref->m_building_graph;
    }

    // creates an edge and its reverse edge in m_edges vector
    // need to call resize_m first, as this places the reverse edge in the
    // undiscovered part of m_edges - this is hacky but should be fast
    // this only places edges in order to not clutter this class
    // intelligence should be outside of the function
    EdgeID new_edge_and_reverse(NodeID source, NodeID target,
                                EdgeID e, EdgeID e_rev, EdgeWeight wgt = 1) {
        return graphref->new_edge_and_reverse(source, target, e, e_rev, wgt);
    }

    // new node version for the sparsification variant
    NodeID new_node_hacky(EdgeID edge) {
        return graphref->new_node_hacky(edge);
    }

    void resize_m(EdgeID m) {
        graphref->resize_m(m);
    }

    void finish_construction() {
        m_degree.resize(number_of_nodes());
        graphref->finish_construction();
    }

    /* ============================================================= */
    /* graph access methods */
    /* ============================================================= */
    NodeID number_of_nodes() const {
        return graphref->number_of_nodes();
    }

    NodeID n() const {
        return number_of_nodes();
    }

    EdgeID number_of_edges() const {
        return graphref->number_of_edges();
    }

    EdgeID m() const {
        return number_of_edges();
    }

    EdgeID get_first_edge(NodeID node) const {
#ifdef NDEBUG
        return graphref->m_nodes[node].firstEdge;
#else
        return graphref->m_nodes.at(node).firstEdge;
#endif
    }

    EdgeID get_first_invalid_edge(NodeID node) const {
        return graphref->m_nodes[node + 1].firstEdge;
    }

    PartitionID get_partition_count() const {
        return m_partition_count;
    }

    void set_partition_count(PartitionID count) {
        m_partition_count = count;
    }

    PartitionID getSeparatorBlock();
    void setSeparatorBlock(PartitionID id);

    PartitionID getPartitionIndex(NodeID node) const;
    void setPartitionIndex(NodeID node, PartitionID id);

    PartitionID getSecondPartitionIndex(NodeID) const {
        assert(0);
        return 0;
    }

    void setSecondPartitionIndex(NodeID, PartitionID) {
        assert(0);
    }
    void resizeSecondPartitionIndex(unsigned) {
        assert(0);
    }

    EdgeWeight getUnweightedNodeDegree(NodeID node) const {
        return graphref->m_nodes[node + 1].firstEdge
               - graphref->m_nodes[node].firstEdge;
    }

    EdgeWeight getWeightedNodeDegree(NodeID node) const {
        if (m_degrees_computed) {
            return m_degree[node];
        } else {
            EdgeWeight degree = 0;
            for (unsigned e = graphref->m_nodes[node].firstEdge;
                 e < graphref->m_nodes[node + 1].firstEdge; ++e) {
                degree += getEdgeWeight(e);
            }
            return degree;
        }
    }

    EdgeWeight getMaxDegree() {
        if (!m_degrees_computed) {
            computeDegrees();
        }

        return m_max_degree;
    }

    EdgeWeight getMaxUnweightedDegree() {
        EdgeWeight deg = 0;
        for (NodeID node : this->nodes()) {
            EdgeWeight deg_node = get_first_invalid_edge(node) -
                                  get_first_edge(node);
            if (deg_node > deg) {
                deg = deg_node;
            }
        }
        return deg;
    }

    void computeDegrees() {
        if (m_degrees_computed)
            return;

        m_max_degree = 0;
        m_min_degree = UNDEFINED_EDGE;

        EdgeWeight cur_degree = 0;
        for (NodeID node : this->nodes()) {
            cur_degree = 0;
            for (EdgeID e : edges_of(node)) {
                cur_degree += getEdgeWeight(e);
            }
            m_degree[node] = cur_degree;

            // set min and max degrees for quick access
            if (cur_degree < m_min_degree) {
                m_min_degree = cur_degree;
            }

            if (cur_degree > m_max_degree) {
                m_max_degree = cur_degree;
            }
        }
        m_degrees_computed = true;
    }

    EdgeWeight sumOfEdgeWeights() {
        if (!m_degrees_computed) {
            computeDegrees();
        }

        EdgeWeight sum_weights = 0;
        for (NodeID n : nodes()) {
            sum_weights += m_degree[n];
        }
        return sum_weights;
    }

    EdgeWeight containedVertices(size_t) {
        return 0;
    }

    EdgeWeight getCurrentPosition(size_t) {
        return 0;
    }

    EdgeWeight getMinDegree() {
        if (!m_degrees_computed) {
            computeDegrees();
        }

        return m_min_degree;
    }

    EdgeWeight getPercentile(double perc) {
        if (!m_degrees_computed) {
            computeDegrees();
        }

        std::vector<EdgeWeight> deg = m_degree;

        size_t el = static_cast<double>(deg.size()) * perc;

        std::nth_element(deg.begin(), deg.begin() + el, deg.end());

        return deg[el];
    }

    void setNodeInCut(NodeID node, bool incut) {
        graphref->m_nodes[node].in_cut = incut;
    }

    bool getNodeInCut(NodeID node) const {
        return graphref->m_nodes[node].in_cut;
    }

    std::pair<NodeID, EdgeWeight> getEdge(NodeID, EdgeID edge) {
        return getEdge(edge);
    }

    std::pair<NodeID, EdgeWeight> getEdge(EdgeID edge) {
        return std::make_pair(getEdgeTarget(edge), getEdgeWeight(edge));
    }

    EdgeWeight getEdgeWeight(NodeID, EdgeID edge) {
        return getEdgeWeight(edge);
    }

    EdgeWeight getEdgeWeight(EdgeID edge) const {
#ifdef NDEBUG
        return graphref->m_edges[edge].weight;
#else
        return graphref->m_edges.at(edge).weight;
#endif
    }

    void setEdgeWeight(EdgeID edge, EdgeWeight weight) {
#ifdef NDEBUG
        graphref->m_edges[edge].weight = weight;
#else
        graphref->m_edges.at(edge).weight = weight;
#endif
    }

    NodeWeight getNodeWeight(NodeID) const {
        assert(false);
        return 0;
    }

    void setNodeWeight(NodeID, NodeWeight) {
        assert(false);
    }

    NodeID getEdgeTarget(NodeID, EdgeID edge) {
        return getEdgeTarget(edge);
    }

    NodeID getEdgeTarget(EdgeID edge) const {
#ifdef NDEBUG
        return graphref->m_edges[edge].target;
#else
        return graphref->m_edges.at(edge).target;
#endif
    }

    EdgeID find_reverse_edge(EdgeID e) {
        EdgeID e_rev = -1;
        NodeID src = getEdgeSource(e);
        NodeID tgt = getEdgeTarget(e);
        for (EdgeID re : edges_of(tgt)) {
            if (getEdgeTarget(re) == src) {
                e_rev = re;
                break;
            }
        }
        return e_rev;
    }

    EdgeRatingType getEdgeRating(EdgeID) const {
        assert(0);
        return 0;
    }

    void setEdgeRating(EdgeID, EdgeRatingType) {
        assert(0);
    }

    int * UNSAFE_metis_style_xadj_array();
    int * UNSAFE_metis_style_adjncy_array();

    int * UNSAFE_metis_style_vwgt_array();
    int * UNSAFE_metis_style_adjwgt_array();

    int build_from_metis(int n, int* xadj, int* adjncy);
    int build_from_metis_weighted(int n, int* xadj, int* adjncy,
                                  int* vwgt, int* adjwgt);

    // void set_node_queue_index(NodeID node, Count queue_index);
    // Count get_node_queue_index(NodeID node);

    graph_access copy();

 private:
    void setGraph(basicGraph* graphref_new) {
        graphref = graphref_new;
        m_degrees_computed = false;
    }

    basicGraph* graphref;
    bool m_degrees_computed;
    unsigned int m_partition_count;
    EdgeWeight m_max_degree;
    EdgeWeight m_min_degree;
    PartitionID m_separator_block_ID;
    std::vector<EdgeWeight> m_degree;
};

/* graph build methods */

inline PartitionID graph_access::getSeparatorBlock() {
    return m_separator_block_ID;
}

inline void graph_access::setSeparatorBlock(PartitionID id) {
    m_separator_block_ID = id;
}

inline PartitionID graph_access::getPartitionIndex(NodeID node) const {
#ifdef NDEBUG
    return graphref->m_refinement_node_props[node].partitionIndex;
#else
    return graphref->m_refinement_node_props.at(node).partitionIndex;
#endif
}

inline void graph_access::setPartitionIndex(NodeID node, PartitionID id) {
#ifdef NDEBUG
    graphref->m_refinement_node_props[node].partitionIndex = id;
#else
    graphref->m_refinement_node_props.at(node).partitionIndex = id;
#endif
}

inline int* graph_access::UNSAFE_metis_style_xadj_array() {
    int* xadj = new int[graphref->number_of_nodes() + 1];

    for (NodeID n : nodes()) {
        xadj[n] = graphref->m_nodes[n].firstEdge;
    }

    xadj[graphref->number_of_nodes()] =
        graphref->m_nodes[graphref->number_of_nodes()].firstEdge;
    return xadj;
}

inline int* graph_access::UNSAFE_metis_style_adjncy_array() {
    int* adjncy = new int[graphref->number_of_edges()];

    for (EdgeID e : this->edges()) {
        adjncy[e] = graphref->m_edges[e].target;
    }

    return adjncy;
}

inline int* graph_access::UNSAFE_metis_style_vwgt_array() {
    int* vwgt = new int[graphref->number_of_nodes()];

    return vwgt;
}

inline int* graph_access::UNSAFE_metis_style_adjwgt_array() {
    int* adjwgt = new int[graphref->number_of_edges()];

    for (EdgeID e : this->edges()) {
        adjwgt[e] = static_cast<int>(graphref->m_edges[e].weight);
    }

    return adjwgt;
}

inline int graph_access::build_from_metis(int n, int* xadj, int* adjncy) {
    graphref = new basicGraph();
    start_construction(n, xadj[n]);

    for (unsigned i = 0; i < (unsigned)n; i++) {
        NodeID node = new_node();
        setNodeWeight(node, 1);
        setPartitionIndex(node, 0);

        for (unsigned e = xadj[i]; e < (unsigned)xadj[i + 1]; e++) {
            EdgeID e_bar = new_edge(node, adjncy[e]);
            setEdgeWeight(e_bar, 1);
        }
    }

    finish_construction();
    return 0;
}

inline int graph_access::build_from_metis_weighted(int n, int* xadj,
                                                   int* adjncy, int* vwgt,
                                                   int* adjwgt) {
    graphref = new basicGraph();
    start_construction(n, xadj[n]);

    for (unsigned i = 0; i < (unsigned)n; i++) {
        NodeID node = new_node();
        setNodeWeight(node, vwgt[i]);
        setPartitionIndex(node, 0);

        for (unsigned e = xadj[i]; e < (unsigned)xadj[i + 1]; e++) {
            EdgeID e_bar = new_edge(node, adjncy[e]);
            setEdgeWeight(e_bar, adjwgt[e]);
        }
    }

    finish_construction();
    return 0;
}

inline graph_access graph_access::copy() {
    graph_access G_bar;
    G_bar.start_construction(number_of_nodes(), number_of_edges());

    for (NodeID node : this->nodes()) {
        NodeID shadow_node = G_bar.new_node();
        for (EdgeID e : this->edges_of(node)) {
            NodeID target = getEdgeTarget(e);
            EdgeID shadow_edge = G_bar.new_edge(shadow_node, target);
            G_bar.setEdgeWeight(shadow_edge, getEdgeWeight(e));
        }
    }

    G_bar.finish_construction();
    return G_bar;
}

[[maybe_unused]] static std::string toStringWeighted(
    graphAccessPtr G) {
    std::ostringstream oss;

    for (NodeID n : G->nodes()) {
        oss << n << ": [";
        for (EdgeID e : G->edges_of(n)) {
            oss << G->getEdgeTarget(e) << "(" << G->getEdgeWeight(e) << ")";
            if (e < G->get_first_invalid_edge(n) - 1) {
                oss << ",";
            }
        }
        oss << "] \n";
    }
    return oss.str();
}

[[maybe_unused]] static std::ostream& operator << (
    std::ostream& os, graphAccessPtr G) {
    // return os << toStringUnweighted(G);
    return os << toStringWeighted(G);
    // return os << toStringCompact(G);
}
