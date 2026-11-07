// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legroute_track_repair.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace livehd::placement::lr {
Track_repair_result solve_track_repair(const Track_repair_problem& problem, const Track_repair_limits& limits) {
  using Clock               = std::chrono::steady_clock;
  const auto          start = Clock::now();
  Track_repair_result out;
  const auto seconds = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };
  const auto finish  = [&](Sat_status status, std::string reason) {
    out.status        = status;
    out.reason        = std::move(reason);
    out.total_seconds = seconds(start, Clock::now());
    if (!out.solve_calls) {
      out.build_seconds = out.total_seconds;
    }
    if (status != Sat_status::satisfiable) {
      out.selected.clear();
    }
    return out;
  };
  const auto expired = [&] { return Clock::now() >= limits.deadline; };
  if (expired()) {
    return finish(Sat_status::unknown, "deadline");
  }
  const auto requests = problem.candidate_counts.size();
  if (requests > limits.max_requests) {
    return finish(Sat_status::unknown, "request_limit");
  }
  if (!problem.preferred.empty() && problem.preferred.size() != requests) {
    throw std::invalid_argument("track repair preferences must match request count");
  }
  if (limits.conflicts > unsigned(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("track repair conflict limit exceeds solver range");
  }
  if (!limits.conflicts) {
    return finish(Sat_status::unknown, "conflict_limit");
  }
  // Preflight exact encoding size before allocating variables or emitting clauses.
  // The AMO ladder uses n-1 auxiliaries and 3*n-4 clauses for n>=2.
  size_t planned_clauses = requests, auxiliaries = 0;
  if (planned_clauses > limits.max_clauses || problem.incompatible.size() > limits.max_clauses - planned_clauses) {
    return finish(Sat_status::unknown, "clause_limit");
  }
  planned_clauses += problem.incompatible.size();
  for (size_t r = 0; r < requests; ++r) {
    if (expired()) {
      return finish(Sat_status::unknown, "deadline");
    }
    const auto n = problem.candidate_counts[r];
    if (n > limits.max_candidates_per_request || n > limits.max_candidates - out.candidate_literals) {
      return finish(Sat_status::unknown, "candidate_limit");
    }
    out.candidate_literals += n;
    if (out.candidate_literals > size_t(std::numeric_limits<int>::max())) {
      return finish(Sat_status::unknown, "variable_limit");
    }
    if (!problem.preferred.empty() && problem.preferred[r] && *problem.preferred[r] >= n) {
      throw std::invalid_argument("track repair preferred candidate is outside its request");
    }
    if (n > 1) {
      if (n - 1 > size_t(std::numeric_limits<int>::max()) - auxiliaries) {
        return finish(Sat_status::unknown, "variable_limit");
      }
      auxiliaries += n - 1;
      // Written without 3*n to avoid overflow even for caller-supplied large limits.
      if (n - 1 > (limits.max_clauses - planned_clauses + 1) / 3) {
        return finish(Sat_status::unknown, "clause_limit");
      }
      planned_clauses += 3 * (n - 1) - 1;
    }
  }
  if (auxiliaries > size_t(std::numeric_limits<int>::max()) - out.candidate_literals) {
    return finish(Sat_status::unknown, "variable_limit");
  }
  for (const auto& pair : problem.incompatible) {
    if (expired()) {
      return finish(Sat_status::unknown, "deadline");
    }
    if (pair[0] >= requests || pair[2] >= requests || pair[1] >= problem.candidate_counts[pair[0]]
        || pair[3] >= problem.candidate_counts[pair[2]]) {
      throw std::invalid_argument("track repair incompatible pair is outside its request/candidate domain");
    }
  }
  Sat_solver                    solver(Sat_backend::cadical);
  std::vector<std::vector<int>> literals(requests);
  for (size_t r = 0; r < requests; ++r) {
    auto& choices = literals[r];
    choices.reserve(problem.candidate_counts[r]);
    for (size_t c = 0; c < problem.candidate_counts[r]; ++c) {
      if (expired()) {
        return finish(Sat_status::unknown, "deadline");
      }
      choices.push_back(solver.new_variable());
      ++out.variables;
    }
  }
  const auto add = [&](std::span<const int> clause) {
    solver.clause(clause);
    ++out.clauses;
  };
  const auto pair_clause = [&](int a, int b) {
    const std::array clause{a, b};
    add(clause);
  };
  for (size_t r = 0; r < requests; ++r) {
    if (expired()) {
      return finish(Sat_status::unknown, "deadline");
    }
    const auto& choices = literals[r];
    add(choices);  // An empty request deliberately makes the finite model UNSAT.
    int previous = 0;
    for (size_t c = 0; c < choices.size(); ++c) {
      if (expired()) {
        return finish(Sat_status::unknown, "deadline");
      }
      if (previous) {
        pair_clause(-choices[c], -previous);
      }
      if (c + 1 < choices.size()) {
        const int next = solver.new_variable();
        ++out.variables;
        pair_clause(-choices[c], next);
        if (previous) {
          pair_clause(-previous, next);
        }
        previous = next;
      }
    }
    if (!problem.preferred.empty() && problem.preferred[r]) {
      solver.prefer(choices[*problem.preferred[r]]);
    }
  }
  for (const auto& pair : problem.incompatible) {
    if (expired()) {
      return finish(Sat_status::unknown, "deadline");
    }
    pair_clause(-literals[pair[0]][pair[1]], -literals[pair[2]][pair[3]]);
  }
  if (out.clauses != planned_clauses || out.variables != out.candidate_literals + auxiliaries) {
    throw std::logic_error("track repair encoding size disagrees with preflight");
  }
  const auto solve_start = Clock::now();
  out.build_seconds      = seconds(start, solve_start);
  const auto remaining   = std::chrono::duration_cast<std::chrono::milliseconds>(limits.deadline - solve_start).count();
  if (remaining <= 0) {
    return finish(Sat_status::unknown, "deadline");
  }
  ++out.solve_calls;
  const auto status
      = solver.solve({}, limits.conflicts, unsigned(std::min<int64_t>(remaining, std::numeric_limits<unsigned>::max())));
  const auto decode_start = Clock::now();
  out.solve_seconds       = seconds(solve_start, decode_start);
  if (expired()) {
    return finish(Sat_status::unknown, "deadline");
  }
  if (status != Sat_status::satisfiable) {
    return finish(status, status == Sat_status::unsatisfiable ? "finite_domain_unsatisfiable" : "solver_unknown");
  }
  out.selected.resize(requests);
  for (size_t r = 0; r < requests; ++r) {
    if (expired()) {
      out.decode_seconds = seconds(decode_start, Clock::now());
      return finish(Sat_status::unknown, "deadline");
    }
    size_t found = 0;
    for (size_t c = 0; c < literals[r].size(); ++c) {
      if (solver.value(literals[r][c])) {
        out.selected[r] = c;
        ++found;
      }
    }
    if (found != 1) {
      throw std::logic_error("track repair SAT model does not select exactly one candidate per request");
    }
  }
  for (const auto& pair : problem.incompatible) {
    if (expired()) {
      out.decode_seconds = seconds(decode_start, Clock::now());
      return finish(Sat_status::unknown, "deadline");
    }
    if (out.selected[pair[0]] == pair[1] && out.selected[pair[2]] == pair[3]) {
      throw std::logic_error("track repair SAT model violates an incompatible pair");
    }
  }
  out.decode_seconds = seconds(decode_start, Clock::now());
  if (expired()) {
    return finish(Sat_status::unknown, "deadline");
  }
  return finish(Sat_status::satisfiable, "finite_allocation");
}
}  // namespace livehd::placement::lr
