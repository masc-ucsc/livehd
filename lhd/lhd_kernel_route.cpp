// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>

#include "export_file.hpp"
#include "file_utils.hpp"
#include "job_manager.hpp"
#include "lhd_kernel_internal.hpp"
#include "lhd_route_options.hpp"
#include "option_text.hpp"
#include "pass/route/route.hpp"
#include "physical_cache.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rules_cc/cc/runfiles/runfiles.h"

extern char** environ;

namespace lhd {
namespace {
namespace fs = std::filesystem;

[[noreturn]] void usage(const std::string& message) { throw Lhd_error{"usage", message, "see lhd route --help"}; }

std::string read(const fs::path& path) {
  auto data = livehd::file_utils::read_file(path.string());
  if (!data) {
    throw Lhd_error{"missing_file", "could not read " + path.string(), ""};
  }
  return std::move(*data);
}

void store(const fs::path& path, const std::string& data) {
  if (auto ec = livehd::file_utils::write_file_atomic(path.string(), data)) {
    throw fs::filesystem_error("write route output", path, ec);
  }
}

std::vector<Lef_view> bundle_views(const fs::path& dir) {
  rapidjson::Document manifest;
  manifest.Parse(read(dir / "lefs.json").c_str());
  if (!manifest.IsArray()) {
    usage("expected an ordered LEF array in " + (dir / "lefs.json").string());
  }
  std::vector<Lef_view> views;
  for (const auto& entry : manifest.GetArray()) {
    if (!entry.IsString()) {
      usage("expected a LEF filename in lefs.json");
    }
    const fs::path rel(entry.GetString());
    if (rel.empty() || rel.is_absolute() || std::find(rel.begin(), rel.end(), "..") != rel.end() || rel.extension() != ".lef") {
      usage("invalid LEF name in lefs.json: " + rel.string());
    }
    views.push_back({dir / rel, rel});
  }
  return views;
}

template <typename T>
bool number(std::string_view s, T& n) {
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
  return ec == std::errc{} && end == s.data() + s.size();
}

void add_setting(std::vector<std::string>& args, std::string_view name, const std::string& value, Route_mode mode) {
  auto it = std::find_if(std::begin(route_settings), std::end(route_settings), [&](auto& s) { return s.name == name; });
  if (it == std::end(route_settings) || !(it->modes & static_cast<int>(mode))) {
    usage("unknown setting for this routing mode: route." + std::string(name));
  }
  auto flag = std::string(name);
  std::replace(flag.begin(), flag.end(), '_', '-');
  args.push_back("--" + flag);
  auto invalid = [&] { usage("invalid value for route." + std::string(name) + ": " + value); };
  if (it->type == Route_value::flag) {
    auto enabled = str_tools::parse_bool(value);
    if (!enabled) {
      invalid();
    }
    if (!*enabled) {
      args.pop_back();
    }
    return;
  }
  if (it->type == Route_value::grid || it->type == Route_value::indices) {
    const bool         grid = it->type == Route_value::grid;
    std::istringstream parts(value);
    size_t             count = 0;
    for (std::string piece; std::getline(parts, piece, grid ? 'x' : ',');) {
      int n = 0;
      if (!number(piece, n) || n < (grid ? 1 : 0)) {
        invalid();
      }
      args.push_back(piece);
      ++count;
    }
    if (!count || (grid && count != 2) || value.back() == (grid ? 'x' : ',')) {
      invalid();
    }
    return;
  }
  int    integer = 0;
  double real    = 0;
  if (it->type == Route_value::integer && !number(value, integer)) {
    invalid();
  }
  if (it->type == Route_value::number && (!number(value, real) || !std::isfinite(real))) {
    invalid();
  }
  if (it->type == Route_value::order && value != "short" && value != "long" && value != "pins" && value != "fewpins") {
    invalid();
  }
  args.push_back(value);
}

void coordinate(Route_mode mode, std::vector<std::string> args, const fs::path& self, const fs::path& log) {
  using rules_cc::cc::runfiles::Runfiles;
  std::string               error;
  std::unique_ptr<Runfiles> files(Runfiles::Create(self.string(), BAZEL_CURRENT_REPOSITORY, &error));
  if (!files) {
    throw Lhd_error{"dependency", error, ""};
  }
  auto script = files->Rlocation(mode == Route_mode::chunk ? "_main/tools/routefirst/rf_chunks.py"
                                                           : "_main/tools/routefirst/rf_feedback.py");
  args.insert(args.begin(), {"python3", script, "--lhd", self.string()});
  std::vector<char*> argv;
  for (auto& a : args) {
    argv.push_back(a.data());
  }
  argv.push_back(nullptr);
  pid_t                      pid;
  // The parent owns stdout and the lhd result envelope. The coordinator keeps its own JSON in feedback.json.
  posix_spawn_file_actions_t actions;
  int                        err = posix_spawn_file_actions_init(&actions);
  if (err) {
    throw std::system_error(err, std::generic_category(), "route spawn setup");
  }
  err = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (!err) {
    err = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  }
  posix_spawn_file_actions_destroy(&actions);
  if (err) {
    throw std::system_error(err, std::generic_category(), "spawn route coordinator");
  }
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      throw std::system_error(errno, std::generic_category(), "wait for routing workers");
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    throw Lhd_error{"config", "routing coordinator failed", "see worker stderr.log and command.json in the route directory"};
  }
}
}  // namespace

