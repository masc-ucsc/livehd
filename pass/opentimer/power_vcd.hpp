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
  size_t n_buckets{100};

  int         map_fd{-1};
  const char *map_buffer{nullptr};
  const char *map_end{nullptr};
  size_t      map_size{0};

  std::vector<std::string_view> scope_stack;

  std::string fname;

  struct Channel {
    std::vector<std::string> hier_name;
    std::vector<size_t>      transitions;
  };

  size_t      deepest_vcd_hier_name_level{0};
  std::string deepest_vcd_hier_name;

  absl::node_hash_map<std::string, Channel> id2channel;

  absl::flat_hash_map<std::string, double> hier_name2power;

  [[nodiscard]] size_t get_current_bucket() const { return (n_buckets * timestamp) / max_timestamp; }

  bool                                      find_max_time();
  const char                               *skip_command(const char *ptr) const;
  const char                               *skip_word(const char *ptr) const;
  std::pair<const char *, std::string_view> parse_word(const char *ptr) const;

  const char *parse_instruction(const char *ptr);
  const char *parse_timestamp(const char *ptr);
  const char *parse_sample(const char *ptr);

  void parse();

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

  void clear_power() { hier_name2power.clear(); }

  void add(std::string_view hier_name, double power) { hier_name2power.insert_or_assign(hier_name, power); }

  void compute(std::string_view odir) const;
};
