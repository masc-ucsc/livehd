// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_topology.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "rules_cc/cc/runfiles/runfiles.h"
#include "thirdparty/flute/flute.hpp"
namespace livehd::placement {
namespace {
namespace fs = std::filesystem;
std::unique_ptr<rules_cc::cc::runfiles::Runfiles> runfiles() {
  using rules_cc::cc::runfiles::Runfiles;
  std::string               error;
  std::unique_ptr<Runfiles> result(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
  if (result) {
    return result;
  }
  // Standard executable-adjacent Bazel runfiles. /proc is available on Linux;
  // macOS supplies _NSGetExecutablePath through the platform branch below.
  std::string executable;
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  executable.resize(size);
  _NSGetExecutablePath(executable.data(), &size);
  executable.resize(std::char_traits<char>::length(executable.c_str()));
#else
  std::error_code ec;
  executable = fs::read_symlink("/proc/self/exe", ec).string();
#endif
  result.reset(Runfiles::Create(executable, BAZEL_CURRENT_REPOSITORY, &error));
  if (!result) {
    throw std::runtime_error("cannot locate FLUTE runfiles: " + error);
  }
  return result;
}
}  // namespace
void initialize_flute_tables() {
  static std::once_flag once;
  std::call_once(once, [] {
    auto       files  = runfiles();
    const auto locate = [&](const char* name) {
      // Resolve the apparent archive name through this module's repository mapping.
      auto path = files->Rlocation(std::string("dreamplace/thirdparty/flute/lut.ICCAD2015/") + name);
      if (!fs::is_regular_file(path)) {
        throw std::runtime_error("FLUTE lookup table missing from runfiles: " + path);
      }
      return path;
    };
    const auto powv = locate("POWV9.dat"), post = locate("POST9.dat");
    flute::readLUT(powv.c_str(), post.c_str());
  });
}

Steiner_topology steiner_topology(const std::vector<Steiner_location>& pins) {
  if (pins.empty()) {
    throw std::invalid_argument("Steiner topology requires pins");
  }
  std::map<Steiner_location, size_t> vertices;
  constexpr auto                     limit = std::numeric_limits<int>::max() / 4;
  for (const auto& p : pins) {
    if (p.first < -limit || p.first > limit || p.second < -limit || p.second > limit) {
      throw std::invalid_argument("coordinates exceed FLUTE integer range");
    }
    vertices.emplace(p, 0);
  }
  if (vertices.size() > 1000) {
    throw std::invalid_argument("Steiner topology exceeds FLUTE degree limit");
  }
  Steiner_topology out;
  std::vector<int> xs, ys;
  for (auto& [p, index] : vertices) {
    index = out.locations.size();
    out.locations.push_back(p);
    xs.push_back(p.first);
    ys.push_back(p.second);
  }
  for (const auto& p : pins) {
    out.terminals.push_back(vertices.at(p));
  }
  if (vertices.size() == 1) {
    return out;
  }
  initialize_flute_tables();
  auto                                                 tree = flute::flute(xs.size(), xs.data(), ys.data(), 3);
  // Keep ownership across allocations/validation failures below.
  std::unique_ptr<flute::Branch, decltype(&std::free)> branches(tree.branch, &std::free);
  if (!branches || tree.deg != static_cast<int>(xs.size())) {
    throw std::runtime_error("invalid FLUTE topology");
  }
  const auto vertex = [&](Steiner_location p) {
    auto [it, added] = vertices.emplace(p, out.locations.size());
    if (added) {
      out.locations.push_back(p);
    }
    return it->second;
  };
  for (int i = 0; i < 2 * tree.deg - 2; ++i) {
    const auto& a = tree.branch[i];
    if (a.n < 0 || a.n >= 2 * tree.deg - 2) {
      throw std::runtime_error("invalid FLUTE branch index");
    }
    const auto& b  = tree.branch[a.n];
    const auto  ai = vertex({a.x, a.y}), bi = vertex({b.x, b.y});
    if (ai != bi) {
      out.edges.emplace_back(std::min(ai, bi), std::max(ai, bi));
    }
  }
  std::sort(out.edges.begin(), out.edges.end());
  out.edges.erase(std::unique(out.edges.begin(), out.edges.end()), out.edges.end());
  std::vector<size_t> parent(out.locations.size());
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](size_t i) {
    while (parent[i] != i) {
      i = parent[i] = parent[parent[i]];
    }
    return i;
  };
  // Zero-length branch coalescing must leave a tree for RC traversal.
  std::erase_if(out.edges, [&](const auto& edge) {
    auto a = root(edge.first), b = root(edge.second);
    if (a == b) {
      return true;
    }
    parent[b] = a;
    return false;
  });
  if (out.edges.size() + 1 != out.locations.size()) {
    throw std::runtime_error("disconnected FLUTE topology");
  }
  return out;
}
}  // namespace livehd::placement
