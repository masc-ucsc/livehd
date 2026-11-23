//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace livehd::file_utils {

// Directory holding the running binary (used to locate bundled tools/scripts).
[[nodiscard]] std::string get_exe_path();

// Whole-file read (binary, no newline translation). nullopt when the file
// cannot be opened; the error policy stays at the caller, whose contracts
// differ (skip / throw / start-empty).
[[nodiscard]] inline std::optional<std::string> read_file(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return std::move(ss).str();
}

// Return the write error, or an empty error code on success.
[[nodiscard]] std::error_code write_all(int fd, std::string_view bytes);

// Append complete records under a caller-held lock, optionally discarding
// the unterminated tail left by an interrupted writer.
[[nodiscard]] std::error_code append_file(const std::string& path, std::string_view bytes, bool repair_partial_line = false);

// Publish a complete file with a same-directory rename. The caller owns
// parent directory creation and any lock needed for read-modify-write.
[[nodiscard]] std::error_code write_file_atomic(const std::string& path, std::string_view bytes);

// The producer streams into an open temporary descriptor and may set its mode.
// A failed producer or close leaves the previous destination intact.
[[nodiscard]] std::error_code write_file_atomic(const std::filesystem::path&               path,
                                                const std::function<std::error_code(int)>& produce);

// Write a caller-owned staging path. Close errors propagate before its caller
// publishes a generation pointer or a group of companion files.
void write_staged_file(const std::filesystem::path& path, const std::function<void(std::ostream&)>& emit);

// ---- the workdir lock -------------------------------------------------------
//
// flock(2) on a lock file OUTSIDE the tree it protects (the tree gets swapped).
// The descriptor is O_CLOEXEC: the host compiler and drv.bin (and its
// forked checkpoint children) must never inherit it, or the lock would outlive
// lhd. Shared <-> exclusive conversion is not atomic (flock semantics), so a
// caller that re-acquires EXCLUSIVE must re-validate what it saw before.
class File_lock {
public:
  File_lock()                            = default;
  File_lock(const File_lock&)            = delete;
  File_lock& operator=(const File_lock&) = delete;
  ~File_lock();

  bool               open(const std::string& path, std::string& why);
  // Blocks. When another holder has it, prints `waiting` to stderr once first.
  bool               exclusive(std::string_view waiting = {});
  bool               shared();
  void               release() noexcept;
  [[nodiscard]] bool is_open() const { return fd_ >= 0; }

private:
  bool lock(int op, std::string_view waiting);
  int  fd_ = -1;
};

// Install a staged directory. Native exchanges keep readers on a complete
// generation. The portable fallback rolls back a failed installation.
void replace_dir_atomic(const std::filesystem::path& staged, const std::filesystem::path& destination);

}  // namespace livehd::file_utils
