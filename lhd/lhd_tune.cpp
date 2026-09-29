//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_tune.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <stdio.h>  // renamex_np, RENAME_SWAP
#include <sys/clonefile.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif

#include "rapidjson/document.h"

namespace lhd::tune {

namespace fs = std::filesystem;

uint64_t fnv1a64(std::string_view bytes, uint64_t h) {
  for (const unsigned char c : bytes) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::string hex16(uint64_t v) { return std::format("{:016x}", v); }

int64_t unix_seconds_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string iso8601_utc(int64_t unix_seconds) {
  const std::time_t t = static_cast<std::time_t>(unix_seconds);
  std::tm           tm{};
  ::gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return std::nullopt;
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return std::move(oss).str();
}

bool write_file_atomic(const std::string& path, std::string_view bytes, std::string& why) {
  std::error_code ec;
  const fs::path  p(path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
      why = std::format("cannot create {}: {}", p.parent_path().string(), ec.message());
      return false;
    }
  }
  const std::string tmp = std::format("{}.tmp.{}", path, ::getpid());
  {
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      why = std::format("cannot write {}", tmp);
      return false;
    }
    ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ofs.flush();
    if (!ofs) {
      why = std::format("short write to {}", tmp);
      fs::remove(tmp, ec);
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    why = std::format("cannot rename {} -> {}: {}", tmp, path, std::strerror(errno));
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

// ---- the JSONL store --------------------------------------------------------

Jsonl_load load_jsonl(const std::string& path, std::string_view schema, bool read_only) {
  Jsonl_load out;
  auto       text = read_file(path);
  if (!text) {
    return out;  // missing: an empty history
  }
  out.status = Jsonl_load::Status::ok;

  // Everything after the last newline is a partial record (a crash mid-append).
  size_t complete = text->rfind('\n');
  complete        = complete == std::string::npos ? 0 : complete + 1;
  if (complete < text->size()) {
    out.dropped_partial = true;
  }

  auto untrusted = [&](std::string reason) {
    out.lines.clear();
    out.status = Jsonl_load::Status::bad;
    out.why    = std::move(reason);
    if (read_only) {
      return out;
    }
    out.bad_path = path + ".bad";
    std::error_code ec;
    fs::rename(path, out.bad_path, ec);
    if (ec) {
      out.bad_path.clear();
    }
    return out;
  };

  size_t pos = 0;
  size_t n   = 0;
  while (pos < complete) {
    const size_t eol  = text->find('\n', pos);
    const auto   line = std::string_view{*text}.substr(pos, eol - pos);
    pos               = eol + 1;
    ++n;
    if (line.empty()) {
      continue;  // a blank line carries nothing; tolerated
    }
    rapidjson::Document doc;
    doc.Parse(line.data(), line.size());
    if (doc.HasParseError() || !doc.IsObject()) {
      return untrusted(std::format("line {} is not a JSON object", n));
    }
    const auto it = doc.FindMember("schema");
    if (it == doc.MemberEnd() || !it->value.IsString()
        || std::string_view{it->value.GetString(), it->value.GetStringLength()} != schema) {
      return untrusted(std::format("line {} is not schema {}", n, schema));
    }
    out.lines.emplace_back(line);
  }

  if (out.dropped_partial && !read_only) {
    // Truncate the partial tail so the next append starts on a clean line. The
    // caller holds the workdir lock, so no concurrent appender can be mid-write.
    std::error_code ec;
    fs::resize_file(path, complete, ec);
  }
  return out;
}

bool append_jsonl(const std::string& path, const std::vector<std::string>& lines, std::string& why) {
  if (lines.empty()) {
    return true;
  }
  std::error_code ec;
  const fs::path  p(path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path(), ec);
  }
  std::string blob;
  for (const auto& l : lines) {
    blob += l;
    blob += '\n';
  }
  // ONE write(2) with O_APPEND: a crash leaves at most a partial final line,
  // which the loader drops.
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    why = std::format("cannot open {}: {}", path, std::strerror(errno));
    return false;
  }
  size_t off = 0;
  while (off < blob.size()) {
    const auto w = ::write(fd, blob.data() + off, blob.size() - off);
    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      why = std::format("cannot append to {}: {}", path, std::strerror(errno));
      ::close(fd);
      return false;
    }
    off += static_cast<size_t>(w);
  }
  ::close(fd);
  return true;
}

bool rewrite_jsonl(const std::string& path, const std::vector<std::string>& lines, std::string& why) {
  std::string blob;
  for (const auto& l : lines) {
    blob += l;
    blob += '\n';
  }
  return write_file_atomic(path, blob, why);
}

// ---- the workdir lock -------------------------------------------------------

File_lock::~File_lock() { release(); }

