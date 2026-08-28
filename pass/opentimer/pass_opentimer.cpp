//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_opentimer.hpp"

#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_split.h"
#include "str_tools.hpp"

static Pass_plugin sample("pass_opentimer", Pass_opentimer::setup);

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::time_work);
  m1.add_label_required("files", "Liberty, spef, sdc file[s] for timing");
  m1.add_label_optional("margin", "% arrival time marging (0-100)", "0");
  m1.add_label_optional("top", "analyze only this module (required when the library holds several defs)", "");
  m1.add_label_optional("hier",
                        "whole-design timing across the instance hierarchy (true|false|stitch, default true): true "
                        "structurally flattens the design (hierarchy inlined into a scratch def, node names keep the "
                        "dotted instance path) and times it as one module, so the critical path can span modules; "
                        "false times one tech-mapped module per run and rejects non-Liberty Subs; stitch is the "
                        "legacy name-stitched hier walk kept for debugging (module-boundary buses are not stitched "
                        "there)",
                        "true");
  m1.add_label_optional("qor",
                        "write the timing JSON (max delay, critical pin, endpoint arrivals, source-attributed) to this file "
                        "(`lhd pass opentimer` defaults it to <workdir>/timing.json when --workdir is set)",
                        "");
  m1.add_label_optional("stats", "report one timing row per pass.abc (definition, color), including resynth=1|0", "false");
  m1.add_label_optional("cache_dir",
                        "INTERNAL kernel plumbing: the INCREMENTAL STA result cache directory, always <workdir>/sta_cache "
                        "(set after user --set merging, so it is not customizable). An analysis whose NETLIST digest, "
                        "timing-file content and options are unchanged since a prior run replays the stored report "
                        "instead of re-timing; salted by a content hash of this pass plus the @opentimer pin. "
                        "Empty = no user --workdir, or `lhd.incremental=false` = no cache",
                        "");

  register_pass(m1);

  Eprp_method m2("pass.opentimer.power", "Power analysis on lgraph", &Pass_opentimer::power_work);
  m2.add_label_required("files", "Liberty, spef, sdc file[s] for timing");
  m2.add_label_optional("odir", "output directory", ".");
  m2.add_label_optional("freq", "frequency (Hz)", "1e9");

  register_pass(m2);
}

Pass_opentimer::Pass_opentimer(const Eprp_var& var) : Pass("pass.opentimer", var) {
  auto n_lib_read = 0;

  for (const auto f : absl::StrSplit(files, ',')) {
    if (!f.empty()) {
      timing_file_list.emplace_back(f);
    }
    if (str_tools::ends_with(f, ".lib")) {
      // DEFERRED to ensure_libs(): parsing sky130 is ~0.7 s and a STA cache hit
      // needs neither the library nor the timer.
      lib_file_list.emplace_back(f);
      n_lib_read++;
    } else if (str_tools::ends_with(f, ".spef")) {
      spef_file_list.emplace_back(f);
    } else if (str_tools::ends_with(f, ".vcd")) {
      vcd_file_list.emplace_back(f);
    } else if (str_tools::ends_with(f, ".sdc")) {
      sdc_file_list.emplace_back(f);
    } else if (str_tools::ends_with(f, ".v")) {    // Nothing to do
    } else if (str_tools::ends_with(f, ".prp")) {  // Nothing to do
    } else {
      livehd::diag::err("pass.opentimer", "bad-option", "io").msg("unknown file extension '{}'", f).fatal();
    }
  }

  if (n_lib_read > 2) {
    livehd::diag::err("pass.opentimer", "bad-option", "io")
        .msg("only supports 1 or 2 liberty (max/min) files not {}", files)
        .fatal();
  }

  margin = 0;
  if (var.has_label("margin")) {
    std::string txt{var.get("margin")};
    margin = std::stof(txt, nullptr);
    if (margin < 0 || margin > 100) {
      livehd::diag::err("pass.opentimer", "bad-option", "io").msg("margin must be between 0 and 100 not {}", margin).fatal();
    }
  }

  odir = ".";
  if (var.has_label("odir")) {
    odir = var.get("odir");
  }
  freq = 1e9;
  if (var.has_label("freq")) {
    std::string txt{var.get("freq")};
    freq = std::stof(txt, nullptr);
  }
  margin_delay = 0;

  qor_path               = var.get("qor", "");
  cache_dir_             = var.get("cache_dir", "");
  top_filter             = var.get("top", "");
  hier_setting_          = var.get("hier", "true");
  const auto stats_label = std::string{var.get("stats", "false")};
  stats_                 = stats_label != "false" && stats_label != "0" && !stats_label.empty();
  if (hier_setting_ == "1") {
    hier_setting_ = "true";
  } else if (hier_setting_ == "0") {
    hier_setting_ = "false";
  } else if (hier_setting_ != "true" && hier_setting_ != "false" && hier_setting_ != "stitch") {
    livehd::diag::err("pass.opentimer", "bad-option", "io")
        .msg("hier expects true|false|stitch, got '{}'", hier_setting_)
        .fatal();
  }
}

