/******************************************************************************
 * push_relabel.h
 *
 * Source of VieCut.
 *
 * Adapted from KaHIP.
 *
 ******************************************************************************
 * Copyright (C) 2013-2015 Christian Schulz <christian.schulz@univie.ac.at>
 * Copyright (C) 2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include "algorithms/misc/graph_algorithms.h"
#include "common/configuration.h"
#include "common/definitions.h"
#include "data_structure/mutable_graph.h"
#include "data_structure/priority_queues/maxNodeHeap.h"
#include "tools/random_functions.h"
#include "tools/timer.h"

const int WORK_OP_RELABEL = 9;
const double GLOBAL_UPDATE_FRQ = 0.51;
const int WORK_NODE_TO_EDGES = 4;

template <bool limited = false, bool parallel_flows = false>
class push_relabel {
 public:
    push_relabel() : m_current_iteration(0) { }
    virtual ~push_relabel() { }

 private:
    void init(mutableGraphPtr G,
              std::vector<NodeID> sources,
              NodeID source) {
        m_current_iteration++;
        if (m_excess.size() < G->n()) {
            m_excess.resize(G->n(), 0);
            m_distance.resize(G->n(), 0);
            m_active.resize(G->n(), false);
            m_count.resize(2 * G->n(), 0);
            m_bfstouched.resize(G->n(), false);
        }
        std::fill(m_excess.begin(), m_excess.end(), 0);
        std::fill(m_distance.begin(), m_distance.end(), 0);
        std::fill(m_active.begin(), m_active.end(), false);
        std::fill(m_count.begin(), m_count.end(), 0);
        std::fill(m_bfstouched.begin(), m_bfstouched.end(), false);
        m_Q.reset();
        m_count[0] = G->number_of_nodes() - 1;
        m_count[G->number_of_nodes()] = 1;

        NodeID flow_source = sources[source];
        m_distance[flow_source] = G->number_of_nodes();

        for (NodeID n : sources) {
            m_active[n] = true;
        }

        for (EdgeID e : G->edges_of(flow_source)) {
            m_excess[flow_source] += G->getEdgeWeight(flow_source, e);
            push(flow_source, e, G->n());
        }
    }

    // perform a backward bfs in the residual starting at the sink
    // to update distance labels
    template <bool initial = false>
    void global_relabeling(std::vector<NodeID> sources, NodeID source) {
        std::queue<NodeID> Q;
        NodeID flow_source = sources[source];

        std::fill(m_bfstouched.begin(), m_bfstouched.end(), false);
        size_t depthPR = configuration::getConfig()->depthOfPartialRelabeling;

        if constexpr (limited && initial) {
            size_t fillValue = depthPR + 1;
            std::fill(m_distance.begin(), m_distance.end(), fillValue);
            m_count[0] = 0;
            m_count[fillValue] = m_G->n() - 1;
        } else {
            for (NodeID n : m_G->nodes()) {
                m_distance[n] = std::max(m_distance[n], m_G->number_of_nodes());
            }
        }

        for (NodeID sink : sources) {
            if (sink == flow_source) {
                m_distance[sink] = m_G->n();
                continue;
            }

            Q.push(sink);
            m_bfstouched[sink] = true;
            m_count[m_distance[sink]]--;
            m_count[0]++;
            m_distance[sink] = 0;
        }

        if constexpr (limited && initial) {
            if (depthPR + 1 <= 1) return;
        }

        m_bfstouched[flow_source] = true;

        NodeID node = 0;
        while (!Q.empty()) {
            node = Q.front();
            Q.pop();

            for (EdgeID e : m_G->edges_of(node)) {
                NodeID target = m_G->getEdgeTarget(node, e);
                if (m_bfstouched[target]) continue;

                EdgeID rev_e = m_G->getReverseEdge(node, e);
                if (initial || (m_G->getEdgeWeight(target, rev_e) -
                                getEdgeFlow(target, rev_e)) > 0) {
                    m_count[m_distance[target]]--;
                    m_distance[target] = m_distance[node] + 1;
                    m_count[m_distance[target]]++;
                    if constexpr (!(limited && initial)) {
                        Q.push(target);
                    } else {
                        if (m_distance[target] < depthPR) {
                            Q.push(target);
                        }
                    }
                    m_bfstouched[target] = true;
                }
            }
        }
    }

    // push flow from source to target if possible
    void push(NodeID source, EdgeID e, NodeID sourceDistance) {
        m_pushes++;
        NodeID target = m_G->getEdgeTarget(source, e);
        if (sourceDistance <= m_distance[target]) [[likely]] return;

        FlowType capacity = m_G->getEdgeWeight(source, e);
        FlowType flow = getEdgeFlow(source, e);
        FlowType amount = std::min(capacity - flow, m_excess[source]);

        if (amount == 0) return;

        m_actual_pushes++;

        EdgeID rev_e = m_G->getReverseEdge(source, e);
        addEdgeFlow(source, e, amount);
        addEdgeFlow(target, rev_e, (-1) * amount);

        m_excess[source] -= amount;
        m_excess[target] += amount;

        if constexpr (limited) {
            if (target == m_sink && m_excess[target] >= m_limit) {
                m_limitreached = true;
                return;
            }
        }

        enqueue(target);
    }

    // put a vertex in the FIFO queue of the global_mincut
    void enqueue(NodeID target) {
        if (m_active[target]) return;
        if (m_excess[target] > 0) {
            m_active[target] = true;
            if constexpr (limited) {
                // min heap if limited as first flow faster
                // max heap if not for better asymptotic runtime
                m_Q.insert(target, (-1) * m_distance[target]);
            } else {
                m_Q.insert(target, m_distance[target]);
            }
            // m_Q.push(target);
        }
    }

    // try to push as much excess as possible out of the node node
    void discharge(NodeID node) {
        NodeID nodeDistance = m_distance[node];
        for (EdgeID e : m_G->edges_of(node)) {
            if (m_excess[node] == 0)
                break;

            push(node, e, nodeDistance);
        }

        if (m_excess[node] > 0) {
            if (m_count[m_distance[node]] == 1
                && m_distance[node] < m_G->number_of_nodes()) {
                // hence this layer will be empty after the relabel step
                gap_heuristic(m_distance[node]);
            } else {
                relabel(node);
            }
        }
    }

    // gap heuristic
    void gap_heuristic(NodeID level) {
        m_gaps++;
        for (NodeID node : m_G->nodes()) {
            if (m_distance[node] < level) continue;
            m_count[m_distance[node]]--;
            m_distance[node] = std::max(m_distance[node], m_G->n());
            m_count[m_distance[node]]++;
            enqueue(node);
        }
    }

    // relabel a node with respect to its
    // neighboring nodes
    void relabel(NodeID node) {
        m_work += WORK_OP_RELABEL;
        m_num_relabels++;

        m_count[m_distance[node]]--;
        m_distance[node] = 2 * m_G->number_of_nodes();

        for (EdgeID e : m_G->edges_of(node)) {
            if (m_G->getEdgeWeight(node, e) - getEdgeFlow(node, e) > 0) {
                NodeID target = m_G->getEdgeTarget(node, e);
                m_distance[node] =
                    std::min(m_distance[node], m_distance[target] + 1);
            }
            m_work++;
        }

        m_count[m_distance[node]]++;
        enqueue(node);
    }

    std::vector<NodeID> computeSourceSet(const std::vector<NodeID>& sources,
                                         NodeID curr_source) {
        std::vector<NodeID> source_set;
        // perform bfs starting from source set
        source_set.clear();
        NodeID src = sources[curr_source];

        for (NodeID node : m_G->nodes()) {
            m_bfstouched[node] = false;
        }

        std::queue<NodeID> Q;
        for (NodeID tgt : sources) {
            if (tgt == src)
                continue;

            Q.push(tgt);
            m_bfstouched[tgt] = true;
        }

        while (!Q.empty()) {
            NodeID node = Q.front();
            Q.pop();

            for (EdgeID e : m_G->edges_of(node)) {
                EdgeID rev_e = m_G->getReverseEdge(node, e);

                NodeID edge_source = m_G->getEdgeTarget(node, e);
                FlowType resCap = m_G->getEdgeWeight(edge_source, rev_e)
                                  - getEdgeFlow(edge_source, rev_e);
                if (resCap > 0 && !m_bfstouched[edge_source]) {
                    Q.push(edge_source);
                    m_bfstouched[edge_source] = true;
                }
            }
        }

        std::queue<NodeID> Qsrc;
        Qsrc.push(src);
        source_set.emplace_back(src);
        m_bfstouched[src] = true;
        while (!Qsrc.empty()) {
            NodeID node = Qsrc.front();
            Qsrc.pop();

            for (EdgeID e : m_G->edges_of(node)) {
                NodeID n = m_G->getEdgeTarget(node, e);
                if (!m_bfstouched[n]) {
                    source_set.emplace_back(n);
                    m_bfstouched[n] = true;
                    Qsrc.push(n);
                }
            }
        }

        return source_set;
    }

    void setEdgeFlow(NodeID n, EdgeID e, FlowType f) {
        if constexpr (parallel_flows) {
            edge_flow[n][e] = f;
        } else {
            m_G->setEdgeFlow(n, e, f, m_problemid);
        }
    }

    void addEdgeFlow(NodeID n, EdgeID e, FlowType f) {
        if constexpr (parallel_flows) {
            edge_flow[n][e] += f;
        } else {
            m_G->addEdgeFlow(n, e, f, m_problemid);
        }
    }

    FlowType getEdgeFlow(NodeID n, EdgeID e) {
        if constexpr (parallel_flows) {
            return edge_flow[n][e];
        } else {
            return m_G->getEdgeFlow(n, e, m_problemid);
        }
    }

 public:
    std::vector<NodeID> callable_max_flow(mutableGraphPtr G,
                                          std::vector<NodeID> sources,
                                          NodeID curr_source,
                                          bool compute_source_set) {
        // this exists to be called by std::async.
        // thus, parallel_flows is set to true
        auto source_set = solve_max_flow_min_cut(
            G, sources, curr_source, compute_source_set).second;
        return source_set;
    }

    std::pair<FlowType, std::vector<NodeID> > solve_max_flow_min_cut(
        mutableGraphPtr G,
        std::vector<NodeID> sources,
        NodeID curr_source,
        bool compute_source_set,
        FlowType limit = 0,
        size_t problem_id = random_functions::nextInt(0, UNDEFINED_NODE)) {
        t.restart();
        for (NodeID s : sources) {
            if (s >= G->number_of_nodes()) {
                return std::make_pair(-1, std::vector<NodeID>());
            }
        }

        if (parallel_flows) {
            // if we have parallel flows on the same graphs,
            // we can't use edge flows on the graph.
            // in sequential cases it's faster and flow values
            // might still be useful outside of this algorithm
            edge_flow.clear();
            for (NodeID n : G->nodes()) {
                edge_flow.emplace_back(G->get_first_invalid_edge(n), 0);
            }
        }

        m_G = G;
        m_work = 0;
        m_num_relabels = 0;
        m_gaps = 0;
        m_pushes = 0;
        m_actual_pushes = 0;
        m_global_updates = 1;
        m_limit = limit;
        m_limitreached = false;
        m_problemid = problem_id;

        if constexpr (limited) {
            if (sources.size() != 2) {
            }
            m_sink = curr_source == 1 ? sources[0] : sources[1];
        }

        NodeID src = sources[curr_source];

        init(G, sources, curr_source);
        global_relabeling<true>(sources, curr_source);

        int work_todo = WORK_NODE_TO_EDGES * G->number_of_nodes()
                        + G->number_of_edges();

        // main loop
        while (!m_Q.empty()) {
            // NodeID v = m_Q.front();
            // m_Q.pop();
            NodeID v = m_Q.deleteMax();
            m_active[v] = false;
            discharge(v);

            if constexpr (limited) {
                if (m_limitreached) {
                    return std::make_pair(limit, std::vector<NodeID> { });
                }
            }

            if (m_work > GLOBAL_UPDATE_FRQ * work_todo) {
                global_relabeling(sources, curr_source);
                m_work = 0;
                m_global_updates++;
            }
        }

        FlowType total_flow = 0;
        // return value of flow
        for (NodeID n : sources) {
            if (n != src) {
                total_flow += m_excess[n];
            }
        }

        std::vector<NodeID> source_set;

        if (compute_source_set) {
            source_set = computeSourceSet(sources, curr_source);
        }

        return std::make_pair(total_flow, source_set);
    }

 private:
    std::vector<FlowType> m_excess;
    std::vector<NodeID> m_distance;
    std::vector<bool> m_active;   // store which nodes are in the queue already
    std::vector<int> m_count;
    maxNodeHeap m_Q;
    std::vector<bool> m_bfstouched;
    std::vector<std::vector<FlowType> > edge_flow;
    int m_num_relabels;
    int m_gaps;
    int m_global_updates;
    int m_pushes;
    int m_actual_pushes;
    int m_work;
    int m_current_iteration;
    NodeID m_sink;
    FlowType m_limit;
    bool m_limitreached;
    size_t m_problemid;
    mutableGraphPtr m_G;
    static const bool extended_logs = false;

    timer t;
};
