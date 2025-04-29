// vcd_reader.hpp
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"

// Base class: memory-map & parse a VCD file inline.  Derived classes must
// implement on_value(hier_name, value) to process each sample.
class Vcd_reader {
public:
  explicit Vcd_reader(size_t n_buckets = 100);
  virtual ~Vcd_reader();

  // Open & mmap the file.  Returns false on any error.
  bool open(const std::string& filename);

  // Drive the parsing & callbacks.
  void process();

protected:
  // Called for every value change:
  //   bucket:   quantized time‐bucket (0..n_buckets_-1)
  //   hier_name: full hierarchical signal name (e.g. "top,modA,signal")
  //   value:    '0','1','x','z',...
  virtual void on_value(const std::string& hier_name, std::string_view value) = 0;

  size_t      n_buckets_;       // number of time buckets
  size_t      max_timestamp_;   // found by scanning the file
  size_t      num_vcd_cycles_;  // number of distinct timestamps seen
  double      timescale_;       // parsed from `$timescale`
  size_t      timestamp_;       // current timestamp during parse
  std::string filename_;

private:
  int                                           map_fd_;
  const char *                                  map_buffer_, *map_end_;
  size_t                                        map_size_;
  std::vector<std::string>                      scope_stack_;
  absl::flat_hash_map<std::string, std::string> id2hier_;

  bool find_max_time();
  void parse();

  const char*                              skip_command(const char* ptr) const;
  const char*                              skip_word(const char* ptr) const;
  std::pair<const char*, std::string_view> parse_word(const char* ptr) const;
  const char*                              parse_instruction(const char* ptr);
  const char*                              parse_timestamp(const char* ptr);
  const char*                              parse_sample(const char* ptr);

  // quantize the current timestamp into a bucket index
  [[nodiscard]] size_t get_current_bucket() const;
};
