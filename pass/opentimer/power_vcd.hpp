//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sys/mman.h>

#include <cassert>
#include <charconv>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"

class Power_vcd {
protected:
  size_t timestamp{0};
  size_t max_timestamp{1};
  size_t max_edges{1};
  double timescale{1e-12};
  double lib_timescale{1e-9};
  size_t n_buckets{100};

  int         map_fd{-1};
  const char *map_buffer{nullptr};
  const char *map_end{nullptr};
  size_t      map_size{0};

  // vcd_hier_skip and vcd_hier_xtra_top are needed to match names between VCD
  // and netlist because they have different top hierarchy.
  //
  // net_hier_name:  gcd,_333_,A
  // vcd_hier_name:  foo,gcd1,_333_,A
  //
  // vcd_hier_skip is the number if characters to drop from vcd_hier_name
  // vcd_hier_xtra_top is the hierarchy to add afterwards to vcd_hier to match net_hier_name
  //
  // vcd_hier_name goo,gcd1,_333_,A -> _333_,A -> gcd,_333_,A

  size_t      vcd_hier_skip{0};
  std::string vcd_hier_xtra_top;

  double power_total{0};
  size_t power_samples{0};

  std::vector<std::string_view> scope_stack;

  std::string fname;

  struct Channel {
    std::vector<std::string> hier_name;
    std::vector<size_t>      transitions;
  };

  absl::node_hash_map<std::string, Channel> id2channel;
  absl::flat_hash_map<std::string, double> net_hier_name2power;

  [[nodiscard]] size_t get_current_bucket() const { return (n_buckets * timestamp) / max_timestamp; }

  bool                                      find_max_time();
  const char                               *skip_command(const char *ptr) const;
  const char                               *skip_word(const char *ptr) const;
  std::pair<const char *, std::string_view> parse_word(const char *ptr) const;

  const char *parse_instruction(const char *ptr);
  const char *parse_timestamp(const char *ptr);
  const char *parse_sample(const char *ptr);

  void parse();
  void find_hier_skip();

public:
  Power_vcd() = default;
  ~Power_vcd() {
    if (map_fd < 0) {
      return;
    }

    ::munmap((void *)map_buffer, map_size);
    ::close(map_fd);
  }

  bool open(std::string_view file_name);

  void dump() const;

  void set_tech_timeunit(double ts) { lib_timescale = ts; }

  void clear_power() { net_hier_name2power.clear(); }

  void add(std::string_view hier_name, double power) {
    // x2 power because VCD uses transition (power for 1->0 and 0->1)
    net_hier_name2power.insert_or_assign(hier_name, 2*power);
  }

  void compute(std::string_view odir);

  [[nodiscard]] std::string_view get_filename() const { return fname; }
  [[nodiscard]] double get_power_average() const {
    if (power_samples)
      return power_total/static_cast<double>(power_samples);
    return 0;
  }

};
