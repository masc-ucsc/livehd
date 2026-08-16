//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "file_output.hpp"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <format>
#include <iostream>
#include <print>

#include "diag.hpp"
#include "iassert.hpp"

File_output::File_output(std::string_view fname) : filename(fname), sz(0), aborted(false) {
  // A path component longer than NAME_MAX cannot be open()ed, and the write
  // happens in the DESTRUCTOR -- where the failure used to hit `I(fd >= 0)` and
  // SIGABRT with no diagnostic at all. cgen names its output `<unit>.v` after
  // the Pyrope unit (`<file-stem>.<module>`), so a design with a long generated
  // module name blows past the limit on its own: the corpus case was
  // OpenABC's `bp_fe_top_itlb_vaddr_width_p56_..._tag_width_p10` (205 chars).
  // Refuse EARLY, where reporting is still possible, and disarm the destructor.
  const auto slash = filename.find_last_of('/');
  const auto base  = slash == std::string::npos ? std::string_view{filename} : std::string_view{filename}.substr(slash + 1);
  if (base.size() > static_cast<size_t>(NAME_MAX)) {
    aborted = true;
    livehd::diag::err("core.file_output", "filename-too-long", "io")
        .msg("cannot create '{}': the file name is {} characters, over the {} the filesystem allows",
             base,
             base.size(),
             NAME_MAX)
        .hint("shorten the module/unit name, or emit to a directory with --emit-dir")
        .emit();
  }
}

// Portable, thread-safe error string helper for GNU & POSIX strerror_r.
#include <cerrno>
#include <cstring>
#include <string>

static std::string strerror_threadsafe(int err) {
  char buf[256];

#if defined(__GLIBC__) && defined(_GNU_SOURCE)
  // GNU variant: returns char* (may point to static storage or buf)
  char* msg = ::strerror_r(err, buf, sizeof(buf));
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

namespace {

#if defined(__APPLE__)
timespec stat_atime(const struct stat& st) { return st.st_atimespec; }
timespec stat_mtime(const struct stat& st) { return st.st_mtimespec; }
#else
timespec stat_atime(const struct stat& st) { return st.st_atim; }
timespec stat_mtime(const struct stat& st) { return st.st_mtim; }
#endif

bool same_time(const timespec& lhs, const timespec& rhs) { return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec; }

timespec next_time(timespec value) {
  if (++value.tv_nsec == 1000000000L) {
    ++value.tv_sec;
    value.tv_nsec = 0;
  }
  return value;
}

}  // namespace

// Does the file already hold EXACTLY the bytes we are about to write? Then the
// write is a no-op that would still stamp a new mtime, and every downstream
// build system keys staleness on mtime — so skipping it is what turns
// "regenerate the tree" into an incremental rebuild.
//
// This is the mechanism behind the sim host build's incrementality, and it is
// what gives it INTERFACE-vs-BODY precision for free: inou.cgen.sim re-emits a
// module's .hpp and .cpp together whenever its structural digest moves, so a
// body-only edit rewrites the .cpp while the .hpp comes out byte-identical.
// Without this check the untouched header still gets a fresh mtime and every
// parent that includes it rebuilds — the exact recompilation the by-value
// header split exists to avoid. With it, the byte compare does the
// interface/body split implicitly, and no separate interface digest is needed.
//
// Cost is one read of a file we were about to overwrite anyway, and only when
// it already exists. Correctness note: this compares CONTENT, so it can never
// skip a write that would have changed the file. core/tests/file_output_test
// pins that, because a wrong `true` here leaves a stale artifact on disk that
// every downstream consumer then agrees with (the emit manifests hash the file
// back, so they would record the stale bytes as correct).
//
// SCOPE: this covers File_output only, which today means the Verilog emitter
// (inou/cgen/cgen_verilog.cpp) and the sim emitter (inou/cgen/cgen_sim.cpp plus
// the sim driver). `pass.prp_writer` still writes Pyrope through a plain
// std::ofstream, so a re-emitted Pyrope tree is NOT yet mtime-quiet — routing
// it through here is the obvious follow-up, and would cut the diff churn the
// manual regeneration recipe warns about.
bool File_output::same_on_disk() const {
  int fd = ::open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;  // no such file (the common first-emission case)
  }
  bool same = false;
  auto len  = ::lseek(fd, 0, SEEK_END);
  if (len >= 0 && static_cast<size_t>(len) == sz) {
    // Size matches, so compare the bytes. Streaming the pieces against a
    // modest buffer keeps this allocation-free for the large emissions
    // (a whole re-emitted Pyrope tree) where a second full copy would hurt.
    same = true;
    if (::lseek(fd, 0, SEEK_SET) == 0) {
      // Streamed against a fixed buffer rather than slurping the file: the
      // emissions this guards are per-module C++ bodies, but nothing stops a
      // caller from writing something far larger, and a second full copy of it
      // would be a poor trade for a comparison.
      char   buf[64 * 1024];
      size_t have = 0, off = 0;  // `have` bytes buffered from disk, `off` consumed
      for (const auto& e : sequence) {
        size_t done = 0;
        while (done < e.size()) {
          if (off == have) {
            auto n = ::read(fd, buf, sizeof buf);
            if (n <= 0) {
              same = false;
              break;
            }
            have = static_cast<size_t>(n);
            off  = 0;
          }
          const size_t chunk = std::min(e.size() - done, have - off);
          if (::memcmp(buf + off, e.data() + done, chunk) != 0) {
            same = false;
            break;
          }
          off  += chunk;
          done += chunk;
        }
        if (!same) {
          break;
        }
      }
    } else {
      same = false;
    }
  }
  ::close(fd);
  return same;
}

File_output::~File_output() {
  if (aborted) {
    return;
  }

  if (same_on_disk()) {
    return;  // byte-identical: leave the file, and its mtime, alone
  }

  //---------------------------------- OPEN

  struct stat before_stat{};
  const bool  existed = ::stat(filename.c_str(), &before_stat) == 0;

  int fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
  I(fd >= 0);  // throw std::runtime_error(std::format("could not create destination {} file (permissions?)", filename));

  size_t map_size = sz;
  map_size >>= 12;
  ++map_size;
  map_size <<= 12;
  auto ok = ftruncate(fd, map_size);
  I(ok == 0);

  void* base = ::mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  // no superpages
  if (base == MAP_FAILED) {
    std::println("mmap errno:{} for filename{}\n", strerror_threadsafe(errno), filename);
    I(false);
  }

  //---------------------------------- SAVE

  char* ptr = static_cast<char*>(base);
  for (const auto& e : sequence) {
    memcpy(ptr, e.data(), e.size());
    ptr += e.size();
    I(static_cast<size_t>(ptr - static_cast<char*>(base)) <= sz);
  }

  //---------------------------------- CLOSE
  ok = ::msync(base, map_size, MS_SYNC);
  I(ok == 0);
  ::munmap(base, map_size);
  if (map_size != sz) {
    auto ok2 = ftruncate(fd, sz);
    I(ok2 == 0);
  }
  if (existed) {
    struct stat after_stat{};
    ok = ::fstat(fd, &after_stat);
    I(ok == 0);
    if (same_time(stat_mtime(before_stat), stat_mtime(after_stat))) {
      const timespec times[2] = {stat_atime(after_stat), next_time(stat_mtime(before_stat))};
      ok                      = ::futimens(fd, times);
      I(ok == 0);
    }
  }
  ::close(fd);
}
