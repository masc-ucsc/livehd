// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Manual, bounded solver-option sweep. No benchmark repository dependencies.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <vector>

#include "fork_race.hpp"
#include "solve_stats.hpp"
#include "solver_profile.hpp"
using namespace livehd::lec;
using cvc5::Kind;
struct Experiment {
  std::string                                      name, profile;
  bool                                             models = false;
  std::vector<std::pair<std::string, std::string>> options;
};
struct Fixture {
  std::string name;
  bool        memory = false, sat = false;
};
cvc5::Term fixture(cvc5::TermManager& tm, const Fixture& f) {
  auto bv = tm.mkBitVectorSort(16);
  auto a = tm.mkConst(bv, "a"), b = tm.mkConst(bv, "b"), c = tm.mkConst(bv, "c");
  auto term = [&](Kind kind, std::initializer_list<cvc5::Term> args) { return tm.mkTerm(kind, std::vector<cvc5::Term>(args)); };
  if (f.name == "multiply") {
    auto lhs = term(Kind::BITVECTOR_MULT, {a, term(Kind::BITVECTOR_ADD, {b, c})});
    auto rhs = term(Kind::BITVECTOR_ADD, {term(Kind::BITVECTOR_MULT, {a, b}), term(Kind::BITVECTOR_MULT, {a, c})});
    return term(Kind::DISTINCT, {lhs, rhs});
  }
  if (f.name == "division") {
    auto product = term(Kind::BITVECTOR_MULT, {term(Kind::BITVECTOR_UDIV, {a, b}), b});
    auto sum     = term(Kind::BITVECTOR_ADD, {product, term(Kind::BITVECTOR_UREM, {a, b})});
    return term(Kind::AND, {term(Kind::DISTINCT, {b, tm.mkBitVector(16, 0)}), term(Kind::DISTINCT, {sum, a})});
  }
  if (f.name == "xor") {
    auto na = term(Kind::BITVECTOR_NOT, {a}), nb = term(Kind::BITVECTOR_NOT, {b}), nc = term(Kind::BITVECTOR_NOT, {c});
    auto and3 = [&](auto x, auto y, auto z) { return term(Kind::BITVECTOR_AND, {term(Kind::BITVECTOR_AND, {x, y}), z}); };
    auto lhs  = term(Kind::BITVECTOR_XOR, {a, term(Kind::BITVECTOR_XOR, {b, c})});
    auto rhs  = term(
        Kind::BITVECTOR_OR,
        {term(Kind::BITVECTOR_OR, {and3(a, nb, nc), and3(na, b, nc)}), term(Kind::BITVECTOR_OR, {and3(na, nb, c), and3(a, b, c)})});
    return term(Kind::DISTINCT, {lhs, rhs});
  }
  if (f.name == "mux") {
    auto select = tm.mkConst(tm.mkBitVectorSort(1), "select");
    auto cond   = term(Kind::EQUAL, {select, tm.mkBitVector(1, 1)});
    auto mask   = term(Kind::BITVECTOR_NEG, {tm.mkTerm(tm.mkOp(Kind::BITVECTOR_ZERO_EXTEND, {15}), {select})});
    auto lhs    = term(Kind::ITE, {cond, a, b});
    auto rhs    = term(Kind::BITVECTOR_OR,
                       {term(Kind::BITVECTOR_AND, {a, mask}), term(Kind::BITVECTOR_AND, {b, term(Kind::BITVECTOR_NOT, {mask})})});
    return term(Kind::DISTINCT, {lhs, rhs});
  }
  if (f.memory) {
    auto mem = tm.mkConst(tm.mkArraySort(bv, bv), "m");
    auto lhs = term(Kind::SELECT, {term(Kind::STORE, {mem, a, b}), c});
    auto rhs = term(Kind::ITE, {term(Kind::EQUAL, {a, c}), b, term(Kind::SELECT, {mem, c})});
    return term(Kind::DISTINCT, {lhs, rhs});
  }
  return term(Kind::AND,
              {term(Kind::EQUAL, {term(Kind::BITVECTOR_MULT, {a, b}), tm.mkBitVector(16, 15)}),
               term(Kind::DISTINCT, {a, tm.mkBitVector(16, 0)})});
}
int main(int argc, char** argv) {
  const long long               cap         = argc > 1 ? std::max(1LL, std::atoll(argv[1])) : 250;
  const std::vector<Experiment> experiments = {
      {       "compiler-minisat1","internal", false,                                     {{"rlimit-per", "10000"}}                                                         },
      {       "compiler-minisat2",     "internal", false,                                                            {{"rlimit-per", "10000"}}},
      {       "compiler-minisat3",     "internal", false,                                                            {{"rlimit-per", "10000"}}},
      {       "compiler-cadical1",     "internal", false,                                                            {{"rlimit-per", "10000"}}},
      {       "compiler-cadical2",     "internal", false,                                                            {{"rlimit-per", "10000"}}},
      {       "compiler-cadical3",     "internal", false,                                                            {{"rlimit-per", "10000"}}},
      {                "baseline",     "internal", false,                                                                                   {}},
      {             "bv-bool-ite",     "internal", false,        {{"bv-to-bool", "true"}, {"ite-simp", "true"}, {"early-ite-removal", "true"}}},
      {                   "lazy8",         "lazy", false,                                                                                   {}},
      {                  "lazy16",         "lazy", false,                                                      {{"bv-abstraction-size", "16"}}},
      {                  "lazy33",         "lazy", false,                                                      {{"bv-abstraction-size", "33"}}},
      {        "lazy-value-limit",         "lazy", false,                                             {{"bv-abstraction-value-limiter", "16"}}},
      {                  "kissat", "eager-kissat", false,                                                                                   {}},
      {                     "cms",    "eager-cms", false,                                                                                   {}},
      {            "deep-restart",     "internal", false,                                                 {{"deep-restart", "input-and-prop"}}},
      {         "learned-rewrite",     "internal", false,                                                        {{"learned-rewrite", "true"}}},
      {                "intblast",     "intblast", false,                                                                                   {}},
      {        "intblast-bitwise",     "intblast", false,                                                           {{"iand-mode", "bitwise"}}},
      {            "intblast-sum",     "intblast", false,                                                               {{"iand-mode", "sum"}}},
      {          "intblast-gran2",     "intblast", false,                                                 {{"bvand-integer-granularity", "2"}}},
      {          "intblast-gran4",     "intblast", false,                                                 {{"bvand-integer-granularity", "4"}}},
      {           "intblast-pow2",     "intblast", false,                                                     {{"bv-to-int-use-pow2", "true"}}},
      {         "intblast-planes",     "intblast", false,                                              {{"nl-ext-tplanes-interleave", "true"}}},
      {          "intblast-cover",     "intblast", false,                                                 {{"nl-cov-linear-model", "initial"}}},
      {            "arrays-prop2",     "internal", false,                                                               {{"arrays-prop", "2"}}},
      {             "arrays-weak",     "internal", false,                                                      {{"arrays-weak-equiv", "true"}}},
      {            "arrays-eager",     "internal", false,                                                    {{"arrays-eager-lemmas", "true"}}},
      {          "arrays-sharing",     "internal", false,                                                  {{"arrays-reduce-sharing", "true"}}},
      {              "central-ee",     "internal", false,                                                             {{"ee-mode", "central"}}},
      {                 "witness",      "witness",  true,                                                                                   {}},
      {"witness-model-verify-off",      "witness",  true,                                                          {{"model-verify", "false"}}},
      {         "verify-baseline",       "verify",  true,                                                                                   {}},
      {       "verify-difficulty",       "verify",  true,                                                     {{"produce-difficulty", "true"}}},
      {            "verify-cores",
       "verify",  true,
       {{"produce-unsat-cores", "true"}, {"minimal-unsat-cores", "true"}, {"unsat-cores-mode", "sat-proof"}}                                  },
      {                   "seed1",     "internal", false,                                            {{"seed", "1"}, {"sat-random-seed", "1"}}},
      {                   "seed7",     "internal", false,                                            {{"seed", "7"}, {"sat-random-seed", "7"}}},
      {        "native-portfolio",     "internal", false,                                                          {{"use-portfolio", "true"}}},
      {       "native-partitions",     "internal", false,                                                        {{"compute-partitions", "2"}}},
  };
  const std::vector<Fixture> fixtures = {
      {"multiply"},
      {"division"},
      {"xor"},
      {"mux"},
      {"array", true},
      {"sat-multiply", false, true}
  };
  std::puts("experiment,fixture,status,wall_us,solver_ms,conflicts,resource_units,model_checked");
  bool unsound = false;
  std::fflush(stdout);
  for (const auto& e : experiments) {
    for (const auto& f : fixtures) {
      auto run = [&](int) -> std::string {
        // Native partition experiments may print SMT-LIB. Keep CSV parent-owned.
        if (std::freopen("/dev/null", "w", stdout) == nullptr) {
          return {};
        }
        const auto             start = std::chrono::steady_clock::now();
        cvc5::TermManager      tm;
        cvc5::Solver           solver(tm);
        Cvc5_stats             stats;
        const auto&            profile = solver_profile(e.profile);
        const Solver_query_ctx ctx{.has_mem         = f.memory,
                                   .witness         = e.models,
                                   .compiler_budget = e.name.starts_with("compiler-minisat"),
                                   .int_blast       = e.profile == "intblast",
                                   .timeout_ms      = cap};
        if (!profile_eligible(profile, ctx)) {
          return std::format("{},{},ineligible,0,0,0,0,0", e.name, f.name);
        }
        const char* status  = "unknown";
        bool        checked = false;
        try {
          solver.setLogic(e.profile == "intblast" ? "QF_AUFBVNIA" : f.memory ? "QF_ABV" : "QF_BV");
          configure_solver(solver, profile, ctx);
          for (const auto& [key, value] : e.options) {
            solver.setOption(key, value);
          }
          const auto goal = fixture(tm, f);
          solver.assertFormula(goal);
          const auto result = solver.checkSat();
          status            = result.isUnsat() ? "unsat" : result.isSat() ? "sat" : "unknown";
          if (result.isSat() && e.models) {
            checked = solver.getValue(goal).getBooleanValue();
            if (!checked) {
              status = "UNSOUND";
            }
          }
          if ((result.isSat() && !f.sat) || (result.isUnsat() && f.sat)) {
            status = "UNSOUND";
          }
          if (e.name == "verify-difficulty") {
            (void)solver.getDifficulty();
          }
          if (e.name == "verify-cores" && result.isUnsat()) {
            (void)solver.getUnsatCore();
          }
          capture_cvc5_stats(solver, &stats);
        } catch (const std::exception& error) {
          status = "rejected";
          std::fprintf(stderr, "%s/%s: %s\n", e.name.c_str(), f.name.c_str(), error.what());
        }
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        return std::format("{},{},{},{},{},{},{},{}",
                           e.name,
                           f.name,
                           status,
                           us,
                           stats.total_ms,
                           stats.conflicts,
                           stats.resource_units,
                           checked ? 1 : 0);
      };
      auto race = fork_race<std::string>(
          1,
          run,
          [](const std::string& s) { return s; },
          [](std::string_view input, std::string& s) {
            s = input;
            return true;
          },
          [](int, const std::string&) { return false; },
          cap + 200);
      if (race.done[0] && !race.results[0].empty()) {
        std::puts(race.results[0].c_str());
        unsound = unsound || race.results[0].find(",UNSOUND,") != std::string::npos;
      } else {
        std::printf("%s,%s,wall_timeout,%lld,0,0,0,0\n", e.name.c_str(), f.name.c_str(), race.elapsed_ms * 1000);
      }
      std::fflush(stdout);
    }
  }
  return unsound ? 1 : 0;
}
