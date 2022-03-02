/******************************************************************************
 * mincut.cpp
 *
 * Source of VieCut
 *
 ******************************************************************************
 * Copyright (C) 2017-2018 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#include <ext/alloc_traits.h>
// #include <omp.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "algorithms/global_mincut/algorithms.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "common/configuration.h"
#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"
#include "io/graph_io.h"
//#include "tlx/cmdline_parser.hpp"
// #include "tlx/logger.hpp"
#include "tools/random_functions.h"
#include "tools/string.h"
#include "tools/timer.h"

// typedef graph_access graph_type;
typedef mutable_graph graph_type;
typedef std::shared_ptr<graph_type> GraphPtr;

int main(int argn, char** argv) {
    static constexpr bool debug = false;

#if 0
    tlx::CmdlineParser cmdl;
    size_t num_iterations = 1;

    auto cfg = configuration::getConfig();

    cmdl.add_param_string("graph", cfg->graph_filename, "path to graph file");
#ifdef PARALLEL
    std::vector<std::string> procs;
    cmdl.add_stringlist('p', "proc", procs, "number of processes");
#endif
    cmdl.add_param_string("algo", cfg->algorithm, "algorithm name");
    cmdl.add_string('q', "pq", cfg->queue_type,
                    "name of priority queue implementation");
    cmdl.add_size_t('i', "iter", num_iterations, "number of iterations");
    cmdl.add_bool('l', "disable_limiting", cfg->disable_limiting,
                  "disable limiting of PQ values");
    cmdl.add_bool('s', "save_cut", cfg->save_cut,
                  "find which vertices are on which side of minimum cut");
    cmdl.add_double('c', "contraction_factor", cfg->contraction_factor,
                    "contraction factor for pre-run of viecut");
    cmdl.add_string('k', "sampling_type", cfg->sampling_type,
                    "sampling variant for pre-run of viecut");
    cmdl.add_flag('b', "balanced", cfg->find_most_balanced_cut,
                  "find most balanced minimum cut");
    cmdl.add_flag('d', "minimize conductance", cfg->find_lowest_conductance,
                  "find lowest conductance minimum cut");
    cmdl.add_string('o', "output_path", cfg->output_path,
                    "print minimum cut to file");
    cmdl.add_flag('v', "verbose", cfg->verbose, "more verbose logs");
    cmdl.add_string('e', "edge_select", cfg->edge_selection, "NNI edge select");
    cmdl.add_size_t('r', "seed", cfg->seed, "random seed");

    if (!cmdl.process(argn, argv))
        return -1;

#else
    size_t num_iterations = 1;
    auto cfg = configuration::getConfig();
#endif

    if (cfg->find_lowest_conductance) {
        // same check, just different optimization function, rest of code reused
        cfg->find_most_balanced_cut = true;
    }

    std::vector<int> numthreads;
    timer t;
    GraphPtr G = graph_io::readGraphWeighted<graph_type>(
        configuration::getConfig()->graph_filename);

    // ***************************** perform cut *****************************
#ifdef PARALLEL
    size_t i;
    try {
        for (i = 0; i < procs.size(); ++i) {
            numthreads.emplace_back(std::stoi(procs[i]));
        }
    } catch (...) {
    }
#else
#endif
    if (numthreads.empty())
        numthreads.emplace_back(1);
    timer tdegs;

    for (size_t i = 0; i < num_iterations; ++i) {
        for (int numthread : numthreads) {
            random_functions::setSeed(cfg->seed);

            NodeID n = G->number_of_nodes();
            EdgeID m = G->number_of_edges();

            auto mc = selectMincutAlgorithm<GraphPtr>(cfg->algorithm);
            // omp_set_num_threads(numthread);
            cfg->threads = numthread;

            t.restart();
            EdgeWeight cut;
            cut = mc->perform_minimum_cut(G);

            if (cfg->output_path != "") {
                if (!cfg->save_cut) {
                    exit(1);
                }
                if (cfg->find_most_balanced_cut == false) {
                    // most balanced cut already prints inside of algorithm
                    graph_io::writeCut(G, cfg->output_path);
                }
            }

            std::string graphname = string::basename(cfg->graph_filename);
            std::string algprint = cfg->algorithm;
#ifdef PARALLEL
            algprint += "par";
#endif
            algprint += cfg->pq;

            if (cfg->disable_limiting) {
                algprint += "unlimited";
            }

            std::cout << "RESULT algo=" << algprint
                      << " graph=" << graphname
                      << " time=" << t.elapsed()
                      << " cut=" << cut
                      << " n=" << n
                      << " m=" << m / 2
                      << " processes=" << numthread
                      << " edge_select=" << cfg->edge_selection
                      << " seed=" << cfg->seed
                      << std::endl;
        }
    }
}
