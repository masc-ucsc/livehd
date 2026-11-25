// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "export_file.hpp"

#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <fstream>
#include <system_error>

#include "file_utils.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd {
namespace {
namespace fs = std::filesystem;

[[noreturn]] void copy_error(const fs::path& source, const fs::path& destination, int error) {
  throw fs::filesystem_error("cannot export file",
                             source,
                             destination,
                             std::error_code(error ? error : EIO, std::generic_category()));
}
}  // namespace

void copy_export_file(const fs::path& source, const fs::path& destination) {
  const auto source_status = fs::status(source);
  if (!fs::is_regular_file(source_status)) {
    copy_error(source, destination, EINVAL);
  }
  std::error_code ec;
  const auto      target_status = fs::status(destination, ec);
  if (ec && ec != std::errc::no_such_file_or_directory) {
    throw fs::filesystem_error("cannot inspect export destination", source, destination, ec);
  }
  if (fs::exists(target_status) && fs::equivalent(source, destination)) {
    return;
  }
  const auto    permissions = fs::exists(target_status) ? target_status.permissions() : source_status.permissions();
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    copy_error(source, destination, errno);
  }

  ec = livehd::file_utils::write_file_atomic(destination, [&](int fd) -> std::error_code {
    std::array<char, 65536> bytes;
    while (input.read(bytes.data(), bytes.size()) || input.gcount() > 0) {
      if (auto error = livehd::file_utils::write_all(fd, {bytes.data(), static_cast<size_t>(input.gcount())})) {
        return error;
      }
    }
    if (input.bad() || !input.eof()) {
      return std::make_error_code(std::errc::io_error);
    }
    if (::fchmod(fd, static_cast<mode_t>(permissions & fs::perms::mask)) != 0) {
      return {errno, std::generic_category()};
    }
    return {};
  });
  if (ec) {
    throw fs::filesystem_error("cannot export file", source, destination, ec);
  }
}
void export_lef_views(const fs::path& directory, const std::vector<Lef_view>& views, std::vector<std::string>& outputs) {
  rapidjson::StringBuffer                    json;
  rapidjson::Writer<rapidjson::StringBuffer> writer(json);
  writer.StartArray();
  for (const auto& view : views) {
    const auto target = directory / view.relative_path;
    fs::create_directories(target.parent_path());
    if (fs::weakly_canonical(view.source) != fs::weakly_canonical(target)) {
      copy_export_file(view.source, target);
    }
    outputs.push_back(target.string());
    writer.String(view.relative_path.generic_string().c_str());
  }
  writer.EndArray();
  const auto manifest = (directory / "lefs.json").string();
  if (auto ec = livehd::file_utils::write_file_atomic(manifest, std::string(json.GetString()) + "\n")) {
    throw fs::filesystem_error("export LEF manifest", manifest, ec);
  }
  outputs.push_back(manifest);
}
}  // namespace lhd
