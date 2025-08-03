// vcd_reader.cpp
#include "vcd_reader.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>

#define VALUES "0123456789zZxXbU-"

Vcd_reader::Vcd_reader(size_t n_buckets)
    : n_buckets_(n_buckets)
    , max_timestamp_(0)
    , num_vcd_cycles_(0)
    , timescale_(1.0)
    , timestamp_(0)
    , map_fd_(-1)
    , map_buffer_(nullptr)
    , map_end_(nullptr)
    , map_size_(0) {}

Vcd_reader::~Vcd_reader() {
  if (map_fd_ >= 0) {
    munmap((void*)map_buffer_, map_size_);
    close(map_fd_);
  }
}

bool Vcd_reader::open(const std::string& filename) {
  filename_ = filename;
  map_fd_   = ::open(filename.c_str(), O_RDONLY);
  if (map_fd_ < 0) {
    return false;
  }

  struct stat st;
  fstat(map_fd_, &st);
  map_size_   = st.st_size;
  map_buffer_ = (const char*)mmap(nullptr, map_size_, PROT_READ, MAP_PRIVATE, map_fd_, 0);
  if (map_buffer_ == MAP_FAILED) {
    close(map_fd_);
    map_fd_ = -1;
    return false;
  }
  map_end_ = map_buffer_ + map_size_;
  if (!find_max_time()) {
    return false;
  }
  return true;
}

void Vcd_reader::process() {
  assert(map_buffer_);
  parse();
}

bool Vcd_reader::find_max_time() {
  // scan backward for the last “\n#<digits>”
  const char* ptr = map_end_ - 1;
  while (ptr > map_buffer_) {
    if (*ptr == '#' && ptr > map_buffer_ && *(ptr - 1) == '\n') {
      auto [endp, ec] = std::from_chars(ptr + 1, map_end_, max_timestamp_);
      if (ec == std::errc() && max_timestamp_ > 0) {
        return true;
      }
    }
    --ptr;
  }
  return false;
}

size_t Vcd_reader::get_current_bucket() const {
  if (num_vcd_cycles_ < n_buckets_) {
    return timestamp_;
  }
  // scale timestamp into [0..n_buckets_-1]
  return (n_buckets_ * timestamp_) / max_timestamp_;
}

const char* Vcd_reader::skip_command(const char* ptr) const {
  // skip until matching “end”
  while (ptr < map_end_) {
    if (*ptr == '$') {
      ++ptr;
      if (std::strncmp(ptr, "end", 3) == 0) {
        return ptr + 3;
      }
    }
    ++ptr;
  }
  return ptr;
}

const char* Vcd_reader::skip_word(const char* ptr) const {
#if 0
  while (ptr < map_end_ && std::isspace(*ptr)) {
    ++ptr;
  }
  while (ptr < map_end_ && !std::isspace(*ptr)) {
    ++ptr;
  }
#else
  size_t head = strspn(ptr, " \n");
  ptr += head;

  size_t len = strcspn(ptr, " \n");
  ptr += len;
#endif

  return ptr;
}

std::pair<const char*, std::string_view> Vcd_reader::parse_word(const char* ptr) const noexcept {
  // Not allowed characters for spaces
  assert(ptr[0] != '\r');
  assert(ptr[0] != '\f');
  assert(ptr[0] != '\t');

  // skip leading whitespace
  size_t head = strspn(ptr, " \n");
  ptr += head;

  // length of next token
  size_t len   = strcspn(ptr, " \n");
  auto   start = ptr;
  ptr += len;
  return {ptr, std::string_view(start, len)};
}

const char* Vcd_reader::parse_instruction(const char* ptr) {
  if (std::strncmp(ptr, "scope", 5) == 0) {
    ptr += 5;
    ptr             = skip_word(ptr);
    auto [p2, name] = parse_word(ptr);
    ptr             = p2;
    scope_stack_.emplace_back(name);
  } else if (std::strncmp(ptr, "upscope", 7) == 0) {
    if (!scope_stack_.empty()) {
      scope_stack_.pop_back();
    }
    ptr += 7;
  } else if (std::strncmp(ptr, "var", 3) == 0) {
    ptr += 3;
    ptr            = skip_word(ptr);  // type
    ptr            = skip_word(ptr);  // size
    auto [p2, id]  = parse_word(ptr);
    ptr            = p2;
    auto [p3, sig] = parse_word(ptr);
    ptr            = p3;
    // build full hierarchical name:
    std::string fullname;
    for (auto& s : scope_stack_) {
      fullname += s;
      fullname.push_back(',');
    }
    fullname += std::string(sig);
    auto it = id2hier_.find(id);
    if (it == id2hier_.end()) {
      id2hier_[std::string(id)] = std::move(fullname);
    } else {
      alias_map_[it->second].emplace_back(std::move(fullname));
    }
  } else if (std::strncmp(ptr, "timescale", 9) == 0) {
    ptr += 9;
    auto [p2, ts] = parse_word(ptr);
    ptr           = p2;
    // simple parse, e.g. “ns”
    if (ts.find("fs") != std::string::npos) {
      timescale_ = 1e-15;
    } else if (ts.find("ps") != std::string::npos) {
      timescale_ = 1e-12;
    } else if (ts.find("ns") != std::string::npos) {
      timescale_ = 1e-9;
    } else if (ts.find("us") != std::string::npos) {
      timescale_ = 1e-6;
    }
  } else {
    // ignore date, version, comment, etc.
  }
  return skip_command(ptr);
}

const char* Vcd_reader::parse_timestamp(const char* ptr) {
  auto [endp, ec] = std::from_chars(ptr, map_end_, timestamp_);
  if (ec == std::errc()) {
    num_vcd_cycles_ = std::max(num_vcd_cycles_, timestamp_ + 1);
  }
  return endp;
}

const char* Vcd_reader::parse_sample(const char* ptr) {
  assert(std::strchr(VALUES, *ptr));
  // get the “word” (either “b0101 id” or “0id”)
  auto [p2, w] = parse_word(ptr);
  ptr          = p2;
  std::string_view value;
  std::string_view id;
  if (w.front() == 'b') {
    // bus: b<value> <id>
    auto [p3, idv] = parse_word(ptr);
    ptr            = p3;
    value          = w.substr(1);
    id             = idv;
  } else {
    // scalar: <v><id>
    value = w.substr(0, 1);
    id    = w.substr(1);
  }
  auto it = id2hier_.find(id);
  if (it != id2hier_.end()) {
    on_value(it->second, value);
  }
  return ptr;
}

const std::vector<std::string>& Vcd_reader::get_alias(std::string_view name) const {
  static std::vector<std::string> empty_map;

  auto it = alias_map_.find(name);
  if (it == alias_map_.end()) {
    return empty_map;
  }

  return it->second;
}

void Vcd_reader::parse() {
  const char* ptr = map_buffer_;
  while (ptr < map_end_) {
    if (std::isspace(*ptr)) {
      ++ptr;
      continue;
    }
    if (*ptr == '$') {
      ptr = parse_instruction(ptr + 1);
    } else if (*ptr == '#') {
      ptr = parse_timestamp(ptr + 1);
    } else {
      ptr = parse_sample(ptr);
    }
  }
}
