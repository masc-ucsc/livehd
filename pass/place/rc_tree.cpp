// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "rc_tree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "require.hpp"

namespace livehd::placement::late {
namespace {
void nonnegative(double value) { require_nonnegative(value, "RC tree requires finite nonnegative capacitance/resistance"); }
}  // namespace

Rc_tree_state analyze_rc_tree(const physical::Rc_net& net, unsigned driver, const std::vector<std::array<double, 2>>& node_cap_ff) {
  physical::validate_rc_tree(net);
  const size_t count = net.nodes.size();
  if (driver >= count || (!node_cap_ff.empty() && node_cap_ff.size() != count)) {
    throw std::invalid_argument("RC driver or node-capacitance count differs from tree");
  }
  std::vector<std::array<double, 2>> cap(count), y2(count), y3(count);
  for (size_t i = 0; i < count; ++i) {
    nonnegative(net.nodes[i].capacitance_ff);
    cap[i] = node_cap_ff.empty() ? std::array{net.nodes[i].capacitance_ff, net.nodes[i].capacitance_ff} : node_cap_ff[i];
    nonnegative(cap[i][0]);
    nonnegative(cap[i][1]);
  }
  std::vector<std::vector<std::pair<unsigned, double>>> neighbors(count);
  for (const auto& edge : net.edges) {
    nonnegative(edge.resistance_ohm);
    neighbors[edge.from].emplace_back(edge.to, edge.resistance_ohm);
    neighbors[edge.to].emplace_back(edge.from, edge.resistance_ohm);
  }
  std::vector<unsigned> parent(count, std::numeric_limits<unsigned>::max()), order{driver};
  std::vector<double>   resistance(count);
  parent[driver] = driver;
  for (size_t i = 0; i < order.size(); ++i) {
    const auto at = order[i];
    for (const auto& [child, r] : neighbors[at]) {
      if (child != parent[at]) {
        parent[child]     = at;
        resistance[child] = r;
        order.push_back(child);
      }
    }
  }
  // For a subtree Y(s)=c*s+a*s^2+b*s^3 behind R, series expansion gives
  // Y/(1+R*Y)=c*s+(a-R*c^2)*s^2+(b-2*R*c*a+R^2*c^3)*s^3.
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const auto at = *it;
    if (at == driver) {
      continue;
    }
    const auto   p = parent[at];
    const double r = resistance[at];
    for (unsigned t = 0; t < 2; ++t) {
      const double c = cap[at][t], a = y2[at][t], b = y3[at][t];
      cap[p][t] += c;
      y2[p][t]  += a - r * c * c;
      y3[p][t]  += b - 2 * r * c * a + r * r * c * c * c;
    }
  }
  Rc_tree_state result;
  result.total_cap_ff = cap[driver];
  result.elmore_ps.resize(count);
  for (auto at : order) {
    for (unsigned t = 0; t < 2; ++t) {
      nonnegative(cap[at][t]);
      if (at != driver) {
        result.elmore_ps[at][t] = result.elmore_ps[parent[at]][t] + .001 * resistance[at] * cap[at][t];
        nonnegative(result.elmore_ps[at][t]);
      }
    }
  }
  for (unsigned t = 0; t < 2; ++t) {
    const double c = cap[driver][t], a = -y2[driver][t], b = y3[driver][t];
    nonnegative(a);
    nonnegative(b);
    auto& pi       = result.pi[t];
    pi.near_cap_ff = c;
    if (a == 0 || b == 0) {
      continue;
    }
    // A pi network has Y(s)=Ctotal*s-R*Cfar^2*s^2+R^2*Cfar^3*s^3.
    const double far = (a / b) * a;
    if (far > c * (1 + 1e-12)) {
      throw std::invalid_argument("nonpassive RC input-admittance moments");
    }
    pi.far_cap_ff     = std::min(c, far);
    pi.near_cap_ff    = c - pi.far_cap_ff;
    pi.resistance_ohm = (b / a) / pi.far_cap_ff;
    nonnegative(pi.far_cap_ff);
    nonnegative(pi.near_cap_ff);
    nonnegative(pi.resistance_ohm);
  }
  return result;
}
}  // namespace livehd::placement::late
