/******************************************************************************
 * dynamic_minimum.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2020 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <tuple>
#include <unordered_set>
#include <vector>

#ifdef PARALLEL
#include "parallel/algorithm/parallel_cactus.h"
#else
#include "algorithms/global_mincut/cactus/cactus_mincut.h"
#endif

#include "algorithms/global_mincut/dynamic/cactus_path.h"
#include "common/definitions.h"
#include "data_structure/mutable_graph.h"
#include "tools/timer.h"

class dynamic_mincut {
 private:
    bool verbose;
    mutableGraphPtr original_graph;
    mutableGraphPtr out_cactus;
    EdgeWeight current_cut;
    size_t flow_problem_id;
    size_t max_cache_size = 1000;
    size_t callsOfStaticAlgorithm;

    size_t numCachedMincuts;
    EdgeWeight lowestCachedMincut;
    std::vector<mutableGraphPtr> cachedCactus;
    std::vector<std::vector<std::tuple<NodeID, NodeID, EdgeWeight> > >
    cachedInserts;
    std::vector<bool> currentlyCaching;
    push_relabel<true, false> pr;

#ifdef PARALLEL
    parallel_cactus<mutableGraphPtr> cactus;
#else
    cactus_mincut<mutableGraphPtr> cactus;
#endif

 public:
    dynamic_mincut() {
        verbose = configuration::getConfig()->verbose;
    }

    ~dynamic_mincut() { }

    EdgeWeight initialize(mutableGraphPtr graph) {
        timer t;
        auto [cut, outgraph, balanced] = cactus.findAllMincuts(graph);
        numCachedMincuts = 0;
        callsOfStaticAlgorithm = 1;
        lowestCachedMincut = UNDEFINED_EDGE;
        original_graph = graph;
        out_cactus = outgraph;
        current_cut = cut;
        flow_problem_id = random_functions::next();
        return cut;
    }

    void checkCacheAndRecompute() {
        EdgeWeight mincut = UNDEFINED_NODE;
        if (lowestCachedMincut != UNDEFINED_EDGE) {
            noi_minimum_cut<mutableGraphPtr> noi;
            mincut = noi.perform_minimum_cut(original_graph);
        }

        if (mincut == lowestCachedMincut &&
            2 * cachedInserts.size() < cachedCactus[mincut]->n()) {
            buildCactusFromCache(mincut);
        } else {
            auto [cut, outg, b] = cactus.findAllMincuts(
                original_graph, mincut);
            callsOfStaticAlgorithm++;
            out_cactus = outg;
            current_cut = cut;
        }
    }

    EdgeWeight addEdge(NodeID s, NodeID t, EdgeWeight w) {
        timer timer;
        NodeID sCactusPos = out_cactus->getCurrentPosition(s);
        NodeID tCactusPos = out_cactus->getCurrentPosition(t);
        original_graph->new_edge_order(s, t, w);
        cacheEdge(s, t, w);
        if (sCactusPos != tCactusPos) {
            if (current_cut == 0) {
                if (out_cactus->n() == 2) {
                    checkCacheAndRecompute();
                } else {
                    out_cactus->contractVertexSet({ sCactusPos, tCactusPos });
                }
            } else {
                auto vtxset = cactus_path::findPath(out_cactus, sCactusPos,
                                                    tCactusPos, current_cut);
                if (vtxset.size() == out_cactus->n()) {
                    checkCacheAndRecompute();
                } else {
                    contractVertexSet(out_cactus, vtxset);
                }
            }
        }
        if (configuration::getConfig()->find_most_balanced_cut) {
            most_balanced_minimum_cut<mutableGraphPtr> mb;
            mb.findCutFromCactus(out_cactus, current_cut, original_graph);
        }
        return current_cut;
    }

    void buildCactusFromCache(EdgeWeight mincut) {
        numCachedMincuts--;
        currentlyCaching[mincut] = false;
        if (numCachedMincuts == 0) {
            lowestCachedMincut = UNDEFINED_EDGE;
        } else {
            for (size_t i = mincut + 1; i < currentlyCaching.size(); ++i) {
                if (currentlyCaching[i]) {
                    lowestCachedMincut = i;
                    break;
                }
            }

            if (cachedInserts[mincut].size()
                + cachedInserts[lowestCachedMincut].size() > max_cache_size) {
                if (numCachedMincuts > 0) {
                    // delete all larger mincut cacti by shrinking the vector
                    cachedCactus.resize(mincut + 1);
                    cachedInserts.resize(mincut + 1);
                    currentlyCaching.resize(mincut + 1);
                    numCachedMincuts = 0;
                }
                lowestCachedMincut = UNDEFINED_EDGE;
            } else {
                for (auto x : cachedInserts[mincut]) {
                    cachedInserts[lowestCachedMincut].emplace_back(x);
                }
            }
        }

        for (auto [s, t, w] : cachedInserts[mincut]) {
            NodeID sCactusPos = cachedCactus[mincut]->getCurrentPosition(s);
            NodeID tCactusPos = cachedCactus[mincut]->getCurrentPosition(t);
            if (sCactusPos != tCactusPos) {
                auto vtxset = cactus_path::findPath(
                    cachedCactus[mincut], sCactusPos, tCactusPos, current_cut);
                if (vtxset.size() == cachedCactus[mincut]->n()) {
                    auto [cut, outg, b] = cactus.findAllMincuts(original_graph);
                    callsOfStaticAlgorithm++;
                    out_cactus = outg;
                    current_cut = cut;
                    return;
                } else {
                    contractVertexSet(cachedCactus[mincut], vtxset);
                }
            }
        }
        out_cactus = cachedCactus[mincut];
        current_cut = mincut;
    }

    void contractVertexSet(
        mutableGraphPtr cactus, const std::unordered_set<NodeID>& vtxset) {
        // if one vertex has high degree and all others don't, it is faster
        // to explicitly contract others into this high degree vertex instead of
        // standard set contraction (also check that no vertex is empty so we
        // have handles on the vertices)
        bool alternativeContract = true;

        NodeID high_degree = UNDEFINED_NODE;
        size_t numNonlow = 0;

        for (auto v : vtxset) {
            if (cactus->numContainedVertices(v) == 0) {
                alternativeContract = false;
                break;
            }
            if (cactus->getUnweightedNodeDegree(v) > 100) {
                if (high_degree != UNDEFINED_NODE) {
                    alternativeContract = false;
                    break;
                } else {
                    high_degree = v;
                }
            }

            if (cactus->getUnweightedNodeDegree(v) > 10) {
                numNonlow++;
            }
            if (numNonlow > 1) {
                alternativeContract = false;
                break;
            }
        }

        if (alternativeContract && high_degree != UNDEFINED_NODE) {
            NodeID high_origid = cactus->containedVertices(high_degree)[0];
            std::vector<NodeID> orig_ids;
            for (auto v : vtxset) {
                if (v != high_degree) {
                    orig_ids.emplace_back(cactus->containedVertices(v)[0]);
                }
            }

            for (auto v : orig_ids) {
                NodeID s = cactus->getCurrentPosition(high_origid);
                NodeID t = cactus->getCurrentPosition(v);
                EdgeID conn_edge = UNDEFINED_EDGE;
                for (EdgeID e : cactus->edges_of(t)) {
                    if (cactus->getEdgeTarget(t, e) == s) {
                        conn_edge = cactus->getReverseEdge(t, e);
                        break;
                    }
                }
                if (conn_edge == UNDEFINED_EDGE) {
                    cactus->contractSparseTargetNoEdge(s, t);
                } else {
                    cactus->contractEdgeSparseTarget(s, conn_edge);
                }
            }
        } else {
            cactus->contractVertexSet(vtxset);
        }
    }

    EdgeWeight removeEdge(NodeID s, NodeID t) {
        timer timer;
        EdgeID eToT = UNDEFINED_EDGE;
        for (EdgeID e : original_graph->edges_of(s)) {
            if (original_graph->getEdgeTarget(s, e) == t) {
                eToT = e;
                break;
            }
        }

        if (eToT == UNDEFINED_EDGE) {
            return current_cut;
        }

        EdgeWeight wgt = original_graph->getEdgeWeight(s, eToT);
        original_graph->deleteEdge(s, eToT);
        NodeID sCactusPos = out_cactus->getCurrentPosition(s);
        NodeID tCactusPos = out_cactus->getCurrentPosition(t);

        if (wgt == 0) {
            return current_cut;
        }

        if (current_cut == 0) {
            return current_cut;
        }

        if (sCactusPos != tCactusPos) {
            /* auto [cut, outg, b] = cactus.findAllMincuts(original_graph);
            out_cactus = outg;
            current_cut = cut;   */
            putIntoCache(out_cactus, current_cut);
            size_t fpid = flow_problem_id++;
            recursive_cactus<mutableGraphPtr> rc;
            size_t flow = pr.solve_max_flow_min_cut(
                original_graph, { s, t }, 0, false, current_cut, fpid).first;

            auto new_g = rc.decrementalRebuild(original_graph, s, flow, fpid);
            current_cut = flow;
            out_cactus = new_g;
        } else {
            size_t fp = flow_problem_id++;
            auto [flow, sourceset] = pr.solve_max_flow_min_cut(
                original_graph, { s, t }, 0, false,
                current_cut, fp);
            if (static_cast<EdgeWeight>(flow) < current_cut) {
                putIntoCache(out_cactus, current_cut);
                recursive_cactus<mutableGraphPtr> rc;
                auto new_g = rc.decrementalRebuild(original_graph, s, flow, fp);
                current_cut = flow;
                out_cactus = new_g;
            }
        }

        if (configuration::getConfig()->find_most_balanced_cut) {
            most_balanced_minimum_cut<mutableGraphPtr> mb;
            mb.findCutFromCactus(out_cactus, current_cut, original_graph);
        }
        return current_cut;
    }

    mutableGraphPtr getOriginalGraph() {
        return original_graph;
    }

    mutableGraphPtr getCurrentCactus() {
        return out_cactus;
    }

    size_t getCallsOfStaticAlgorithm() {
        return callsOfStaticAlgorithm;
    }

    EdgeWeight getCurrentCut() {
        return current_cut;
    }

    void putIntoCache(mutableGraphPtr cactusToCache, EdgeWeight cactusCut) {
        numCachedMincuts++;
        if (cactusCut < lowestCachedMincut) {
            lowestCachedMincut = cactusCut;
        }

        if (cachedCactus.size() < cactusCut + 1) {
            cachedCactus.resize(cactusCut + 1);
            cachedInserts.resize(cactusCut + 1);
            currentlyCaching.resize(cactusCut + 1, false);
        }

        cachedCactus[cactusCut] = cactusToCache;
        cachedInserts[cactusCut].clear();
        currentlyCaching[cactusCut] = true;
    }

    void cacheEdge(NodeID s, NodeID t, EdgeWeight wgt) {
        if (numCachedMincuts > 0
            && cachedInserts[lowestCachedMincut].size() <= max_cache_size) {
            cachedInserts[lowestCachedMincut].emplace_back(s, t, wgt);
        } else {
            if (numCachedMincuts > 0) {
                numCachedMincuts--;
                currentlyCaching[lowestCachedMincut] = false;
                if (numCachedMincuts > 0) {
                    // delete all larger mincut cacti by shrinking the vector
                    cachedCactus.resize(lowestCachedMincut + 1);
                    cachedInserts.resize(lowestCachedMincut + 1);
                    currentlyCaching.resize(lowestCachedMincut + 1);
                    numCachedMincuts = 0;
                }
                lowestCachedMincut = UNDEFINED_EDGE;
            }
        }
    }
};
