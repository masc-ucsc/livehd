/******************************************************************************
 * maximum_flow.h
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/
#pragma once

#include <future>
#include <memory>
#include <queue>
#include <unordered_set>
#include <vector>

#include "algorithms/flow/push_relabel.h"
#include "algorithms/multicut/graph_contraction.h"
#include "algorithms/multicut/multicut_problem.h"
#include "common/configuration.h"
#include "common/definitions.h"

class maximum_flow {
 public:
    explicit maximum_flow(std::vector<NodeID> o)
        : original_terminals(o),
          num_threads(configuration::getConfig()->threads) { }

    void maximumSTFlow(problemPointer problem) {
        push_relabel pr;
        auto G = problem->graph;

        std::vector<NodeID> current_terminals;
        for (const auto& t : problem->terminals) {
            current_terminals.emplace_back(t.position);
        }

        auto [flow, isolating_block] =
            pr.solve_max_flow_min_cut(G, current_terminals, 0, true);

        NodeID term0 = problem->terminals[0].original_id;
        NodeID term1 = problem->terminals[1].original_id;

        for (NodeID n : problem->graph->nodes()) {
            G->setPartitionIndex(n, term1);
        }

        for (size_t i = 0; i < original_terminals.size(); ++i) {
            NodeID c =
                G->getCurrentPosition(problem->mapped(original_terminals[i]));
            G->setPartitionIndex(c, i);
        }

        for (NodeID n : isolating_block) {
            G->setPartitionIndex(n, term0);
        }

        problem->upper_bound = flow + problem->deleted_weight;
    }

    std::vector<NodeID> highDistanceVertices(
        problemPointer problem) {
        std::queue<NodeID> Q;
        std::vector<bool> found(problem->graph->n(), false);
        std::vector<NodeID> order;

        for (const auto& t : problem->terminals) {
            found[t.position] = true;
            order.emplace_back(t.position);
            Q.emplace(t.position);
        }

        while (!Q.empty()) {
            NodeID n = Q.front();
            Q.pop();
            for (EdgeID e : problem->graph->edges_of(n)) {
                NodeID t = problem->graph->getEdgeTarget(n, e);
                if (!found[t]) {
                    found[t] = true;
                    Q.emplace(t);
                    order.emplace_back(t);
                }
            }
        }

        return order;
    }

    union_find nonTerminalFlow(problemPointer problem,
                               bool parallel,
                               const std::vector<bool>& active) {
        union_find uf(problem->graph->n());
        std::unordered_set<NodeID> previous;

        std::vector<std::future<std::vector<NodeID> > > futures;
        std::vector<push_relabel<false, true> > prs(
            configuration::getConfig()->random_flows +
            configuration::getConfig()->high_distance_flows);
        size_t pr_id = 0;

        if (!configuration::getConfig()->disable_cpu_affinity) {
            cpu_set_t all_cores;
            CPU_ZERO(&all_cores);
            for (size_t i = 0; i < num_threads; ++i) {
                CPU_SET(i, &all_cores);
            }
            sched_setaffinity(0, sizeof(cpu_set_t), &all_cores);
        }

        for (const auto& t : problem->terminals) {
            previous.insert(t.position);
        }

        for (size_t i = 0; i < configuration::getConfig()->random_flows; ++i) {
            NodeID r = random_functions::nextInt(0, problem->graph->n() - 1);
            EdgeWeight max = problem->graph->getMaxDegree();
            size_t num_tries = 0;
            while (((max >= (problem->graph->getWeightedNodeDegree(r)
                             + num_tries++))
                    || uf.Find(r) != r
                    || previous.count(r) > 0
                    || !active[r])
                   && num_tries < max) {
                r = random_functions::nextInt(0, problem->graph->n() - 1);
            }

            previous.insert(r);
            std::vector<NodeID> terms;
            for (const auto& t : problem->terminals) {
                terms.emplace_back(t.position);
            }
            terms.emplace_back(r);
            size_t num_t = terms.size() - 1;

            if (parallel) {
                futures.emplace_back(
                    std::async(&push_relabel<false, true>::callable_max_flow,
                               &prs[pr_id++],
                               problem->graph, terms, num_t, true));
            } else {
                push_relabel pr;
                auto sourceSet = pr.solve_max_flow_min_cut(
                    problem->graph, terms, num_t, true).second;

                for (const auto& s : sourceSet) {
                    uf.Union(s, r);
                }
            }
        }

        auto hdv = highDistanceVertices(problem);
        size_t num_flows = configuration::getConfig()->high_distance_flows;
        double hd_factor = configuration::getConfig()->high_distance_factor;
        for (size_t i = 0; i < num_flows && hdv.size() > 0; ++i) {
            NodeID last = hdv.size() - 1;
            NodeID idx = random_functions::nextInt(last * hd_factor, last);
            NodeID r = hdv[idx];
            if (uf.Find(r) != r) {
                continue;
            }

            previous.insert(r);
            std::vector<NodeID> terms;
            for (const auto& t : problem->terminals) {
                terms.emplace_back(t.position);
            }
            terms.emplace_back(r);
            push_relabel pr;
            size_t num_t = terms.size() - 1;

            if (parallel) {
                futures.emplace_back(
                    std::async(&push_relabel<false, true>::callable_max_flow,
                               &prs[pr_id++],
                               problem->graph, terms, num_t, true));
            } else {
                push_relabel pr;
                auto sourceSet = pr.solve_max_flow_min_cut(
                    problem->graph, terms, num_t, true).second;

                for (const auto& s : sourceSet) {
                    uf.Union(s, r);
                }
            }
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            auto& t = futures[i];
            const auto& vec = t.get();
            NodeID head = vec[0];
            for (const auto& n : vec) {
                uf.Union(n, head);
            }
        }

        return uf;
    }

    void maximumIsolatingFlow(problemPointer problem,
                              size_t thread_id, bool parallel) {
        graph_contraction::setTerminals(problem, original_terminals);
        std::vector<std::vector<NodeID> > maxVolIsoBlock;
        std::vector<NodeID> curr_terminals;
        std::vector<NodeID> orig_index;
        for (const auto& t : problem->terminals) {
            curr_terminals.emplace_back(t.position);
            orig_index.emplace_back(t.original_id);
        }

        bool parallel_flows = false;
        std::vector<std::future<std::vector<NodeID> > > futures;
        // so futures don't lose their object :)
        std::vector<push_relabel<false, true> > prs(problem->terminals.size());
        if (parallel) {
            // in the beginning when we don't have many problems
            // already (but big graphs), we can start a thread per flow.
            // later on, we have a problem for each processor to work on,
            // so we run sequential flows
            parallel_flows = true;
        }

        for (NodeID i = 0; i < problem->terminals.size(); ++i) {
            if (problem->terminals[i].invalid_flow) {
                if (parallel_flows) {
                    maxVolIsoBlock.emplace_back();
                    if (!configuration::getConfig()->disable_cpu_affinity) {
                        cpu_set_t all_cores;
                        CPU_ZERO(&all_cores);
                        for (size_t i = 0; i < num_threads; ++i) {
                            CPU_SET(i, &all_cores);
                        }
                        sched_setaffinity(0, sizeof(cpu_set_t), &all_cores);
                    }
                    futures.emplace_back(
                        std::async(
                            &push_relabel<false, true>::callable_max_flow,
                            &prs[i], problem->graph, curr_terminals,
                            i, true));
                } else {
                    push_relabel pr;
                    maxVolIsoBlock.emplace_back(
                        pr.solve_max_flow_min_cut(problem->graph,
                                                  curr_terminals,
                                                  i,
                                                  true).second);
                }

                problem->terminals[i].invalid_flow = false;
            } else {
                maxVolIsoBlock.emplace_back();
                maxVolIsoBlock.back().emplace_back(
                    problem->terminals[i].position);
            }
        }

        if (parallel_flows) {
            for (size_t i = 0; i < futures.size(); ++i) {
                auto& t = futures[i];
                maxVolIsoBlock[i] = t.get();
            }

            if (!configuration::getConfig()->disable_cpu_affinity) {
                cpu_set_t my_id;
                CPU_ZERO(&my_id);
                CPU_SET(thread_id, &my_id);
                sched_setaffinity(0, sizeof(cpu_set_t), &my_id);
            }
        }

        graph_contraction::contractIsolatingBlocks(problem, maxVolIsoBlock);

        EdgeWeight maximum = 0;
        FlowType sum = 0;
        size_t max_index = 0;

        for (size_t i = 0; i < problem->terminals.size(); ++i) {
            NodeID orig_id = problem->terminals[i].original_id;
            NodeID term_id = problem->mapped(original_terminals[orig_id]);
            NodeID pos = problem->graph->getCurrentPosition(term_id);
            EdgeWeight deg = problem->graph->getWeightedNodeDegree(pos);

            if (deg > maximum) {
                max_index = orig_id;
                maximum = deg;
            }
            sum += deg;
        }

        for (NodeID n : problem->graph->nodes()) {
            problem->graph->setPartitionIndex(n, max_index);
        }

        for (size_t l = 0; l < original_terminals.size(); ++l) {
            NodeID l_coarse = problem->mapped(original_terminals[l]);
            NodeID v = problem->graph->getCurrentPosition(l_coarse);
            problem->graph->setPartitionIndex(v, l);
        }

        problem->upper_bound = problem->deleted_weight + sum - maximum;
        problem->lower_bound = problem->deleted_weight + (sum+2-1)/2; // tlx::div_ceil(sum, 2);

        graph_contraction::setTerminals(problem, original_terminals);
    }

    std::vector<NodeID> original_terminals;
    size_t num_threads;
};
