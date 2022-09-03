//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <charconv>
#include <cstdio>
#include <functional>
#include <map>  // FIXME: replace for node_flat_map
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

  std::map<std::string, Channel> id2channel;

  size_t get_current_bucket() const { return (n_buckets * timestamp) / max_timestamp; }

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
    if (map_fd < 0)
      return;

    ::munmap((void *)map_buffer, map_size);
    ::close(map_fd);
  }

  bool open(std::string_view file_name);

  void dump() const;
};
