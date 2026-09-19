//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Command-agnostic plumbing of the profile-guided tuner (sim_profile.md §5):
// the append-only JSONL tune store, the workdir lock, and the copy-on-write
// tree retention (clone + atomic swap) that makes a rejected trial's revert a
// pointer swap instead of a rebuild. Nothing here knows what a tune vector
// means; lhd_sim_tune.{hpp,cpp} is the sim model on top.
//
// Deliberately free of Options/Result/diag: it links into the small `lhd_tune`
// library that the unit test uses without dragging the whole engine in.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lhd::tune {

// FNV-1a 64: the tune store's content hash (retained-tree names, testbench
// digests, raw-file dedup). Not cryptographic; collisions only cost a trial.
[[nodiscard]] uint64_t    fnv1a64(std::string_view bytes, uint64_t h = 0xcbf29ce484222325ULL);
[[nodiscard]] std::string hex16(uint64_t v);

[[nodiscard]] int64_t     unix_seconds_now();
[[nodiscard]] std::string iso8601_utc(int64_t unix_seconds);

// Whole-file read; nullopt when the file cannot be opened.
[[nodiscard]] std::optional<std::string> read_file(const std::string& path);

// tmp + rename in the same directory: a reader sees the old bytes or the new
// ones, never a torn file. Creates the parent directory.
bool write_file_atomic(const std::string& path, std::string_view bytes, std::string& why);

// ---- the JSONL store --------------------------------------------------------
//
// One JSON object per line, each carrying `"schema":<schema>`. The file is the
// history; readers replay it. Rules (sim_profile.md §5):
//  - a trailing PARTIAL line (no newline: a crash mid-append) is ignored, and
//    truncated away so the next append cannot glue onto it;
//  - any complete line that is malformed or carries a foreign schema makes the
//    WHOLE file untrusted: it is renamed `<path>.bad` and the store starts empty.
struct Jsonl_load {
  enum class Status { missing, ok, bad };
  Status                   status = Status::missing;
  std::vector<std::string> lines;                    // complete, validated records, in file order
  bool                     dropped_partial = false;  // a trailing partial line was ignored
  std::string              bad_path;                 // where an untrusted file was moved
  std::string              why;                      // the first reason it was untrusted
};
// `read_only`: a reader that holds no lock (an observation run resolving the
// workdir's decision) changes nothing on disk -- an untrusted file is ignored,
// not renamed, and a partial tail is skipped, not truncated.
[[nodiscard]] Jsonl_load load_jsonl(const std::string& path, std::string_view schema, bool read_only = false);

// Append whole lines (each gets a '\n'). The caller holds the workdir lock.
bool append_jsonl(const std::string& path, const std::vector<std::string>& lines, std::string& why);
// Replace the file with exactly `lines` (compaction), tmp + rename.
bool rewrite_jsonl(const std::string& path, const std::vector<std::string>& lines, std::string& why);

// ---- the workdir lock -------------------------------------------------------
//
// flock(2) on a lock file OUTSIDE the tree it protects (the tree gets swapped).
// The descriptor is O_CLOEXEC: ninja, the host compiler and drv.bin (and its
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
  bool               exclusive(std::string_view waiting);
  bool               shared();
  void               release() noexcept;
  [[nodiscard]] bool is_open() const { return fd_ >= 0; }

private:
  bool lock(int op, std::string_view waiting);
  int  fd_ = -1;
};

// ---- copy-on-write retention --------------------------------------------------
//
// clone_tree: an APFS clonefile(2) (macOS) or `cp --reflink=always -a` (Linux)
// of a whole directory. NEVER a hardlink snapshot: generated files are
// rewritten IN PLACE (File_output: O_RDWR + mmap), which would corrupt a
// hardlinked copy. Fails (returns false, `why` set, `dst` absent) on a
// filesystem without CoW; the caller then simply retains nothing.
bool clone_tree(const std::string& src, const std::string& dst, std::string& why);

// Atomically exchange two directory entries (renamex_np RENAME_SWAP /
// renameat2 RENAME_EXCHANGE), falling back to three renames. A crash inside the
// fallback leaves `a` missing, which the callers treat as a cold regeneration.
bool swap_dirs(const std::string& a, const std::string& b, std::string& why);

// rename to a unique trash name next to `dir`, then remove it. Best effort.
void discard_dir(const std::string& dir) noexcept;

}  // namespace lhd::tune
