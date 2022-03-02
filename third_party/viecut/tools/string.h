/*******************************************************************************
 * string.h
 *
 * Source of VieCut.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2017-2018 Alexander Noe <alexander.noe@univie.ac.at>
 ******************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/definitions.h"
#include "data_structure/graph_access.h"
#include "data_structure/mutable_graph.h"

class string {
 public:
    template <typename T>
    static std::string VecToStr(const std::vector<T>& data) {
        std::ostringstream oss;
        oss << '[';
        for (typename std::vector<T>::const_iterator it = data.begin();
             it != data.end(); ++it) {
            if (it != data.begin()) oss << ',';
            oss << *it;
        }
        oss << ']';
        return oss.str();
    }

    template <typename T>
    static typename std::enable_if<std::is_integral<T>::value,
                                   std::string>::type
    NonZeroVecToStr(const std::vector<T>& data) {
        std::ostringstream oss;
        oss << '[';

        bool first = true;
        for (typename std::vector<T>::const_iterator it = data.begin();
             it != data.end(); ++it) {
            size_t i = it - data.begin();
            if (*it != 0) {
                if (first) {
                    first = false;
                } else {
                    oss << ',';
                }
                oss << i << ":";
                oss << *it;
            }
        }
        oss << ']';
        return oss.str();
    }

    template <typename T>
    static typename std::enable_if<!std::is_integral<T>::value,
                                   std::string>::type
    NonZeroVecToStr(const std::vector<T>&) {
        return "This method only works for integral types. Use VecToStr()";
    }

#if __has_cpp_attribute(maybe_unused)
    [[maybe_unused]] static std::string graphToString(
        graphAccessPtr G) {
#else
    static std::string graphToString(graphAccessPtr G) {
#endif
        std::ostringstream oss;

        for (NodeID n : G->nodes()) {
            oss << n << ": [";
            for (EdgeID e : G->edges_of(n)) {
                oss << G->getEdgeTarget(e);
                if (e < G->get_first_invalid_edge(n) - 1) {
                    oss << ",";
                }
            }
            oss << "]\n";
        }
        return oss.str();
    }

    [[maybe_unused]] static std::string graphToString(
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
            oss << "]\n";
        }
        return oss.str();
    }

    [[maybe_unused]] static std::string weightedGraphToString(
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
            oss << "]\n";
        }
        return oss.str();
    }

    [[maybe_unused]] static std::string weightedGraphToString(
        mutableGraphPtr G) {
        std::ostringstream oss;

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

    static std::string basename(std::string filename) {
        if (filename.find_last_of("\\/") == std::string::npos) {
            return filename;
        } else {
            return filename.substr(filename.find_last_of("\\/") + 1);
        }
    }
};  // NOLINT