void Pass_opentimer::ensure_libs() {
  if (libs_loaded_) {
    return;
  }
  libs_loaded_ = true;
  for (size_t i = 0; i < lib_file_list.size(); ++i) {
    std::print("opentimer using liberty file '{}'\n", lib_file_list[i]);
    if (i == 0) {
      timer.read_celllib(lib_file_list[i]);
    } else {
      timer.read_celllib(lib_file_list[i], ot::MIN);
    }
  }
  // Flush the (lineage-queued) read_celllib so build_circuit can validate cell
  // names against the loaded library at queue time. The design is still empty,
  // so this is a no-op timing update.
  timer.update_timing();
}

// SDC is Tcl: its options are ORDER-FREE, so every directive below is parsed by
// SCANNING its tokens, never by indexing them. The previous version indexed
// (`line_vec[0]`, `std::stof(line_vec[1])`), which meant a blank line -- the
// split yields NO tokens for one -- read past the end of the vector and took the
// whole process down with SIGSEGV, and any file writing the delay after its
// flags (`set_input_delay -clock clk 0.0 …`, the spelling OpenSTA and every SDC
// generator emit) threw out of std::stof. Both are ordinary constraint files.
//
// What is modelled: create_clock, and the three port constraints
// (set_input_delay / set_input_transition / set_output_delay) over
// [get_ports …], [all_inputs] and [all_outputs]. Every other SDC command is
// REPORTED as ignored (once per command name) rather than dropped in silence --
// an ignored `set_false_path` or `set_load` changes the reported delay, so the
// reader has to be told. Malformed input (a flag with no value, a delay that is
// not a number) is an error: it means the file says something we are not
// reading, which is never safe to guess at.
namespace {

// Tcl bracket/brace wrappers around one token: `[get_ports` -> `get_ports`,
// `clk]` -> `clk`, `[all_outputs]` -> `all_outputs`, `{a` -> `a`.
std::string sdc_strip(std::string_view t) {
  while (!t.empty() && (t.front() == '[' || t.front() == '{')) {
    t.remove_prefix(1);
  }
  while (!t.empty() && (t.back() == ']' || t.back() == '}')) {
    t.remove_suffix(1);
  }
  return std::string{t};
}

bool sdc_closes(std::string_view t) { return !t.empty() && t.back() == ']'; }

// A token is the constraint's VALUE only if it parses as a complete number.
// `-min` must not be read as one, and neither must a port called `1a`.
bool sdc_number(const std::string& t, float& out) {
  if (t.empty()) {
    return false;
  }
  char*       endp  = nullptr;
  const float v     = std::strtof(t.c_str(), &endp);
  if (endp == t.c_str() || *endp != '\0') {
    return false;
  }
  out = v;
  return true;
}

}  // namespace

