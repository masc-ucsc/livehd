/******************************************************************************
 * unit_flow.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <utility>
#include <vector>

#include "common/definitions.h"
#include "data_structure/flow_graph.h"
#include "data_structure/graph_access.h"

class unit_flow {
 public:
    static constexpr bool debug = false;
    unit_flow() { }
    virtual ~unit_flow() { }

    void init(flow_graph fg, const std::vector<EdgeWeight>& delta_src,
              EdgeWeight unit_cap, NodeID max_height, FlowType w) {
        m_f.resize(fg.number_of_nodes());
        m_fg = &fg;
        m_delta_src = delta_src;
        m_unit_cap = unit_cap;
        m_max_height = max_height;
        m_w = w;
        m_current_edge.resize(fg.number_of_nodes(), 0);
        m_height.resize(fg.number_of_nodes(), 0);
        for (NodeID n = 0; n < fg.number_of_nodes(); ++n) {
            m_f[n] = delta_src[n];
            if (delta_src[n] > (EdgeWeight)fg.getCapacity(n)) {
                Q.push(std::make_pair(0, n));
            }
        }

        pushCtr = 0;
        relabelCtr = 0;
        done = false;
    }

    void run() {
        if (Q.empty())
            return;

        m_v = Q.top().second;
        Q.pop();
        while (!done) {
            pushRelabel(m_v);
        }

        std::vector<size_t> heights(m_max_height + 1, 0);

        for (size_t i = 0; i < m_height.size(); ++i) {
            heights[m_height[i]]++;
        }
    }

    FlowType excess(NodeID v) {
        return std::max(m_f[v] - m_fg->getCapacity(v), (FlowType)0);
    }

    FlowType flow(NodeID v) {
        return m_f[v];
    }

    NodeID height(NodeID v) {
        return m_height[v];
    }

 private:
    struct cmp {
        bool operator () (const std::pair<EdgeWeight, NodeID>& p1,
                          const std::pair<EdgeWeight, NodeID>& p2) {
            return p1.first > p2.first;
        }
    };

    bool applicable(NodeID v, EdgeID e) {
        NodeID tgt = m_fg->getEdgeTarget(v, e);
        return (excess(v) > 0)
               && (m_height[v] == m_height[tgt] + 1)
               && (m_fg->getEdgeCapacity(v, e) > m_fg->getEdgeFlow(v, e))
               && (m_f[tgt] < m_w * m_fg->getCapacity(tgt));
    }

    void push(NodeID v, EdgeID e) {
        ++pushCtr;
        NodeID tgt = m_fg->getEdgeTarget(v, e);
        FlowType flow = m_fg->getEdgeFlow(v, e);
        assert(m_f[tgt] < m_w * m_fg->getCapacity(tgt));
        EdgeWeight supply = std::min(excess(v), std::min(
                                         m_fg->getEdgeCapacity(v, e) - flow,
                                         (m_w * m_fg->getCapacity(tgt))
                                         - m_f[tgt]));

        EdgeID rev = m_fg->getReverseEdge(v, e);
        m_fg->setEdgeFlow(v, e, flow + supply);
        m_fg->setEdgeFlow(tgt, rev, m_fg->getEdgeFlow(tgt, rev) - supply);
        m_f[v] -= supply;
        bool active_before = active(tgt);
        m_f[tgt] += supply;

        if (!active(v)) {
            if (!Q.empty()) {
                m_v = Q.top().second;
                Q.pop();
            } else {
                done = true;
            }
        }

        if (!active_before && active(tgt)) {
            Q.push(std::make_pair(m_height[tgt], tgt));

            if (done) {
                m_v = Q.top().second;
                Q.pop();
                done = false;
            }
        }
    }

    bool active(NodeID v) {
        return m_height[v] < m_max_height && excess(v) > 0;
    }

    void relabel(NodeID v) {
        ++relabelCtr;
        assert(active(v));

        for (EdgeID e : m_fg->edges_of(v)) {
            assert(m_fg->getEdgeFlow(v, e) == m_fg->getEdgeCapacity(v, e) ||
                   m_height[v] <= m_height[m_fg->getEdgeTarget(v, e)] ||
                   m_fg->getEdgeCapacity(v, e) == 0);
        }
        ++m_height[v];
        if (m_height[v] < m_max_height) {
            Q.push(std::make_pair(m_height[v], v));
        }

        if (!Q.empty()) {
            m_v = Q.top().second;
            Q.pop();
        } else {
            done = true;
        }
    }

    void pushRelabel(NodeID v) {
        EdgeID e = m_current_edge[v];
        if (m_fg->get_first_invalid_edge(v)) {
            if (applicable(v, e)) {
                push(v, e);
            } else {
                if (m_height[v] && e + 1 < m_fg->get_first_invalid_edge(v)) {
                    ++m_current_edge[v];
                } else {
                    relabel(v);
                    m_current_edge[v] = 0;
                }
            }
        } else {
            Q.pop();
        }
    }

 private:
    std::priority_queue<std::pair<EdgeWeight, NodeID>,
                        std::vector<std::pair<EdgeWeight, NodeID> >,
                        cmp> Q;
    std::vector<EdgeID> m_current_edge;
    std::vector<NodeID> m_height;
    std::vector<FlowType> m_f;
    flow_graph* m_fg;
    std::vector<EdgeWeight> m_delta_src;
    EdgeWeight m_unit_cap;
    NodeID m_max_height;
    FlowType m_w;
    NodeID m_v;
    bool done;

    size_t pushCtr, relabelCtr;
};