// Hash only the inputs read for this operation. A chunk checkpoint may also
// contain gigabytes of worker DEFs and unrelated placements. LEF order matters.
std::string route_input_identity(const Options& opts) {
  std::string id;
  for (const auto& verb : opts.files) {
    id += "|verb=" + verb;
  }
  auto add = [&](std::string_view role, const fs::path& path) {
    if (!fs::is_regular_file(path)) {
      throw Lhd_error{"missing_file", "missing routing input: " + path.string(), ""};
    }
    id += "|" + std::string(role) + "=" + physical_file_digest(path);
  };
  for (const auto& in : opts.ins) {
    fs::path p(in.path);
    if (in.kind == "def" && fs::is_directory(p)) {
      add("def", p / "out.def");
      add("lef_manifest", p / "lefs.json");
      for (const auto& v : bundle_views(p)) {
        add("lef", v.source);
      }
      for (auto n : {"pcrs_clock.sdc", "pcrs_clock.json", "power_grid.json"}) {
        if (fs::is_regular_file(p / n)) {
          add(n, p / n);
        }
      }
    } else if (in.kind == "route") {
      if (fs::is_directory(p / "finish")) {
        p /= "finish";
      }
      add("checkpoint", p / "routed.def");
      add("feedback", p / "feedback.tsv");
      for (auto n : {"findings.tsv", "stats.json"}) {
        if (fs::is_regular_file(p / n)) {
          add(n, p / n);
        }
      }
    } else if (in.kind == "reuse") {
      add("reuse", p / "inputs.json");
      std::vector<fs::path> packets;
      for (const auto& d : fs::directory_iterator(p)) {
        if (d.path().filename().string().starts_with("worker-") && d.is_directory()) {
          packets.push_back(d.path());
        }
      }
      std::sort(packets.begin(), packets.end());
      for (const auto& d : packets) {
        add(d.filename().string(), d / "wires.json");
      }
      if (fs::is_regular_file(p / "trunks/routed.def")) {
        add("trunks", p / "trunks/routed.def");
      }
    } else {
      add(in.kind, p);
    }
  }
  return id;
}

