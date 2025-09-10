//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "file_output.hpp"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <format>
#include <iostream>
#include <print>

#include "iassert.hpp"

File_output::File_output(std::string_view fname) : filename(fname), sz(0), aborted(false) {}

// Portable, thread-safe error string helper for GNU & POSIX strerror_r.
#include <cerrno>
#include <cstring>
#include <string>

static std::string strerror_threadsafe(int err) {
  char buf[256];

#if defined(__GLIBC__) && defined(_GNU_SOURCE)
  // GNU variant: returns char* (may point to static storage or buf)
  char *msg = ::strerror_r(err, buf, sizeof(buf));
  if (msg && *msg) {
    return std::string(msg);
  }
  return "Unknown error";
#else
  // XSI/POSIX variant (macOS, BSDs, many libcs): returns int and writes into buf
  int rc = ::strerror_r(err, buf, sizeof(buf));
  if (rc == 0 && *buf) {
    return std::string(buf);
  }
  // Best-effort fallback if strerror_r fails
  return "Unknown error " + std::to_string(err);
#endif
}

File_output::~File_output() {
  if (aborted) {
    return;
  }

  //---------------------------------- OPEN

  int fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
  I(fd >= 0);  // throw std::runtime_error(std::format("could not create destination {} file (permissions?)", filename));

  size_t map_size = sz;
  map_size >>= 12;
  ++map_size;
  map_size <<= 12;
  auto ok = ftruncate(fd, map_size);
  I(ok == 0);

  void *base = ::mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
  if (base == MAP_FAILED) {
    std::println("mmap errno:{} for filename{}\n", strerror_threadsafe(errno), filename);
    I(false);
  }

  //---------------------------------- SAVE

  char *ptr = static_cast<char *>(base);
  for (const auto &e : sequence) {
    memcpy(ptr, e.data(), e.size());
    ptr += e.size();
    I(static_cast<size_t>(ptr - static_cast<char *>(base)) <= sz);
  }

  //---------------------------------- CLOSE
  ::munmap(base, map_size);
  if (map_size != sz) {
    auto ok2 = ftruncate(fd, sz);
    (void)ok2;
  }
  ::close(fd);
}