bool File_lock::open(const std::string& path, std::string& why) {
  release();
  std::error_code ec;
  const fs::path  p(path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path(), ec);
  }
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd_ < 0) {
    why = std::format("cannot open lock {}: {}", path, std::strerror(errno));
    return false;
  }
  return true;
}

bool File_lock::lock(int op, std::string_view waiting) {
  if (fd_ < 0) {
    return false;
  }
  while (::flock(fd_, op | LOCK_NB) != 0) {
    if (errno == EINTR) {
      continue;
    }
    if (errno != EWOULDBLOCK) {
      return false;
    }
    if (!waiting.empty()) {
      std::fprintf(stderr, "%.*s\n", static_cast<int>(waiting.size()), waiting.data());
      std::fflush(stderr);
    }
    while (::flock(fd_, op) != 0) {
      if (errno != EINTR) {
        return false;
      }
    }
    return true;
  }
  return true;
}

bool File_lock::exclusive(std::string_view waiting) { return lock(LOCK_EX, waiting); }

bool File_lock::shared() { return lock(LOCK_SH, {}); }

void File_lock::release() noexcept {
  if (fd_ >= 0) {
    (void)::flock(fd_, LOCK_UN);
    ::close(fd_);
    fd_ = -1;
  }
}

// ---- copy-on-write retention --------------------------------------------------

bool clone_tree(const std::string& src, const std::string& dst, std::string& why) {
  std::error_code ec;
  if (!fs::is_directory(src, ec)) {
    why = std::format("{} is not a directory", src);
    return false;
  }
  if (fs::exists(dst, ec)) {
    fs::remove_all(dst, ec);
  }
  fs::create_directories(fs::path(dst).parent_path(), ec);
#if defined(__APPLE__)
  // clonefile(2) clones a directory hierarchy in one call on APFS, keeping the
  // nanosecond mtimes ninja and the built-in stamps key on. Never `cp -c`: it
  // silently falls back to a FULL copy on a non-cloning volume, which would hide
  // the cost from the payoff gate.
  if (::clonefile(src.c_str(), dst.c_str(), CLONE_NOFOLLOW | CLONE_NOOWNERCOPY) == 0) {
    return true;
  }
  why = std::format("clonefile {} -> {}: {}", src, dst, std::strerror(errno));
  fs::remove_all(dst, ec);
  return false;
#elif defined(__linux__)
  const auto quote = [](const std::string& s) {
    std::string q{"'"};
    for (const char c : s) {
      q += c == '\'' ? std::string{"'\\''"} : std::string(1, c);
    }
    return q + "'";
  };
  const auto cmd = std::format("/bin/cp --reflink=always -a {} {} 2>/dev/null", quote(src), quote(dst));
  const int  st  = std::system(cmd.c_str());
  if (st == 0) {
    return true;
  }
  why = std::format("cp --reflink=always failed ({}): no copy-on-write on this filesystem", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
  fs::remove_all(dst, ec);
  return false;
#else
  why = "no copy-on-write clone on this platform";
  return false;
#endif
}

bool swap_dirs(const std::string& a, const std::string& b, std::string& why) {
#if defined(__APPLE__)
  if (::renamex_np(a.c_str(), b.c_str(), RENAME_SWAP) == 0) {
    return true;
  }
#elif defined(__linux__) && defined(SYS_renameat2)
  constexpr unsigned kRenameExchange = 1u << 1;  // RENAME_EXCHANGE; glibc < 2.28 lacks the wrapper
  if (::syscall(SYS_renameat2, AT_FDCWD, a.c_str(), AT_FDCWD, b.c_str(), kRenameExchange) == 0) {
    return true;
  }
#endif
  // Three-rename fallback. A crash between the renames leaves `a` missing,
  // which is a cold regeneration, never a wrong tree.
  const std::string mid = std::format("{}.swap.{}", a, ::getpid());
  if (std::rename(a.c_str(), mid.c_str()) != 0) {
    why = std::format("cannot move {} aside: {}", a, std::strerror(errno));
    return false;
  }
  if (std::rename(b.c_str(), a.c_str()) != 0) {
    why = std::format("cannot move {} -> {}: {}", b, a, std::strerror(errno));
    (void)std::rename(mid.c_str(), a.c_str());
    return false;
  }
  if (std::rename(mid.c_str(), b.c_str()) != 0) {
    why = std::format("cannot move {} -> {}: {}", mid, b, std::strerror(errno));
    return false;
  }
  return true;
}

void discard_dir(const std::string& dir) noexcept {
  try {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
      return;
    }
    const std::string trash = std::format("{}.trash.{}", dir, ::getpid());
    fs::rename(dir, trash, ec);
    fs::remove_all(ec ? fs::path(dir) : fs::path(trash), ec);
  } catch (...) {  // NOLINT(bugprone-empty-catch) -- best effort: a leftover is collected by the next begin()
  }
}

}  // namespace lhd::tune
