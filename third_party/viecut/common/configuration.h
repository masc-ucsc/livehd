/******************************************************************************
 * configuration.h
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 *****************************************************************************/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/definitions.h"

class configuration {
 public:
    configuration(configuration const&) = delete;
    void operator = (configuration const&) = delete;

    static std::shared_ptr<configuration> getConfig() {
        static std::shared_ptr<configuration> instance{ new configuration };
        return instance;
    }

    ~configuration() { }

    // Settings - these are public for ease of use
    // don't change in program when not necessary
    std::string graph_filename;
    std::string partition_file = "";
    std::string output_path = "";
    size_t seed = 0;
    bool verbose = false;

    // multiterminal cut parameters
    std::string edge_selection = "heavy_vertex";
    std::string queue_type = "bound_sum";
    std::vector<std::string> term_strings;
    int top_k = 0;
    int random_k = 0;
    size_t bfs_size = 0;
    size_t threads = 1;
    double preset_percentage = 0;
    size_t print_cc = 0;
    size_t num_terminals;
    bool disable_cpu_affinity = false;
    double high_distance_factor = 0.9;
    size_t high_distance_flows = 5;
    int distant_terminals = 0;
    std::string first_branch_path = "";
    bool write_solution = false;
    size_t neighborhood_degrees = 50;
    size_t random_flows = 5;
    double removeTerminalsBeforeBranch = 0.1;
    size_t contractionDepthAroundTerminal = 1;
    size_t maximumBranchingFactor = 5;
    bool multibranch = true;
    bool inexact = false;
    bool runLocalSearch = true;
    size_t timeoutSeconds = 600;
    double ilpTime = 60.0;
    NodeID orign;
    EdgeID origm;

    // minimum cut parameters
    bool save_cut = false;
    std::string algorithm;
    std::string sampling_type = "geometric";
    std::string pq = "default";
    size_t num_iterations = 1;
    bool disable_limiting = false;
    double contraction_factor = 0.0;
    bool find_most_balanced_cut = false;
    bool find_lowest_conductance = false;
    bool blacklist = true;
    bool set_node_in_cut = false;

    // dynamic minimum cut
    size_t depthOfPartialRelabeling = 1;

    // karger-stein:
    size_t optimal = 0;

    bool use_ilp = false;

 private:
    configuration() { }
};
