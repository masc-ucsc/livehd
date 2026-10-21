// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "export_file.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <system_error>

#include <sys/stat.h>
#include <unistd.h>

namespace lhd {
namespace {
namespace fs = std::filesystem;

[[noreturn]] void copy_error(const fs::path& source, const fs::path& destination, int error) {
  throw fs::filesystem_error("cannot export file", source, destination, std::error_code(error ? error : EIO, std::generic_category()));
}
}  // namespace

void copy_export_file(const fs::path& source, const fs::path& destination) {
  const auto source_status = fs::status(source);
  if (!fs::is_regular_file(source_status)) {
    copy_error(source, destination, EINVAL);
  }
  std::error_code ec;
  const auto target_status = fs::status(destination, ec);
  if (ec && ec != std::errc::no_such_file_or_directory) {
    throw fs::filesystem_error("cannot inspect export destination", source, destination, ec);
  }
  if (fs::exists(target_status) && fs::equivalent(source, destination)) {
    return;
  }
  const auto permissions = fs::exists(target_status) ? target_status.permissions() : source_status.permissions();
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    copy_error(source, destination, errno);
  }

  // A unique sibling keeps rename on the destination filesystem. Keeping the
  // descriptor open also avoids reopening the temporary through its name.
  auto temporary = (destination.parent_path() / ".lhd-export-XXXXXX").string();
  int descriptor = ::mkstemp(temporary.data());
  if (descriptor < 0) {
    copy_error(source, destination, errno);
  }
  try {
    const auto close_file = [](FILE* file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> output(::fdopen(descriptor, "wb"), close_file);
    if (!output) {
      const int error = errno;
      ::close(descriptor);
      descriptor = -1;
      copy_error(source, destination, error);
    }
    descriptor = -1;  // output owns the descriptor from here.
    std::array<char, 65536> bytes;
    while (input.read(bytes.data(), bytes.size()) || input.gcount() > 0) {
      const auto count = static_cast<size_t>(input.gcount());
      if (std::fwrite(bytes.data(), 1, count, output.get()) != count) {
        copy_error(source, destination, errno);
      }
    }
    if (input.bad() || (!input.eof() && input.fail())) {
      copy_error(source, destination, EIO);
    }
    if (std::fflush(output.get()) != 0) {
      copy_error(source, destination, errno);
    }
    // Change only the new inode, never a source or existing export inode.
    if (::fchmod(::fileno(output.get()), static_cast<mode_t>(permissions & fs::perms::mask)) != 0) {
      copy_error(source, destination, errno);
    }
    if (std::fclose(output.release()) != 0) {
      copy_error(source, destination, errno);
    }
    fs::rename(temporary, destination);
  } catch (...) {
    if (descriptor >= 0) {
      ::close(descriptor);
    }
    fs::remove(temporary, ec);
    throw;
  }
}
}  // namespace lhd
