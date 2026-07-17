//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Public engine initialization, option discovery, and command dispatch.

#include <algorithm>
#include <format>

#include "diag.hpp"
#include "lhd_kernel_internal.hpp"
#include "pass.hpp"

namespace lhd {

// ---- public entry points ----------------------------------------------------

// Resolve an abbreviated --set/--config key to its canonical
// "<passtoken>.<flag>" form (2h-set_path). `ctx` is the command-path
// established by the command words to the LEFT of the flag, dotted
// (e.g. "pass.abc" after `lhd pass abc`, "" before any command word). Tries
// the key as-is, then prepends successively shorter prefixes of `ctx`
// (longest first); the first candidate whose leading segment(s) name a known
// pass (kSetPasses) wins. Returns the key unchanged when nothing resolves, so
// check_known_set_passes still emits the standard unknown-pass error. Uses
// only the constexpr kSetPasses table, so it is safe before init_engine().
std::string canonical_set_key(std::string_view key, std::string_view ctx) {
  // `<channel>.log=<level>` is the developer-logging namespace (livehd::log),
  // orthogonal to the pass-flag registry — a channel name (e.g. `upass`,
  // `cprop`) is NOT a pass name, so it must never collect a command-path
  // prefix. Leave any `.log` key verbatim for apply_log_settings.
  if (key.size() > 4 && key.substr(key.size() - 4) == ".log") {
    return std::string{key};
  }
  // The `sim.*` command namespace (sim_command + the cgen.sim codegen labels,
  // kSimSetOptions) is its own vocabulary. Like `.log`, it must never collect
  // a command-path prefix under a `compile`/describe context.
  if (key.size() > 4 && key.substr(0, 4) == "sim.") {
    auto flag = key.substr(4);
    for (const auto& s : kSimSetOptions) {
      if (s.name == flag) {
        return std::string{key};
      }
    }
  }
  auto names_a_pass = [](std::string_view candidate) {
    auto pos = candidate.rfind('.');
    return pos != std::string_view::npos && !set_pass_method(candidate.substr(0, pos)).empty();
  };
  if (names_a_pass(key)) {
    return std::string{key};
  }
  std::string prefix{ctx};
  while (!prefix.empty()) {
    std::string candidate = prefix + "." + std::string{key};
    if (names_a_pass(candidate)) {
      return candidate;
    }
    auto pos = prefix.rfind('.');
    if (pos == std::string::npos) {
      break;
    }
    prefix.resize(pos);
  }
  return std::string{key};
}

Lhd_error classify_engine_failure(std::string_view fallback_msg) {
  const auto& recs = livehd::diag::sink().records();
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    if (it->severity == livehd::diag::Severity::error) {
      return Lhd_error{map_diag_category(it->category), it->message, it->hint};
    }
  }
  return Lhd_error{"internal", std::string{fallback_msg}, ""};
}

void init_engine() {
  static bool done = false;  // engine commands and `lhd list options`/`describe pass.flag` may both reach here
  if (done) {
    return;
  }
  done = true;
  for (const auto& it : Pass_plugin::get_registry()) {
    it.second();
  }
  setup_inou_yosys();
}

std::vector<Set_option> list_set_options() {
  init_engine();
  std::vector<Set_option> out;
  const auto is_formal_common = [](std::string_view flag) {
    for (const auto& f : kFormalCommonFlags) {
      if (f == flag) {
        return true;
      }
    }
    return false;
  };
  for (const auto& sp : kSetPasses) {
    if (sp.list == Set_pass::List::none) {
      continue;  // legacy alias spelling: accepted by --set, never listed
    }
    const auto* m = Pass::eprp.get_method(sp.method);
    if (m == nullptr) {
      continue;  // defensive: every kSetPasses method registers in init_engine
    }
    for (const auto& [flag, attr] : m->labels) {
      if (is_kernel_label(flag)) {
        continue;
      }
      // `top` on any pass is the shared --top / lhd.top (set_top_label plumbs
      // it); a per-pass listing would advertise N spellings of one knob.
      if (flag == "top") {
        continue;
      }
      // Deprecated alias labels and kernel-plumbed internals stay functional
      // but leave the vocabulary listing.
      if (attr.help.starts_with("DEPRECATED") || attr.help.starts_with("INTERNAL")) {
        continue;
      }
      if (sp.list == Set_pass::List::common && !is_formal_common(flag)) {
        continue;
      }
      if (sp.list == Set_pass::List::specific && is_formal_common(flag)) {
        continue;
      }
      out.push_back(Set_option{std::format("{}.{}", sp.set_name, flag), std::string{sp.method}, attr.default_value, attr.help});
    }
  }
  // The `lhd.*` kernel namespace: shared, cross-pass settings the kernel folds
  // into Options (apply_lhd_settings), not labels of any single EPRP method.
  // Keep in sync with check_known_set_passes / apply_lhd_settings.
  out.push_back(Set_option{"lhd.seed",
                           "lhd",
                           "0",
                           "shared RNG seed for every pass that wants determinism (e.g. pass.color mincut); one seed per run"});
  out.push_back(
      Set_option{"lhd.top",
                 "lhd",
                 "",
                 "top module shared across passes; the canonical form of the --top flag (the flag wins if both are given). "
                 "Full internal `file.entity` name, or the bare entity when unique (resolves with a top-entity-fallback warning)"});
  out.push_back(Set_option{"lhd.stats",
                           "lhd",
                           "false",
                           "print the pass's aggregate statistics report; the canonical form of the --stats flag (either "
                           "spelling works). Consumed by the passes that have one (pass.color, pass.semdiff)"});
  // The `sim.*` command namespace (consumed by sim_command, not an EPRP method):
  // keep `lhd list options` / `lhd describe` complete. Single source of truth =
  // kSimSetOptions, which also drives check_known_set_passes / the sim --help block.
  for (const auto& s : kSimSetOptions) {
    if (std::string_view{s.help}.starts_with("DEPRECATED")) {
      continue;  // accepted spelling, canonical name listed instead
    }
    out.push_back(Set_option{std::format("sim.{}", s.name), "sim", std::string{s.default_value}, std::string{s.help}});
  }
  std::sort(out.begin(), out.end(), [](const Set_option& a, const Set_option& b) { return a.name < b.name; });
  return out;
}

void run_engine_command(Options& opts, Result& res) {
  validate_emits(opts);
  validate_dumps(opts);
  check_known_set_passes(opts);  // --set AND --config table names: a typo'd pass must error, not no-op
  apply_log_settings(opts);      // --set <channel>.log=<level> -> livehd::log (developer logging)
  apply_lhd_settings(opts);      // --set lhd.seed / lhd.top -> the shared kernel Options fields

  // --depfile is supported on both frontends: the Verilog flow lists its
  // declared inputs, and the Pyrope flow additionally folds in every file the
  // Source_locator tables saw (the locator's file table IS the
  // actually-read-files list, covering import discovery).

  if (opts.command == "compile") {
    compile_command(opts, res);
  } else if (opts.command == "lec") {
    lec_command(opts, res);
  } else if (opts.command == "formal") {
    formal_command(opts, res);
  } else if (opts.command == "scan") {
    scan_command(opts, res);
  } else if (opts.command == "tool") {
    tool_command(opts, res);
  } else if (opts.command == "pass") {
    pass_command(opts, res);
  } else if (opts.command == "sim") {
    sim_command(opts, res);
  } else {
    throw Lhd_error{"usage", std::format("unknown command '{}'", opts.command), ""};
  }
}

}  // namespace lhd
