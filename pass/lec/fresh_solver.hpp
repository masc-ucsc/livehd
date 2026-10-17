// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "array_blast.hpp"
#include "solver_profile.hpp"

namespace livehd::lec {
class Solver_deadline : public std::runtime_error {
public:
  Solver_deadline() : std::runtime_error("hit formal.timeout (shared wall deadline)") {}
};
// Assertion scopes are term-vector snapshots, never SMT push/pop. Every check
// owns a fresh non-incremental solver; its model survives until the next check.
// Terms and the base DAG belong to the caller's shared TermManager.
class Fresh_solver {
public:
  using Attach  = std::function<void(cvc5::Solver&)>;
  using Capture = std::function<void(const cvc5::Solver&)>;
  explicit Fresh_solver(cvc5::TermManager& tm, Attach attach = {}, Capture capture = {})
      : tm_(tm), attach_(std::move(attach)), capture_(std::move(capture)) {}
  ~Fresh_solver() { finish(); }
  Fresh_solver(const Fresh_solver&)            = delete;
  Fresh_solver& operator=(const Fresh_solver&) = delete;
  void          setLogic(std::string_view logic) { logic_ = logic; }
  void          setOption(std::string_view key, std::string_view value) { options_[std::string(key)] = value; }
  void          set_profile(std::string_view name, Solver_query_ctx ctx) {
    profile_ = name;
    ctx_     = ctx;
  }
  void set_base_slicing(bool enable) { slice_base_ = enable; }
  // Rewrite arrays whose index has at most `max_index_bits` bits into
  // per-entry bit-vectors before every check (see array_blast.hpp); 0 = off.
  // getValue/simplify follow the same rewrite, so callers keep using their
  // original terms.
  void set_array_blast(uint32_t max_index_bits) {
    blast_bits_ = max_index_bits;
    blaster_.reset();
  }
  void set_deadline(std::chrono::steady_clock::time_point deadline) { deadline_ = deadline; }
  void assertFormula(const cvc5::Term& term) { assertions_.push_back(term); }
  void push(uint32_t n = 1) {
    while (n--) {
      scopes_.push_back(assertions_.size());
    }
  }
  void pop(uint32_t n = 1) {
    if (n > scopes_.size()) {
      throw std::logic_error("unbalanced fresh-solver assertion scope");
    }
    while (n--) {
      assertions_.resize(scopes_.back());
      scopes_.pop_back();
    }
  }
  cvc5::Result checkSat() { return check(nullptr); }
  cvc5::Result checkSatAssuming(const cvc5::Term& assumption) { return check(&assumption); }
  // Check the innermost pushed scope ALONE, without the base assertions. For
  // a scope that pins every symbol to a value whose consistency with the base
  // the caller has already established (the sampled-effort simulator: one
  // substitution + rewrite of the base instead of cvc5 re-preprocessing the
  // whole unrolling); the model then serves getValue as usual.
  cvc5::Result check_scope() {
    std::vector<bool> keep(assertions_.size(), false);
    for (size_t i = scopes_.empty() ? 0 : scopes_.back(); i < assertions_.size(); ++i) {
      keep[i] = true;
    }
    rebuild(nullptr, false, &keep);
    checked_ = true;
    return solver_->checkSat();
  }
  cvc5::Term getValue(const cvc5::Term& term) const {
    if (blasted_) {
      auto v = blaster_->value(term, [&](const cvc5::Term& t) { return solver_->getValue(t); });
      if (!v.isNull()) {
        return v;
      }
    }
    return solver_->getValue(term);
  }
  cvc5::Term simplify(const cvc5::Term& term) {
    if (!solver_) {
      rebuild();
    }
    if (blasted_ && !term.getSort().isArray()) {
      if (auto r = blaster_->rewrite(term); !r.isNull()) {
        return solver_->simplify(r);
      }
    }
    return solver_->simplify(term);
  }
  const std::vector<cvc5::Term>& assertions() const { return assertions_; }

private:
  cvc5::Result check(const cvc5::Term* assumption) {
    rebuild(assumption, true);
    checked_    = true;
    auto result = solver_->checkSat();
    // UNSAT of a weaker base proves the full obligation. SAT/Unknown must be
    // rechecked against the full base; only that model can yield a witness.
    if (sliced_ && !result.isUnsat()) {
      rebuild(assumption, false);
      checked_ = true;
      result   = solver_->checkSat();
    }
    return result;
  }
  std::vector<bool> slice(const cvc5::Term* assumption) const {
    std::vector<bool> keep(assertions_.size(), true);
    if (!slice_base_ || scopes_.empty()) {
      return keep;
    }
    const auto base = scopes_.back();
    // Tiny bases cost less to preprocess than to index. Sampling does not
    // enable this path; its restrictions usually touch the whole unrolling.
    if (base < 256) {
      return keep;
    }
    std::unordered_map<cvc5::Term, std::vector<size_t>> definitions;
    std::vector<cvc5::Term>                             todo;
    for (size_t i = 0; i < assertions_.size(); ++i) {
      const auto& term = assertions_[i];
      if (i < base && term.getKind() == cvc5::Kind::EQUAL && term[0].getKind() == cvc5::Kind::CONSTANT) {
        definitions[term[0]].push_back(i);
        keep[i] = false;
      } else {
        todo.push_back(term);
      }
    }
    if (assumption) {
      todo.push_back(*assumption);
    }
    std::unordered_set<cvc5::Term> seen;
    while (!todo.empty()) {
      auto term = todo.back();
      todo.pop_back();
      if (!seen.insert(term).second) {
        continue;
      }
      if (auto it = definitions.find(term); it != definitions.end()) {
        for (auto index : it->second) {
          keep[index] = true;
          todo.push_back(assertions_[index][1]);
        }
      }
      for (const auto& child : term) {
        todo.push_back(child);
      }
    }
    return keep;
  }
  void finish() noexcept {
    if (solver_ && checked_ && capture_) {
      try {
        capture_(*solver_);
      } catch (...) { /* Statistics must not fail a proof. */
      }
    }
    solver_.reset();
    checked_ = false;
  }
  void rebuild(const cvc5::Term* assumption = nullptr, bool allow_slice = false, const std::vector<bool>* only = nullptr) {
    finish();
    const auto keep = only != nullptr ? *only : allow_slice ? slice(assumption) : std::vector<bool>(assertions_.size(), true);
    sliced_         = std::find(keep.begin(), keep.end(), false) != keep.end();
    // Small arrays become per-entry bit-vectors (all-or-nothing: an array's
    // entry symbols are related to it only through the rewrite).
    std::vector<cvc5::Term> blasted;
    blasted_ = false;
    if (blast_bits_ > 0) {
      if (!blaster_) {
        blaster_ = std::make_unique<Array_blaster>(tm_, blast_bits_);
      }
      bool ok = !blaster_->failed();
      for (size_t i = 0; ok && i < assertions_.size(); ++i) {
        if (keep[i]) {
          blasted.push_back(blaster_->rewrite(assertions_[i]));
          ok = !blasted.back().isNull();
        }
      }
      if (ok && assumption) {
        blasted.push_back(blaster_->rewrite(*assumption));
        ok = !blasted.back().isNull();
      }
      blasted_ = ok && blaster_->active();
    }
    solver_ = std::make_unique<cvc5::Solver>(tm_);
    if (attach_) {
      attach_(*solver_);
    }
    // Every array was rewritten away: the query is array-free, and declaring
    // the array theory anyway costs cvc5 several times the solve time (a
    // two-lane 4x8 BMC miter: 11.9 s under QF_ABV, 2.6 s under QF_BV).
    const bool  array_free = blasted_ && !blaster_->saw_large_array();
    std::string logic      = profile_ == "intblast" ? "QF_AUFBVNIA" : logic_;
    if (array_free && logic == "QF_ABV") {
      logic = "QF_BV";
    } else if (array_free && logic == "QF_AUFBV") {
      logic = "QF_UFBV";
    }
    solver_->setLogic(logic);
    for (const auto& [key, value] : options_) {
      solver_->setOption(key, value);
    }
    auto ctx      = ctx_;
    // Legacy call sites can widen the logic for int blasting or UF boxes.
    ctx.int_blast = ctx.int_blast || (logic_ == "ALL" || logic_ == "QF_AUFBVNIA") || profile_ == "intblast";
    ctx.has_boxes = ctx.has_boxes || logic_ == "QF_AUFBV";
    if (array_free) {
      ctx.has_mem = false;
    }
    configure_solver(*solver_, solver_profile(profile_), ctx);
    // Dynamic caps must win over the profile's initial allowance.
    if (auto it = options_.find("tlimit-per"); it != options_.end()) {
      solver_->setOption(it->first, it->second);
    }
    if (deadline_ != std::chrono::steady_clock::time_point{}) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - std::chrono::steady_clock::now()).count();
      if (left <= 0) {
        throw Solver_deadline{};
      }
      long long cap = left;
      if (auto it = options_.find("tlimit-per"); it != options_.end()) {
        const auto requested = std::stoll(it->second);
        if (requested > 0) {
          cap = std::min(cap, requested);
        }
      }
      solver_->setOption("tlimit-per", std::to_string(cap));
    }
    if (blasted_) {
      for (const auto& t : blasted) {
        solver_->assertFormula(t);
      }
      return;
    }
    for (size_t i = 0; i < assertions_.size(); ++i) {
      if (keep[i]) {
        solver_->assertFormula(assertions_[i]);
      }
    }
    if (assumption) {
      solver_->assertFormula(*assumption);
    }
  }
  cvc5::TermManager&                    tm_;
  Attach                                attach_;
  Capture                               capture_;
  std::string                           logic_ = "QF_ABV", profile_ = "internal";
  Solver_query_ctx                      ctx_;
  std::map<std::string, std::string>    options_;
  std::vector<cvc5::Term>               assertions_;
  std::vector<size_t>                   scopes_;
  std::unique_ptr<cvc5::Solver>         solver_;
  std::chrono::steady_clock::time_point deadline_{};
  std::unique_ptr<Array_blaster>        blaster_;
  uint32_t                              blast_bits_ = 0;
  bool                                  checked_ = false, slice_base_ = false, sliced_ = false, blasted_ = false;
};
}  // namespace livehd::lec
