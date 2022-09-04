//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "power_vcd.hpp"
#include "pass.hpp"

#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"

//#define bithack_haszero(v) (((v) - 0x01010101UL) & ~(v) & 0x80808080UL)
//#define bithack_hasvalue(x,n) (haszero((x) ^ (~0UL/255 * (n))))

#define VALUES "0123456789zZxXbU-"  // allowed bus values/types

bool Power_vcd::find_max_time() {
  // Look for max timestep to create buckets
  // Start from end of file and look for \n#

  const char *ptr = map_end;

  while (ptr > map_buffer) {
    if (*ptr != '#') {
      --ptr;
      continue;
    }
    if (*(ptr - 1) != '\n') {
      --ptr;
      continue;
    }

    auto [end_ptr, ec] = std::from_chars(ptr + 1, map_end, max_timestamp, 10);
    if (ec != std::errc()) {  // no error
      --ptr;
      continue;
    }

    if (max_timestamp == 0)
      return false;

    return true;
  }

  return false;
}

const char *Power_vcd::skip_command(const char *ptr) const {
  while (ptr < map_end) {
    if (*ptr != '$') {
      ++ptr;
      continue;
    }
    ++ptr;

    if (strncmp(ptr, "end", 3) == 0)
      return ptr + 3;
  }

  return ptr;
}

const char *Power_vcd::skip_word(const char *ptr) const {
  // 1st spaces first
  while (ptr < map_end && isspace(*ptr)) {
    ++ptr;
  }
  // 2nd skip word
  while (ptr < map_end && !isspace(*ptr)) {
    ++ptr;
  }

  return ptr;
}

std::pair<const char *, std::string_view> Power_vcd::parse_word(const char *ptr) const {
  // 1st spaces first
  while (ptr < map_end && isspace(*ptr)) {
    ++ptr;
  }
  const char *str_start = ptr;
  // 2nd skip word
  while (ptr < map_end && !isspace(*ptr)) {
    ++ptr;
  }

  return std::make_pair(ptr, std::string_view(str_start, ptr - str_start));
}

const char *Power_vcd::parse_instruction(const char *ptr) {
  if (strncmp("var", ptr, 3) == 0) {
    ptr += 3;

    ptr = skip_word(ptr);  // Skip type
    ptr = skip_word(ptr);  // Skip size

    auto [ptr2, var_id] = parse_word(ptr);  // get var_id
    ptr                 = ptr2;

    auto [ptr3, var_name] = parse_word(ptr);  // get var_id
    ptr                   = ptr3;

    std::string hier_name;
    for (auto &str : scope_stack) {
      hier_name.append(str);
      hier_name.append(",");
    }
    hier_name.append(var_name);
    auto it = id2channel.find(std::string(var_id));
    if (it == id2channel.end()) {
      id2channel[std::string(var_id)].transitions.resize(n_buckets);
    }
    id2channel[std::string(var_id)].hier_name.emplace_back(hier_name);

    //fmt::print("hier_name:{} var_id:{}\n", hier_name, var_id);

  } else if (strncmp("scope", ptr, 5) == 0) {
    ptr += 5;
    ptr = skip_word(ptr);  // Skip type

    auto [ptr2, scope_name] = parse_word(ptr);  // get name
    ptr                     = ptr2;

    // fmt::print("scope:{}\n", scope_name);
    if (scope_name != "TOP")
      scope_stack.emplace_back(scope_name);

  } else if (strncmp("date", ptr, 4) == 0) {
    ptr += 4;
  } else if (strncmp("version", ptr, 7) == 0) {
    ptr += 7;
  } else if (strncmp("timescale", ptr, 9) == 0) {
    ptr += 9;
  } else if (strncmp("comment", ptr, 7) == 0) {
    ptr += 7;
  } else if (strncmp("upscope", ptr, 7) == 0) {
    if (!scope_stack.empty())
      scope_stack.pop_back();
    ptr += 7;
  } else if (strncmp("enddefinitions", ptr, 14) == 0) {
    ptr += 14;
  } else if (strncmp("dumpvars", ptr, 8) == 0) {
    ptr += 8;
  } else if (strncmp("end", ptr, 3) == 0) {
    ptr += 3;
  } else {
    fmt::print("ERROR: unknown token: {}\n", ptr);
  }

  ptr = skip_command(ptr);

  return ptr;
}

const char *Power_vcd::parse_timestamp(const char *ptr) {
  auto [end_ptr, ec] = std::from_chars(ptr, map_end, timestamp, 10);

  // fmt::print("timestamp:{}\n", timestamp);

  return end_ptr;
}

