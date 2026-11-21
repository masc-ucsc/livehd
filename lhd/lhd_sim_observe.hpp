//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace lhd::sim_observe {

struct Signal {
  std::string name;
  std::string alias;
  std::string kind;
  long        bits          = 0;
  long        declared_bits = 0;
  long        size          = -1;
  bool        is_signed     = false;
};

struct Catalog {
  std::string         test;
  std::string         clock;
  std::vector<Signal> sigs;
};

struct Request {
  bool        list_signals = false;
  std::string probe;
  long        probe_from = -1;
  long        probe_to   = -1;
  std::string break_when;
  std::string query;
};

struct Range {
  int64_t from;
  int64_t to;  // inclusive
};

struct Column {
  size_t  signal;
  int64_t index                            = -1;  // a memory word, or a scalar if negative
  auto    operator<=>(const Column&) const = default;
};

class Observer {
public:
  Observer(const Catalog& catalog, const Request& request, int64_t fail_cycle, uint64_t seed);
  ~Observer();

  const std::vector<Column>& columns() const;
  const std::vector<Range>&  ranges() const;
  bool                       wants_sample(int64_t cycle) const;
  void                       sample(int64_t cycle, std::vector<std::string>& row);
  void                       read_samples(std::istream& in);
  void                       replayed(int64_t first, int64_t count);
  std::string                debug_json() const;
  std::string                query_json() const;
  std::string                break_summary() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lhd::sim_observe
