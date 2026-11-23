
#include "file_utils.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>

#include "iassert.hpp"

#ifdef __APPLE__
#include <libproc.h>
#include <stdio.h>  // renamex_np, RENAME_SWAP
#elif defined(__linux__)
#include <sys/syscall.h>
#endif

std::string livehd::file_utils::get_exe_path() {
  char exePath[PATH_MAX] = {
      0,
  };
#ifdef __APPLE__
  pid_t pid = getpid();
  int   ret = proc_pidpath(pid, exePath, PATH_MAX);
  I(ret > 0);
  int len = strlen(exePath);
#else
  int len = readlink("/proc/self/exe", exePath, PATH_MAX);
#endif
  I(len > 0 && len < PATH_MAX);
  for (int p = len - 1; p >= 0; p--) {
    if (exePath[p] == '/') {
      len = p;
      break;
    }
  }

  std::string str(exePath, len);

  return str;
}

std::error_code livehd::file_utils::write_all(int fd, std::string_view bytes) {
  constexpr auto max_write = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
  while (!bytes.empty()) {
    const auto n = ::write(fd, bytes.data(), std::min(bytes.size(), max_write));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {errno, std::generic_category()};
    }
    if (n == 0) {
      return std::make_error_code(std::errc::io_error);
    }
    bytes.remove_prefix(static_cast<size_t>(n));
  }
  return {};
}

namespace {
std::error_code discard_partial_line(int fd) {
  struct stat info{};
  if (::fstat(fd, &info) != 0) {
    return {errno, std::generic_category()};
  }
  std::array<char, 4096> chunk;
  off_t                  end = info.st_size;
  while (end > 0) {
    const auto begin = std::max<off_t>(0, end - static_cast<off_t>(chunk.size()));
    const auto size  = static_cast<size_t>(end - begin);
    const auto n     = ::pread(fd, chunk.data(), size, begin);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return {errno, std::generic_category()};
    }
    if (static_cast<size_t>(n) != size) {
      return std::make_error_code(std::errc::io_error);
    }
    const auto last = std::string_view{chunk.data(), size}.rfind('\n');
    if (last != std::string_view::npos) {
      const auto complete = begin + static_cast<off_t>(last + 1);
      if (complete == info.st_size || ::ftruncate(fd, complete) == 0) {
        return {};
      }
      return {errno, std::generic_category()};
    }
    end = begin;
  }
  if (info.st_size != 0 && ::ftruncate(fd, 0) != 0) {
    return {errno, std::generic_category()};
  }
  return {};
}
}  // namespace

std::error_code livehd::file_utils::append_file(const std::string& path, std::string_view bytes, bool repair_partial_line) {
  const int mode = repair_partial_line ? O_RDWR : O_WRONLY;
  const int fd   = ::open(path.c_str(), mode | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) {
    return {errno, std::generic_category()};
  }
  auto error = repair_partial_line ? discard_partial_line(fd) : std::error_code{};
  if (!error) {
    error = write_all(fd, bytes);
  }
  if (::close(fd) != 0 && !error) {
    error = {errno, std::generic_category()};
  }
  return error;
}

std::error_code livehd::file_utils::write_file_atomic(const std::string& path, std::string_view bytes) {
  return write_file_atomic(path, [&](int fd) { return write_all(fd, bytes); });
}

void livehd::file_utils::write_staged_file(const std::filesystem::path& path, const std::function<void(std::ostream&)>& emit) {
  std::ofstream out;
  out.exceptions(std::ios::failbit | std::ios::badbit);
  out.open(path, std::ios::binary);
  emit(out);
  out.close();
}

std::error_code livehd::file_utils::write_file_atomic(const std::filesystem::path&               path,
                                                      const std::function<std::error_code(int)>& produce) {
  auto      temp = (path.parent_path() / ".lhd-tmp-XXXXXX").string();
  const int fd   = ::mkstemp(temp.data());
  if (fd < 0) {
    return {errno, std::generic_category()};
  }
  std::error_code error;
  try {
    error = produce(fd);
  } catch (...) {
    ::close(fd);
    ::unlink(temp.c_str());
    throw;
  }
  if (::close(fd) != 0 && !error) {
    error = {errno, std::generic_category()};
  }
  if (!error && ::rename(temp.c_str(), path.c_str()) != 0) {
    error = {errno, std::generic_category()};
  }
  if (error) {
    ::unlink(temp.c_str());
  }
  return error;
}

namespace livehd::file_utils {

// ---- the workdir lock -------------------------------------------------------

File_lock::~File_lock() { release(); }

bool File_lock::open(const std::string& path, std::string& why) {
  release();
  std::error_code             ec;
  const std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
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

void replace_dir_atomic(const std::filesystem::path& staged, const std::filesystem::path& destination) {
  namespace fs = std::filesystem;
  File_lock   lock;
  std::string why;
  if (!lock.open(destination.string() + ".publish.lock", why) || !lock.exclusive()) {
    throw std::runtime_error(why.empty() ? "cannot lock directory publication: " + destination.string() : why);
  }
  if (!fs::exists(destination)) {
    fs::rename(staged, destination);
    return;
  }

  const bool swapped =
#if defined(__APPLE__)
      ::renamex_np(staged.c_str(), destination.c_str(), RENAME_SWAP) == 0;
#elif defined(__linux__) && defined(SYS_renameat2)
      ::syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD, destination.c_str(), 1u << 1) == 0;
#else
      false;
#endif
  std::error_code cleanup;
  if (swapped) {
    fs::remove_all(staged, cleanup);  // the old generation
    return;
  }

  // Place the backup in a unique sibling. If restoration itself fails, retain
  // the previous generation there and report its path to the caller.
  auto backup_dir = destination.string() + ".old.XXXXXX";
  if (::mkdtemp(backup_dir.data()) == nullptr) {
    throw std::system_error(errno, std::generic_category(), "creating backup for " + destination.string());
  }
  const auto      backup = fs::path(backup_dir) / "previous";
  std::error_code error;
  fs::rename(destination, backup, error);
  if (error) {
    fs::remove_all(backup_dir, cleanup);
    throw fs::filesystem_error("retiring previous generation", destination, backup, error);
  }
  fs::rename(staged, destination, error);
  if (error) {
    std::error_code restore;
    fs::rename(backup, destination, restore);
    if (restore) {
      throw fs::filesystem_error("restoring previous generation", backup, destination, restore);
    }
    fs::remove_all(backup_dir, cleanup);
    throw fs::filesystem_error("publishing directory", staged, destination, error);
  }
  fs::remove_all(backup_dir, cleanup);
}

}  // namespace livehd::file_utils
