// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <filesystem>
#include <map>
#include <set>

#include "compile_salt.hpp"
#include "formal_salt.hpp"
#include "lhd_kernel_internal.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd {
std::string synth_invocation_context(const Options& opts, const Result& res, const Eprp_var::Eprp_dict& labels) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  const auto                                 string  = [&](std::string_view value) { json.String(value.data(), value.size()); };
  const auto                                 strings = [&](const auto& values) {
    json.StartArray();
    for (const auto& value : values) {
      string(value);
    }
    json.EndArray();
  };
  json.StartObject();
  json.Key("argv");
  strings(opts.invocation_argv);
  json.Key("cwd");
  string(std::filesystem::current_path().string());
  json.Key("command");
  string(opts.command);
  json.Key("reader");
  string(opts.reader);
  json.Key("top");
  string(opts.top);
  json.Key("seed");
  string(opts.seed);
  json.Key("incremental");
  json.Bool(opts.incremental);
  json.Key("compile_salt");
  string(std::to_string(livehd::kCompileSrcSalt));
  json.Key("formal_salt");
  string(std::to_string(livehd::kFormalSrcSalt));
  json.Key("effective_sets");
  json.StartArray();
  for (const auto& [key, value] : opts.sets) {
    json.StartArray();
    string(key);
    string(value);
    json.EndArray();
  }
  json.EndArray();
  json.Key("step_labels");
  json.StartObject();
  const std::map<std::string, std::string> ordered(labels.begin(), labels.end());
  for (const auto& [key, value] : ordered) {
    string(key);
    string(value);
  }
  json.EndObject();
  json.Key("prior_steps");
  strings(res.recipe_steps);
  std::set<std::string> inputs;
  const auto            input = [&](std::string path) {
    for (const auto prefix : {"lg:", "ln:", "verilog:", "pyrope:"}) {
      if (path.starts_with(prefix)) {
        path.erase(0, std::char_traits<char>::length(prefix));
        break;
      }
    }
    if (!path.empty()) {
      inputs.insert(std::move(path));
    }
  };
  for (const auto& path : res.inputs) {
    input(path);
  }
  for (size_t i = opts.command == "pass" ? 1 : 0; i < opts.files.size(); ++i) {
    input(opts.files[i]);
  }
  for (const auto* paths : {&opts.ins, &opts.in_dirs, &opts.libs}) {
    for (const auto& path : *paths) {
      input(path.path);
    }
  }
  input(opts.config);
  for (const auto key : {"library", "timing_files"}) {
    if (auto it = labels.find(key); it != labels.end()) {
      std::string_view files = it->second;
      while (!files.empty()) {
        const auto end = files.find(',');
        input(std::string(files.substr(0, end)));
        if (end == std::string_view::npos) {
          break;
        }
        files.remove_prefix(end + 1);
      }
    }
  }
  json.Key("inputs");
  strings(inputs);
  json.EndObject();
  return {buffer.GetString(), buffer.GetSize()};
}
}  // namespace lhd