void Pass_opentimer::read_sdc(std::string_view sdc_file) {
  std::ifstream file(std::string{sdc_file});
  if (!file.is_open()) {
    livehd::diag::err("pass.opentimer", "missing-file", "io").msg("could not open sdc:{}", sdc_file).fatal();
    return;
  }

  std::string line;
  size_t      lineno = 0;

  // Ports named by a create_clock, so `[all_inputs -no_clocks]` can exclude
  // them. Both spellings are recorded: the clock's -name and its [get_ports].
  absl::flat_hash_set<std::string> clock_ports;
  // One report per ignored SDC command, however many times it appears.
  absl::flat_hash_set<std::string> reported_ignored;

  const auto fail = [&](std::string_view code, const std::string& what) {
    livehd::diag::err("pass.opentimer", code, "io")
        .msg("{}:{}: {}", sdc_file, lineno, what)
        .hint(std::string{line})
        .fatal();
  };

  while (std::getline(file, line)) {
    ++lineno;
    const std::vector<std::string> tok = absl::StrSplit(line, absl::ByAnyChar(" \t\r"), absl::SkipWhitespace());
    if (tok.empty() || tok[0].front() == '#') {
      continue;  // blank line or Tcl comment
    }
    const std::string& directive = tok[0];

    if (directive == "create_clock") {
      float       period = 1000;
      std::string pname  = "clock";
      for (std::size_t i = 1; i < tok.size(); i++) {
        if (tok[i] == "-period" || tok[i] == "-name") {
          if (i + 1 >= tok.size()) {
            fail("sdc-syntax", std::format("create_clock {} needs a value", tok[i]));
          }
          if (tok[i] == "-name") {
            pname = tok[i + 1];
          } else if (float v = 0; sdc_number(tok[i + 1], v)) {
            period = v;
          } else {
            fail("sdc-syntax", std::format("create_clock -period expects a number, got '{}'", tok[i + 1]));
          }
          ++i;
        } else if (sdc_strip(tok[i]) == "get_ports") {
          for (std::size_t j = i + 1; j < tok.size(); ++j) {
            clock_ports.insert(sdc_strip(tok[j]));
            if (sdc_closes(tok[j])) {
              i = j;
              break;
            }
          }
        }
      }
      clock_ports.insert(pname);
      timer.create_clock(pname, period);
      continue;
    }

    const bool is_at    = directive == "set_input_delay";
    const bool is_slew  = directive == "set_input_transition";
    const bool is_rat   = directive == "set_output_delay";
    if (!is_at && !is_slew && !is_rat) {
      // Recognized SDC, not modelled here. Say so once: a dropped exception or
      // load changes the number this pass reports.
      if (reported_ignored.insert(directive).second) {
        livehd::diag::warn("pass.opentimer", "sdc-ignored", "unsupported")
            .msg("{}:{}: SDC command '{}' is not modelled by pass.opentimer and was ignored", sdc_file, lineno, directive)
            .emit();
      }
      continue;
    }

    float                    value = 0;
    bool                     have_value = false;
    bool                     want_min = false, want_max = false, want_rise = false, want_fall = false;
    bool                     all_inputs = false, all_outputs = false, no_clocks = false;
    std::vector<std::string> ports;

    for (std::size_t i = 1; i < tok.size(); i++) {
      const std::string& t = tok[i];
      if (t == "-min") {
        want_min = true;
      } else if (t == "-max") {
        want_max = true;
      } else if (t == "-rise") {
        want_rise = true;
      } else if (t == "-fall") {
        want_fall = true;
      } else if (t == "-clock" || t == "-reference_pin") {
        if (i + 1 >= tok.size()) {
          fail("sdc-syntax", std::format("{} {} needs a value", directive, t));
        }
        ++i;  // the clock/pin is not modelled: one timer, one corner set
      } else if (t == "-add_delay" || t == "-clock_fall" || t == "-level_sensitive" || t == "-network_latency_included"
                 || t == "-source_latency_included") {
        // no argument, no effect on what this pass computes
      } else if (const std::string bare = sdc_strip(t); bare == "get_ports" || bare == "all_inputs" || bare == "all_outputs") {
        if (bare == "all_inputs") {
          all_inputs = true;
        } else if (bare == "all_outputs") {
          all_outputs = true;
        }
        if (sdc_closes(t)) {
          continue;  // `[all_outputs]` is self-closing
        }
        for (std::size_t j = i + 1; j < tok.size(); ++j) {
          const std::string arg = sdc_strip(tok[j]);
          if (arg == "-no_clocks") {
            no_clocks = true;
          } else if (bare == "get_ports" && !arg.empty()) {
            ports.push_back(arg);
          }
          if (sdc_closes(tok[j])) {
            i = j;
            break;
          }
          i = j;
        }
      } else if (!have_value && sdc_number(t, value)) {
        have_value = true;
      } else {
        fail("sdc-unsupported", std::format("{} does not understand '{}'", directive, t));
      }
    }

    if (!have_value) {
      fail("sdc-syntax", std::format("{} has no delay/transition value", directive));
    }
    if (ports.empty() && !all_inputs && !all_outputs) {
      fail("sdc-unsupported", std::format("{} needs [get_ports X], [all_inputs] or [all_outputs]", directive));
    }
    if (all_inputs) {
      for (const auto& [pname, _] : timer.primary_inputs()) {
        if (!no_clocks || !clock_ports.contains(pname)) {
          ports.push_back(pname);
        }
      }
    }
    if (all_outputs) {
      for (const auto& [pname, _] : timer.primary_outputs()) {
        ports.push_back(pname);
      }
    }

    // No -min/-max selects both splits, no -rise/-fall both transitions --
    // the SDC default, and what the three inlined ladders this replaces did.
    const bool do_min  = want_min || !want_max;
    const bool do_max  = want_max || !want_min;
    const bool do_rise = want_rise || !want_fall;
    const bool do_fall = want_fall || !want_rise;
    for (const auto& pname : ports) {
      for (const auto split : {ot::MIN, ot::MAX}) {
        if ((split == ot::MIN && !do_min) || (split == ot::MAX && !do_max)) {
          continue;
        }
        for (const auto tran : {ot::RISE, ot::FALL}) {
          if ((tran == ot::RISE && !do_rise) || (tran == ot::FALL && !do_fall)) {
            continue;
          }
          if (is_at) {
            timer.set_at(pname, split, tran, value);
          } else if (is_slew) {
            timer.set_slew(pname, split, tran, value);
          } else {
            timer.set_rat(pname, split, tran, value);
          }
        }
      }
    }
  }
  file.close();
}
