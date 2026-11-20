// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "wirelength_factor.hpp"

#include <algorithm>
#include <bit>
#include <numeric>

#include "pass/usyn/decomposition.hpp"
#include "pass/usyn/xag.hpp"

namespace livehd::placement {
namespace {
using namespace usyn;
Truth_table function(const liberty::Comb_cell& cell) {
  Truth_table table(cell.inputs.size());
  const auto  bits = liberty::cell_truth(cell).value();
  for (unsigned i = 0; i < 1U << table.inputs; ++i) {
    table.set(i, (bits >> i) & 1);
  }
  return table;
}
// Evaluate a Liberty function in the XAG with Shannon cofactors, with
// inversion and shared inputs preserved by structural hashing.
Xsignal emit(Xag& xag, const Truth_table& table, const std::vector<Xsignal>& inputs, unsigned i = 0, unsigned pattern = 0) {
  if (i == inputs.size()) {
    return xag.constant(table.get(pattern));
  }
  auto lo = emit(xag, table, inputs, i + 1, pattern);
  auto hi = emit(xag, table, inputs, i + 1, pattern | (1U << i));
  return xag.mux(inputs[i], hi, lo);
}
struct Binding {
  std::string           master;
  std::vector<unsigned> arguments;
  double                area = 0;
};
Binding bind_factor_cell(const Truth_table& table, const std::vector<unsigned>& args,
                         const std::map<std::string, liberty::Comb_cell>& cells) {
  Binding best;
  for (const auto& [name, cell] : cells) {
    if (cell.inputs.size() != args.size() || (!best.master.empty() && cell.area >= best.area)) {
      continue;
    }
    const auto bits = liberty::cell_truth(cell);
    if (!bits) {
      continue;
    }
    std::vector<unsigned> order(args.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      bool equal = true;
      for (unsigned x = 0; x < (1U << table.inputs); ++x) {
        unsigned p = 0;
        for (unsigned i = 0; i < order.size(); ++i) {
          p |= ((x >> order[i]) & 1) << i;
        }
        if (table.get(x) != bool((*bits >> p) & 1)) {
          equal = false;
          break;
        }
      }
      if (equal) {
        best = {name, {}, cell.area};
        for (auto i : order) {
          best.arguments.push_back(args[i]);
        }
        break;
      }
    } while (std::next_permutation(order.begin(), order.end()));
  }
  return best;
}
}  // namespace
bool equivalent_factor(const liberty::Comb_cell& original, const Wire_factor& f,
                       const std::map<std::string, liberty::Comb_cell>& cells) {
  const auto n = original.inputs.size();
  if (n > 6 || !cells.contains(f.divisor) || !cells.contains(f.top) || cells.at(f.divisor).inputs.size() != f.bound.size()
      || cells.at(f.top).inputs.size() != f.free.size() || !liberty::cell_truth(original)
      || !liberty::cell_truth(cells.at(f.divisor)) || !liberty::cell_truth(cells.at(f.top))) {
    return false;
  }
  Xag                  xag;
  std::vector<Xsignal> inputs, part, top;
  std::vector<Id>      basis;
  for (auto& name : original.inputs) {
    auto p = xag.input(name);
    inputs.push_back(p);
    basis.push_back(p.id);
  }
  for (auto i : f.bound) {
    if (i >= n) {
      return false;
    }
    part.push_back(inputs[i]);
  }
  auto divisor = emit(xag, function(cells.at(f.divisor)), part);
  auto signals = inputs;
  signals.push_back(divisor);
  if (!f.intermediate.empty()) {
    const auto at = cells.find(f.intermediate);
    if (at == cells.end() || !liberty::cell_truth(at->second) || at->second.inputs.size() != f.intermediate_args.size()) {
      return false;
    }
    std::vector<Xsignal> args;
    for (auto i : f.intermediate_args) {
      if (i >= signals.size()) {
        return false;
      }
      args.push_back(signals[i]);
    }
    signals.push_back(emit(xag, function(at->second), args));
  }
  for (auto i : f.free) {
    if (i >= signals.size()) {
      return false;
    }
    top.push_back(signals[i]);
  }
  auto   replacement = emit(xag, function(cells.at(f.top)), top);
  auto   reference   = emit(xag, function(original), inputs);
  Budget work{100000};
  auto   miter = basis_function(xag, xag.lxor(reference, replacement), basis, {}, work);
  return miter.status == Status::feasible && std::ranges::all_of(miter.table.words, [](auto x) { return x == 0; });
}
std::optional<Wire_push> wire_push(const liberty::Comb_cell& root, const liberty::Comb_cell& load, unsigned input,
                                   const std::map<std::string, liberty::Comb_cell>& cells) {
  if (!liberty::cell_truth(root) || !liberty::cell_truth(load) || input >= load.inputs.size()) {
    return {};
  }
  const auto bind_table = [&](const Truth_table& f) {
    std::vector<unsigned> args(f.inputs);
    std::iota(args.begin(), args.end(), 0);
    return bind_factor_cell(f, args, cells);
  };
  const auto new_root = bind_table(function(root).complement());
  if (new_root.master.empty()) {
    return {};
  }
  auto orig = function(load), flipped = orig;
  for (unsigned x = 0; x < 1U << orig.inputs; ++x) {
    flipped.set(x, orig.get(x ^ (1U << input)));
  }
  Wire_push result{new_root.master, {}, new_root.arguments, {}, false};
  if (flipped.inputs == 1 && !flipped.get(0) && flipped.get(1)) {
    result.bypass = true;
  } else {
    auto mapped = bind_table(flipped);
    if (mapped.master.empty()) {
      return {};
    }
    result.load           = mapped.master;
    result.load_arguments = std::move(mapped.arguments);
  }
  // Prove both interfaces independently. Each side input remains symbolic,
  // so the same result applies even when each load has different side nets.
  const auto proves = [&](const Truth_table& gold, const liberty::Comb_cell& replacement, const std::vector<unsigned>& order) {
    Xag                  xag;
    std::vector<Xsignal> pins, args;
    std::vector<Id>      basis;
    for (unsigned i = 0; i < gold.inputs; ++i) {
      auto x = xag.input(std::to_string(i));
      pins.push_back(x);
      basis.push_back(x.id);
    }
    for (auto idx : order) {
      args.push_back(pins.at(idx));
    }
    auto       mapped = emit(xag, function(replacement), args);
    Budget     work{100000};
    const auto measured = basis_function(xag, mapped, basis, {}, work);
    return measured.status == Status::feasible && measured.table == gold;
  };
  if (!proves(function(root).complement(), cells.at(result.root), result.root_arguments)
      || (!result.bypass && !proves(flipped, cells.at(result.load), result.load_arguments))) {
    return {};
  }
  return result;
}
std::vector<Wire_factor> wire_factors(const liberty::Comb_cell& original, const std::map<std::string, liberty::Comb_cell>& cells) {
  const auto               n = original.inputs.size();
  std::vector<Wire_factor> result;
  if (n < 3 || n > 6 || !liberty::cell_truth(original)) {
    return result;
  }
  const auto table = function(original);
  for (unsigned mask = 1; mask + 1 < (1U << n); ++mask) {
    if (std::popcount(mask) < 2) {
      continue;
    }
    Budget work{100000};
    auto   d = decompose_function(table, mask, n - 1, work);
    if (d.status != Status::feasible || d.divisors.size() != 1) {
      continue;
    }
    std::vector<unsigned> args{static_cast<unsigned>(n)};
    args.insert(args.end(), d.free.begin(), d.free.end());
    for (bool inv : {false, true}) {
      auto div = bind_factor_cell(inv ? d.divisors.front().complement() : d.divisors.front(), d.bound, cells);
      if (div.master.empty()) {
        continue;
      }
      auto fp = d.top;
      if (inv) {
        for (unsigned x = 0; x < (1U << fp.inputs); ++x) {
          fp.set(x, d.top.get(x ^ 1));
        }
      }
      auto top = bind_factor_cell(fp, args, cells);
      if (top.master.empty()) {
        continue;
      }
      Wire_factor f{div.master, top.master, div.arguments, top.arguments};
      if (equivalent_factor(original, f, cells)) {
        result.push_back(std::move(f));
      }
    }
  }
  // A 2:1 mux has overlapping support. Shannon expansion in XOR form is
  // lo XOR (sel AND (lo XOR hi)). Try both polarities of the select and
  // keep only candidates passing the same full XAG miter as disjoint cuts.
  if (n == 3) {
    Truth_table xor2(2), and2(2);
    xor2.set(1, true);
    xor2.set(2, true);
    and2.set(3, true);
    for (unsigned sel = 0; sel < 3; ++sel) {
      for (unsigned lo = 0; lo < 3; ++lo) {
        if (lo == sel) {
          continue;
        }
        const unsigned hi  = 3 - sel - lo;
        bool           mux = true;
        for (unsigned x = 0; x < 8; ++x) {
          if (table.get(x) != bool((x >> (((x >> sel) & 1) ? hi : lo)) & 1)) {
            mux = false;
            break;
          }
        }
        if (!mux) {
          continue;
        }
        auto diff  = bind_factor_cell(xor2, {lo, hi}, cells);
        auto gated = bind_factor_cell(and2, {sel, 3}, cells);
        auto merge = bind_factor_cell(xor2, {lo, 4}, cells);
        if (diff.master.empty() || gated.master.empty() || merge.master.empty()) {
          continue;
        }
        Wire_factor f{diff.master, merge.master, diff.arguments, merge.arguments, gated.master, gated.arguments};
        if (equivalent_factor(original, f, cells)) {
          result.push_back(std::move(f));
        }
      }
    }
  }
  return result;
}
}  // namespace livehd::placement
