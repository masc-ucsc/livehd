//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_sim_options.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <format>
#include <string>

#include "core/metadata.hpp"
#include "lhd_kernel_internal.hpp"
#include "lhd_sim_tune.hpp"
#include "lhd_tune.hpp"

namespace lhd {

namespace {

using livehd::sim::Tune_vector;

bool is_bool_text(std::string_view v) { return v == "true" || v == "false" || v == "1" || v == "0" || v == "on" || v == "off"; }

// Record a file the generated code depends on (the --depfile / envelope inputs).
void add_input(Result& res, const std::string& path) {
  if (std::ranges::find(res.inputs, path) == res.inputs.end()) {
    res.inputs.push_back(path);
  }
}


// Replay only the simulation section; explicit --set knobs retain precedence.
std::optional<sim_tune::Tune_file> load_tune_file(const Options& opts, Result& res) {
  if (!opts.metadata) {
    return std::nullopt;
  }
  const auto records = opts.metadata->select("livehd.sim.tuning", opts.top);
  if (records.empty()) {
    return std::nullopt;
  }
  if (records.size() != 1) {
    throw Lhd_error{"config", "multiple simulation tuning records match this design", "keep one tune vector per design"};
  }
  for (const auto& path : opts.metadata_files) {
    add_input(res, path);
  }
  std::string err;
  auto        f = sim_tune::parse_tune_file(livehd::metadata::canonical(*records.front()), err);
  if (!f) {
    throw Lhd_error{"config", "simulation tuning metadata: " + err, ""};
  }
  return f;
}

sim_tune::Pins explicit_pins(const Options& opts) {
  std::string err;
  auto        p = sim_tune::pins_from_sets(opts.sets, err);
  if (!err.empty()) {
    throw Lhd_error{"usage", std::format("--set {}", err), ""};
  }
  return p;
}

}  // namespace

// ---- option grammar ---------------------------------------------------------------

std::pair<std::string, std::string> sim_set_value_error(const Sim_set_option& opt, std::string_view value) {
  using K        = Sim_set_option::Kind;
  const auto bad = [&](std::string_view expects, std::string hint = {}) {
    return std::pair{std::format("--set/--config sim.{} expects {}, got '{}'", opt.name, expects, value), std::move(hint)};
  };
  switch (opt.kind) {
    case K::boolean    : return is_bool_text(value) ? std::pair<std::string, std::string>{} : bad("true|false");
    case K::non_neg_num: {
      // The numeric checkpoint knobs must be non-negative numbers, else a typo
      // would silently reach the driver as 0 (checkpoint every cycle).
      const std::string v{value};
      errno               = 0;
      char*        endp   = nullptr;
      const double parsed = std::strtod(v.c_str(), &endp);
      if (v.empty() || endp == v.c_str() || *endp != '\0' || parsed < 0.0 || errno == ERANGE) {
        return bad("a non-negative number");
      }
      return {};
    }
    case K::bool_or_file:
    case K::path        : return {};
    case K::backend:
      return livehd::sim::parse_tune_backend(value).ok ? std::pair<std::string, std::string>{} : bad("auto|slop|llvm");
    case K::tri: return livehd::sim::parse_tune_dirty(value).ok ? std::pair<std::string, std::string>{} : bad("auto|on|off");
    case K::tune_mode:
      return value == "auto" || value == "on" || value == "off" ? std::pair<std::string, std::string>{}
                                                                : bad("auto|on|off (a mode, not a boolean)");
    case K::fence:
      return livehd::sim::parse_tune_fence(value).ok ? std::pair<std::string, std::string>{}
                                                     : bad("auto|none|N (N a whole number in [0, 1048576])",
                                                           "`none` fences nothing; `0` fences every single-use module");
    case K::num_or_auto:
      return livehd::sim::parse_tune_live_words(value).ok
                 ? std::pair<std::string, std::string>{}
                 : bad("auto|N (N a whole number in [1, 1048576])", value == "0" ? "the default budget is spelled `auto`" : "");
    case K::count: {
      // The same range drv.bin accepts, checked here so a bad value fails at
      // parse time rather than after a full build (or silently, when the run
      // does not profile).
      uint64_t n = 0;
      if (!livehd::sim::tune_detail::whole(value, 0, livehd::sim::kTuneMaxNumber, n)) {
        return bad("a whole number in [0, 1048576]");
      }
      return {};
    }
  }
  return {};
}

std::optional<std::pair<std::string, std::string>> renamed_sim_set(std::string_view flag, std::string_view value) {
  if (const auto r = sim_tune::renamed_sim_flag(flag, value)) {
    return std::pair{"sim." + r->new_flag, r->value};
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> sim_tune_codegen_labels(const Options& opts, Result& res) {
  Tune_vector v;
  bool        partition_auto = false;
  if (opts.sim_tune.resolved) {
    v              = Tune_vector{opts.sim_tune.dirty, opts.sim_tune.fence, opts.sim_tune.live_words, opts.sim_tune.llvm};
    partition_auto = opts.sim_tune.partition_auto;
    // The session already read the file; the compile phase reset the inputs.
    for (const auto& path : opts.metadata_files) {
      add_input(res, path);
    }
  } else {
    // `lhd compile --emit-dir sim:`: no workdir tune store; explicit > file > default.
    const auto f   = load_tune_file(opts, res);
    const auto r   = sim_tune::resolve(explicit_pins(opts), f ? f->pins : sim_tune::Pins{}, std::nullopt);
    v              = r.v;
    partition_auto = r.dirty == sim_tune::Source::dflt && r.fence == sim_tune::Source::dflt;
  }
  // Unpinned dirty and fence: the generator decides from the design's
  // dirty partition (inou.cgen.sim auto-dirty rule).
  return {
      {"color_dirty", partition_auto ? "auto" : (v.dirty ? "true" : "false")},
      {"fence_ratio",              partition_auto ? "auto" : v.fence_str()},
      { "live_words",         v.live_words_str()},
      {    "backend",            v.backend_str()},
  };
}

}  // namespace lhd