const char *Power_vcd::parse_sample(const char *ptr) {
  assert(strchr(VALUES, *ptr));

  auto [ptr2, first] = parse_word(ptr);
  ptr                = ptr2;

  auto pos = get_current_bucket();

  if (first.front() == 'b') {
    auto [ptr3, second] = parse_word(ptr);
    ptr                 = ptr3;

    // fmt::print("ID:{} val:{}\n", second,first);

    id2channel[std::string(second)].transitions[pos]++;
  } else {
    // fmt::print("ID:{} val:{}\n", first.substr(1), first.front());
    id2channel[std::string(first.substr(1))].transitions[pos]++;
  }

  return ptr;
}

void Power_vcd::parse() {
  const char *ptr = map_buffer;

  while (ptr < map_end) {
    if (std::isspace(*ptr)) {
      ++ptr;
      continue;
    }

    if (*ptr == '$') {
      ptr = parse_instruction(++ptr);
    } else if (*ptr == '#') {
      ptr = parse_timestamp(++ptr);
    } else {
      ptr = parse_sample(ptr);
    }
  }
}

bool Power_vcd::open(std::string_view file_name) {
  fname = file_name;

  map_fd = ::open(fname.c_str(), O_RDONLY);
  if (map_fd < 0) {
    fmt::print("ERROR: could not open vcd file {}\n", fname);
    return false;
  }

  struct stat sb;
  fstat(map_fd, &sb);
  map_size = sb.st_size;

  map_buffer = (char *)::mmap(NULL, map_size, PROT_READ, MAP_PRIVATE, map_fd, 0);
  if (map_buffer == MAP_FAILED) {
    fmt::print("ERROR: could not map vcd file {}\n", fname);
    ::close(map_fd);
    map_fd = -1;
    return false;
  }
  map_end = map_buffer + map_size;

  auto ok = find_max_time();
  fmt::print("INFO: vcd max_timestamp:{}\n", max_timestamp);

  if (ok)
    parse();

  return ok;
}

void Power_vcd::dump() const {
  for (const auto &e : id2channel) {
    fmt::print("------------ id:{}\n", e.first);
    for (const auto &str : e.second.hier_name) {
      fmt::print("  {}\n", str);
    }
    for (const auto &v : e.second.transitions) {
      fmt::print("  {}", v);
    }
    fmt::print("\n");
  }
}

void Power_vcd::compute(std::string_view odir) const {

  absl::node_hash_map<std::string, std::vector<double>> module_trace;

  double transitions=0;
  double max_transitions=1;

  for (const auto &e : id2channel) {
    std::vector<double> trace_step;

    for (const auto &hier_name : e.second.hier_name) {
      auto it = hier_name2power.find(hier_name);
      if (it == hier_name2power.end())
        continue;

      if (trace_step.empty()) {
        trace_step.reserve(e.second.transitions.size());

        for (const auto &v : e.second.transitions) {
          transitions += v;
          trace_step.emplace_back(v * it->second);
        }
        max_transitions += max_timestamp;
      }

      const std::vector<std::string> m = absl::StrSplit(hier_name, ',');
      // top_1,imm1,imm2,lib,pin  // top_1 + imm1 + imm2 traces

      std::string hier_path;

      for(int i=0;i<static_cast<int>(m.size()-2);++i) {
        if (hier_path.empty())
          hier_path = m[i];
        else
          absl::StrAppend(&hier_path, ":", m[i]); // : is easier for file names

        auto it2 = module_trace.find(hier_path);
        if (it2 == module_trace.end()) {
          module_trace[hier_path] = trace_step;
        }else{
          for(auto j=0u;j<trace_step.size();++j) {
            module_trace[hier_path][j] += trace_step[j];
          }
        }
      }
    }
  }

  double step = max_timestamp/n_buckets;
  for(const auto &e:module_trace) {

    auto out_fname = absl::StrCat(odir,"/", fname, "_", e.first, ".power.trace");

    std::FILE *f = std::fopen(out_fname.c_str(), "w");
    if (f == nullptr) {
      Pass::error("could not create {} output power trace", out_fname);
      return;
    }

    double x = 0;
    for(double v:e.second) {
      fmt::print(f,"{} {}\n", x, v/max_timestamp);
      x += step;
    }

    std::fclose(f);
  }

  // NOTE: 2x because clock does 2x transitions (max_timestamp)
  fmt::print("average activity rate {}\n", 2*transitions/max_transitions);
}

