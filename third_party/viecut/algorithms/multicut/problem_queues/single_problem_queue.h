/******************************************************************************
 * single_problem_queue.cpp
 *
 * Source of VieCut.
 *
 ******************************************************************************
 * Copyright (C) 2018-2019 Alexander Noe <alexander.noe@univie.ac.at>
 *
 * Published under the MIT license in the LICENSE file.
 *****************************************************************************/

#pragma once

#include <mutex>
#include <queue>
#include <utility>
#include <vector>

#include "algorithms/multicut/multicut_problem.h"

class single_problem_queue {
 public:
    single_problem_queue() : pq(cmp) { }
    ~single_problem_queue() { }

    std::pair<multicut_problem, bool> pullProblem() {
        multicut_problem mcp;
        pop_mutex.lock();
        if (empty()) {
            pop_mutex.unlock();
            return std::make_pair(mcp, false);
        }
        multicut_problem current_problem = pq.top();
        pq.pop();
        pop_mutex.unlock();
        return std::make_pair(current_problem, true);
    }

    void addProblem(const multicut_problem& p) {
        pop_mutex.lock();
        pq.push(p);
        pop_mutex.unlock();
    }

    bool empty() {
        return pq.size() == 0;
    }

    size_t size() {
        return pq.size();
    }

 private:
    constexpr static auto cmp =
        [](const multicut_problem& p1, const multicut_problem& p2) {
            if (p1.upper_bound == p2.upper_bound) {
                return p1.lower_bound > p2.lower_bound;
            } else {
                return p1.upper_bound > p2.upper_bound;
            }
        };

    std::priority_queue<multicut_problem,
                        std::vector<multicut_problem>,
                        std::function<bool(const multicut_problem&,
                                           const multicut_problem&)> > pq;

    std::mutex pop_mutex;
};
