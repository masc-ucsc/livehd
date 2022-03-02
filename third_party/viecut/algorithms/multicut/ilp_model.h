/******************************************************************************
 * ilp_model.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2017 Alexandra Henzinger <ahenz@stanford.edu>
 * Copyright (C) 2017-2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gurobi_c++.h" // NOLINT

#include "algorithms/multicut/multicut_problem.h"
#include "common/configuration.h"
#include "tools/timer.h"

class ilp_model {
 public:
    std::tuple<std::vector<NodeID>, EdgeWeight, bool> computeIlp(
        problemPointer problem,
        const std::vector<NodeID>& presets,
        size_t num_terminals,
        bool parallel,
        size_t thread_id) {
        try {
            mutableGraphPtr graph = problem->graph;
            timer ilp_timer;
            GRBModel model = GRBModel(ilp_model::env);

            NodeID max_weight = 0;
            NodeID max_id = 0;
            for (size_t i = 0; i < problem->terminals.size(); ++i) {
                NodeID v = problem->terminals[i].position;
                if (graph->getWeightedNodeDegree(v) > max_weight) {
                    max_weight = graph->getWeightedNodeDegree(v);
                    max_id = i;
                }
            }

            std::vector<std::vector<GRBVar> > nodes(num_terminals);
            std::vector<GRBVar> edges(graph->m() / 2);

            for (auto& v : nodes) {
                v.resize(graph->n());
            }

            model.set(GRB_StringAttr_ModelName, "Partition");
            model.set(GRB_DoubleParam_MIPGap, 0);
            model.set(GRB_IntParam_LogToConsole, 0);
            if (!parallel) {
                model.set(GRB_IntParam_Threads, 1);
            } else {
                size_t threads = configuration::getConfig()->threads;
                model.set(GRB_IntParam_Threads, threads);
                if (!configuration::getConfig()->disable_cpu_affinity) {
                    cpu_set_t all_cores;
                    CPU_ZERO(&all_cores);
                    for (size_t i = 0; i < threads; ++i) {
                        CPU_SET(i, &all_cores);
                    }

                    sched_setaffinity(0, sizeof(cpu_set_t), &all_cores);
                }
            }
            model.set(GRB_IntParam_PoolSearchMode, 0);
            model.set(GRB_DoubleParam_TimeLimit,
                      configuration::getConfig()->ilpTime);
            // Set decision variables for nodes
            for (size_t q = 0; q < num_terminals; q++) {
                GRBLinExpr nodeTot = 0;
                for (NodeID i = 0; i < graph->n(); i++) {
                    if (presets[i] < num_terminals) {
                        bool isCurrent = (presets[i] == q);
                        double f = isCurrent ? 1.0 : 0.0;
                        nodes[q][i] = model.addVar(f, f, 0, GRB_BINARY);
                        nodes[q][i].set(GRB_DoubleAttr_Start, f);
                    } else {
                        nodes[q][i] = model.addVar(0.0, 1.0, 0, GRB_BINARY);
                        nodes[q][i].set(GRB_DoubleAttr_Start, (max_id == q));
                    }
                }
            }

            size_t j = 0;
            // Decision variables for edges
            GRBLinExpr edgeTot = 0;
            for (NodeID n : graph->nodes()) {
                for (EdgeID e : graph->edges_of(n)) {
                    auto [t, w] = graph->getEdge(n, e);
                    if (n > t) {
                        bool terminalIncident = (presets[n] < num_terminals ||
                                                 presets[t] < num_terminals);

                        edges[j] = model.addVar(0.0, 1.0, w, GRB_BINARY);
                        // there is no edge between terminals, we mark edges
                        // that are incident to non-maximal weight terminal
                        double start =
                            (terminalIncident && presets[n] != max_id
                             && presets[t] != max_id) ? 1.0 : 0.0;

                        edges[j].set(GRB_DoubleAttr_Start, start);
                        for (size_t q = 0; q < num_terminals; q++) {
                            GRBLinExpr c = nodes[q][n] - nodes[q][t];
                            // Add constraint: valid partiton
                            std::string v =
                                "valid part on edge " + std::to_string(j)
                                + " between " + std::to_string(n)
                                + " and " + std::to_string(t);
                            std::string w =
                                "neg valid part on edge "
                                + std::to_string(j) + " between "
                                + std::to_string(n) + " and "
                                + std::to_string(t);
                            model.addConstr(edges[j], GRB_GREATER_EQUAL, c, v);
                            model.addConstr(edges[j], GRB_GREATER_EQUAL, -c, w);
                        }
                        j++;
                    }
                }
            }

            // Add constraint: sum of all decision variables for 1 node is 1
            for (size_t i = 0; i < graph->n(); i++) {
                GRBLinExpr sumCons = 0;
                for (size_t q = 0; q < num_terminals; q++) {
                    sumCons += nodes[q][i];
                }
                model.addConstr(sumCons, GRB_EQUAL, 1);
            }

            model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);
            // Optimize model
            model.optimize();

            std::vector<NodeID> result(graph->n());
            // if solution is found
            if (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL ||
                model.get(GRB_IntAttr_Status) == GRB_TIME_LIMIT) {
                // set partition
                for (PartitionID q = 0; q < num_terminals; q++) {
                    for (size_t i = 0; i < graph->n(); i++) {
                        auto v = nodes[q][i].get(GRB_DoubleAttr_X);
                        if (v == 1) {
                            result[i] = q;
                        }
                    }
                }
            } else {
            }

            bool reIntroduce = false;
            if (model.get(GRB_IntAttr_Status) == GRB_TIME_LIMIT) {
                reIntroduce = true;
            }
            EdgeWeight wgt = std::lround(model.get(GRB_DoubleAttr_ObjVal));

            if (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL ||
                model.get(GRB_IntAttr_Status) == GRB_TIME_LIMIT) {
                bool optimal = (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL);

            }

            if (parallel) {
                if (!configuration::getConfig()->disable_cpu_affinity) {
                    cpu_set_t my_id;
                    CPU_ZERO(&my_id);
                    CPU_SET(thread_id, &my_id);
                    sched_setaffinity(0, sizeof(cpu_set_t), &my_id);
                }
            }

            return std::make_tuple(result, wgt, reIntroduce);
        } catch (GRBException e) {
            exit(1);
        }
    }

 private:
    GRBEnv env;
};