void route_command(Options& opts, Result& res) {
  Route_mode mode = Route_mode::flat;
  if (opts.files == std::vector<std::string>{"chunk"}) {
    mode = Route_mode::chunk;
  } else if (opts.files == std::vector<std::string>{"repair"}) {
    mode = Route_mode::repair;
  } else if (opts.files == std::vector<std::string>{"verify"}) {
    mode = Route_mode::verify;
  } else if (!opts.files.empty()) {
    usage("expected route, route chunk, route repair or route verify");
  }
  if (!opts.raw_args.empty() || !opts.libs.empty() || !opts.in_dirs.empty() || !opts.dumps.empty()) {
    usage("use typed route inputs and --set route.NAME=VALUE");
  }
  for (const auto& e : opts.emits) {
    if (e.kind != "diagnostics") {
      usage("routing exports DEF with --emit-dir def:DIR");
    }
  }
  if (opts.emit_dirs.size() > 1 || (!opts.emit_dirs.empty() && opts.emit_dirs.front().kind != "def")) {
    usage("expected one --emit-dir def:DIR");
  }
  if (mode == Route_mode::verify && !opts.emit_dirs.empty()) {
    usage("route verify reads the existing DEF; use --workdir to keep findings");
  }
  if (mode != Route_mode::verify && opts.emit_dirs.empty() && opts.workdir.empty()) {
    usage("routing needs --workdir or --emit-dir def:DIR");
  }

  std::vector<std::string> args;
  for (const auto& [key, value] : opts.sets) {
    if (key.starts_with("route.")) {
      add_setting(args, std::string_view(key).substr(6), value, mode);
    }
  }
  fs::path              frame, previous, bundle;
  std::vector<Lef_view> views;
  std::set<std::string> seen;
  for (const auto& in : opts.ins) {
    if (in.kind != "lef" && in.kind != "protect-failures" && !seen.insert(in.kind).second) {
      usage("duplicate routing input: " + in.kind);
    }
    if (!fs::exists(in.path)) {
      throw Lhd_error{"missing_file", "missing routing input: " + in.path, ""};
    }
    res.inputs.push_back(in.path);
    if (in.kind == "def") {
      frame = in.path;
    } else if (in.kind == "lef") {
      views.push_back({in.path, fs::path("lef") / (std::to_string(views.size()) + "_" + fs::path(in.path).filename().string())});
    } else if (in.kind == "route" && mode == Route_mode::repair) {
      previous = in.path;
    } else if ((mode == Route_mode::chunk && (in.kind == "plan" || in.kind == "reuse"))
               || (mode == Route_mode::repair && (in.kind == "targets" || in.kind == "protect-failures" || in.kind == "timing-eco"))
               || (mode == Route_mode::flat && (in.kind == "only" || in.kind == "clock-nets" || in.kind == "reserve-cells"))
               || ((mode == Route_mode::chunk || mode == Route_mode::flat) && in.kind == "timing")) {
      if (in.kind == "reuse" && !opts.incremental) {
        usage("reuse: is incompatible with lhd.incremental=false");
      }
      args.insert(args.end(), {"--" + in.kind, in.path});
    } else {
      usage("unsupported input for this routing mode: " + in.kind);
    }
  }
  if (fs::is_directory(frame)) {
    if (!views.empty()) {
      usage("a DEF bundle already specifies its ordered LEFs");
    }
    bundle = frame;
    views  = bundle_views(bundle);
    frame  = bundle / "out.def";
  }
  if (views.empty()) {
    usage("supply a synthesis DEF bundle or explicit ordered lef: inputs");
  }
  if (mode == Route_mode::repair && previous.empty()) {
    usage("route repair requires route:PREVIOUS");
  }
  if (mode != Route_mode::repair && frame.empty()) {
    usage("routing requires def:FRAME");
  }
  if (!frame.empty() && !fs::is_regular_file(frame)) {
    usage("missing DEF " + frame.string());
  }
  livehd::routing::Frame request;
  for (const auto& view : views) {
    if (!fs::is_regular_file(view.source)) {
      usage("missing LEF " + view.source.string());
    }
    request.lefs.push_back(view.source.string());
  }
  const auto workspace = fs::absolute(workdir(opts));
  const auto latest    = workspace / (mode == Route_mode::verify ? "route-verify" : "route");
  if (fs::exists(latest) && !fs::is_symlink(latest)) {
    usage("expected a routing checkpoint link at " + latest.string());
  }
  const auto runs = workspace / "route-runs";
  fs::create_directories(runs);
  auto pattern = (runs / "trial-XXXXXX").string();
  if (!::mkdtemp(pattern.data())) {
    throw std::system_error(errno, std::generic_category(), "create routing trial");
  }
  const fs::path trial(pattern);
  const auto     root = trial / "route";
  struct Scratch {
    fs::path dir;
    ~Scratch() {
      std::error_code ec;
      if (!dir.empty()) {
        fs::remove_all(dir, ec);
      }
    }
  } scratch{opts.workdir_scratch ? fs::path(opts.workdir) : fs::path{}};
  if (opts.gui) {
    if (mode != Route_mode::chunk || opts.workdir_scratch) {
      usage("--gui needs route chunk and a named workdir");
    }
    if (std::find(args.begin(), args.end(), "--agent-socket") == args.end()) {
      args.insert(args.end(), {"--agent-socket", (workspace / "route.sock").string()});
    }
  }
  fs::path routed = root / "routed.def", summary = root / "feedback.json";
  if (mode == Route_mode::flat || mode == Route_mode::verify) {
    if (!fs::create_directory(root)) {
      usage("routing checkpoint already exists: " + root.string());
    }
    args.insert(args.end(), {"--report", (root / "feedback.tsv").string(), "--verify-report", (root / "findings.tsv").string()});
    request.input_def   = frame.string();
    request.output_def  = routed.string();
    const auto settings = livehd::routing::parse_settings(args);
    auto       result   = mode == Route_mode::verify ? livehd::routing::verify_frame(request, settings)
                                                     : livehd::routing::route_frame(request, settings);
    if (mode == Route_mode::flat) {
      store(root / "stats.json", result.stats);
    }
    rapidjson::Document outcome(rapidjson::kObjectType);
    auto&               alloc = outcome.GetAllocator();
    rapidjson::Value    violations(rapidjson::kObjectType);
    for (const auto& [name, count] : result.findings) {
      violations.AddMember(rapidjson::Value(name.c_str(), alloc), rapidjson::Value(count), alloc);
    }
    auto opens = result.findings.find("verify_nets_without_wires");
    outcome.AddMember("unrouted", opens == result.findings.end() ? int64_t{0} : opens->second, alloc);
    outcome.AddMember("violations", violations, alloc);
    rapidjson::StringBuffer                    buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    outcome.Accept(writer);
    store(summary, buf.GetString());
  } else {
    if (mode == Route_mode::chunk) {
      const unsigned ceiling = livehd::Job_manager::global().ceiling();
      const auto     workers = opts.jobs ? ceiling : std::min(4u, ceiling);
      args.insert(args.end(), {"--workers", std::to_string(workers)});
      if (opts.incremental && !seen.contains("reuse") && fs::is_regular_file(latest / "inputs.json")) {
        args.insert(args.end(), {"--reuse-if-compatible", fs::canonical(latest).string()});
      }
      routed = root / "finish/routed.def";
    } else if (fs::is_directory(previous / "finish")) {
      previous /= "finish";
    }
    args.insert(args.end(), {"--out", root.string(), "--", (mode == Route_mode::repair ? previous : frame).string()});
    args.insert(args.end(), request.lefs.begin(), request.lefs.end());
    coordinate(mode, std::move(args), opts.driver_path, trial / "coordinator.log");
  }
  rapidjson::Document qor;
  qor.Parse(read(summary).c_str());
  if (!qor.IsObject() || !qor.HasMember("unrouted") || !qor["unrouted"].IsInt64() || !qor.HasMember("violations")
      || !qor["violations"].IsObject()) {
    throw Lhd_error{"internal", "missing native route verification summary", ""};
  }
  qor.AddMember("complete", qor["unrouted"].GetInt64() == 0 && qor["violations"].ObjectEmpty(), qor.GetAllocator());
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  qor.Accept(writer);
  res.qor_json = buffer.GetString();
  store(summary, res.qor_json + "\n");
  const std::string verb = opts.files.empty() ? "" : opts.files.front();
  res.command            = verb.empty() ? "route" : "route " + verb;
  res.recipe_steps.push_back(verb.empty() ? "route" : "route." + verb);
  if (!opts.workdir_scratch) {
    res.outputs.push_back(mode == Route_mode::verify ? (root / "findings.tsv").string() : routed.string());
    res.outputs.push_back(summary.string());
  }
  if (!opts.emit_dirs.empty()) {
    const fs::path dest = opts.emit_dirs.front().path;
    fs::create_directories(dest);
    copy_export_file(routed, dest / "out.def");
    res.outputs.push_back((dest / "out.def").string());
    export_lef_views(dest, views, res.outputs);
    for (auto n : {"pcrs_clock.sdc", "pcrs_clock.json", "power_grid.json"}) {
      if (!bundle.empty() && fs::is_regular_file(bundle / n)) {
        if (fs::weakly_canonical(bundle / n) != fs::weakly_canonical(dest / n)) {
          copy_export_file(bundle / n, dest / n);
        }
        res.outputs.push_back((dest / n).string());
      } else {
        fs::remove(dest / n);
      }
    }
  }
  if (!opts.workdir_scratch) {
    auto link = trial / "latest";
    fs::create_directory_symlink(root.lexically_relative(workspace), link);
    fs::rename(link, latest);
    res.outputs.push_back(latest.string());
  }
}
}  // namespace lhd
