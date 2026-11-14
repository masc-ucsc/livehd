//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_opentimer.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_split.h"
#include "core/diag.hpp"
#include "pass/timing/sdc.hpp"
#include "str_tools.hpp"

static Pass_plugin sample("pass_opentimer", Pass_opentimer::setup);

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::time_work);
  m1.add_label_required("files", "Liberty, spef, sdc file[s] for timing");
  m1.add_label_optional("extra_libs",
                        "Liberty file[s] with more cells for BOTH corners (e.g. the provisional domino cells), loaded after "
                        "`files`' own max/min library pair",
                        "");
  m1.add_label_optional("margin", "% arrival time marging (0-100)", "0");
  m1.add_label_optional("critical", "Annotate all nodes on paths near their timing target; requires targets or SDC", "false");
  m1.add_label_optional("critical_percent", "Setup slack margin as a percentage of each target period (0-100)", "5");
  m1.add_label_optional("targets",
                        "Comma-separated clock:period overrides, e.g. clk:1ns,bus:4ns; bare periods use Liberty units",
                        "");
  m1.add_label_optional("io_load", "Primary-output load in fF; negative preserves the default", "-1");
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
  // Like OpenSTA's default, do not time data paths through async preset/clear
  // -> Q arcs (recovery/removal checks on those pins remain).
  timer.preset_clear_arcs(false);
  // A negative-edge check (clock-gate enable flops) captures the rising-edge
  // launch of the same cycle, as OpenSTA times a half-cycle path.
  timer.falling_edge_same_cycle(true);
  physical_rc_          = var.physical_rc;
  physical_timing_cuts_ = var.physical_timing_cuts;
  parse_critical_options(var);
  const auto load_text = std::string(var.get("io_load", "-1"));
  char*      load_end  = nullptr;
  io_load_ff_          = std::strtof(load_text.c_str(), &load_end);
  if (load_end == load_text.c_str() || *load_end != '\0' || !std::isfinite(io_load_ff_)) {
    livehd::diag::err("pass.opentimer", "bad-option", "io").msg("io_load must be a finite capacitance in fF").fatal();
  }
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

  // Cells the max/min pair does not carry: read into both corners, and part of
  // the file list the STA cache and the physical session key on.
  for (const auto f : absl::StrSplit(std::string(var.get("extra_libs", "")), ',')) {
    if (!f.empty()) {
      timing_file_list.emplace_back(f);
      extra_lib_file_list.emplace_back(f);
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
    livehd::diag::err("pass.opentimer", "bad-option", "io").msg("hier expects true|false|stitch, got '{}'", hier_setting_).fatal();
  }
}

bool Pass_opentimer::configure_session_report(const Eprp_var& var) {
  const Pass_opentimer requested(var);
  // Reporting/cache policy may change, but timing semantics must match. The
  // physical session already owns a flat design; legacy stitching/annotations
  // still take the standalone path. Nonzero margin annotates graph colors.
  if (requested.critical_ || requested.margin != 0 || requested.hier_setting_ == "stitch"
      || requested.timing_file_list != timing_file_list || requested.top_filter != top_filter
      || requested.io_load_ff_ != io_load_ff_ || requested.targets_text_ != targets_text_ || requested.qor_path.empty()) {
    return false;
  }
  margin                = requested.margin;
  critical_percent_     = requested.critical_percent_;
  qor_path              = requested.qor_path;
  cache_dir_            = requested.cache_dir_;
  stats_                = requested.stats_;
  hier_setting_         = requested.hier_setting_;
  physical_timing_cuts_ = requested.physical_timing_cuts_;
  if (var.physical_rc) {
    physical_timing_cuts_.insert(var.physical_rc->timing_cuts.begin(), var.physical_rc->timing_cuts.end());
  }
  session_report_ = true;
  return true;
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
  for (const auto& file : extra_lib_file_list) {
    std::print("opentimer using extra liberty file '{}'\n", file);
    timer.read_celllib(file);
  }
  // Flush the (lineage-queued) read_celllib so build_circuit can validate cell
  // names against the loaded library at queue time. The design is still empty,
  // so no timing propagation is needed.
  timer.update_netlist();
  // A transparent LATCH cell (pass.abc maps level-sensitive latches onto the
  // Liberty's own: ASAP7 DHLx1/DLLx1, sky130 dlxtp/dlxtn) carries a
  // combinational data->Q arc beside its enable->Q edge arc. OpenTimer times
  // that arc like a gate's, chaining every latch pipeline end to end through
  // the transparent windows (minion: a 50 ns path through ~600 latch cells,
  // Q->D hold loops included). The native Latch these cells replace is a hard
  // path boundary here (transparency and time borrowing are not modeled; see
  // the 4th phase in opentimer.cpp), so keep that convention for the cell:
  // drop the data->Q arc, and a latch cell ends a path at its D setup check
  // and launches one from its enable edge, exactly like a flop cell. The cells
  // are recognized by that arc shape (an output with an edge-triggered arc AND
  // a combinational arc from a different pin), before any gate is inserted.
  for (const auto corner : {ot::MIN, ot::MAX}) {
    const auto& lib = timer.celllib(corner);
    if (!lib) {
      continue;
    }
    for (auto& [cell_name, cell] : const_cast<ot::Celllib&>(*lib).cells) {
      (void)cell_name;
      for (auto& [pin_name, pin] : cell.cellpins) {
        (void)pin_name;
        if (pin.direction != ot::CellpinDirection::OUTPUT) {
          continue;
        }
        absl::flat_hash_set<std::string> edge_pins;
        for (const auto& t : pin.timings) {
          if (t.is_rising_edge_triggered() || t.is_falling_edge_triggered()) {
            edge_pins.insert(t.related_pin);
          }
        }
        if (edge_pins.empty()) {
          continue;
        }
        std::erase_if(pin.timings, [&](const ot::Timing& t) {
          const auto type = t.type.value_or(ot::TimingType::COMBINATIONAL);  // the Liberty default
          return (type == ot::TimingType::COMBINATIONAL || type == ot::TimingType::COMBINATIONAL_RISE
                  || type == ot::TimingType::COMBINATIONAL_FALL)
                 && !edge_pins.contains(t.related_pin);
        });
      }
    }
  }
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

// Tcl wrappers around a scalar token or selector head. Port-list contents
// are handled separately so literal bus indices keep their closing bracket.
std::string sdc_strip(std::string_view t) {
  if (!t.empty() && (t.front() == '[' || t.front() == '{' || t.front() == '"')) {
    const char closing = t.front() == '[' ? ']' : t.front() == '{' ? '}' : '"';
    t.remove_prefix(1);
    if (!t.empty() && t.back() == closing) {
      t.remove_suffix(1);
    }
  }
  return std::string{t};
}

struct Sdc_selector {
  std::vector<std::string> args;
  bool                     closed = false;
};

// Read the outer command substitution, not the first bus-index ']'. Braced
// port lists quote their brackets literally; escapes and balanced unbraced
// brackets must likewise survive as part of the selected pin name.
Sdc_selector sdc_selector(const std::vector<std::string>& tokens, size_t& index) {
  Sdc_selector result;
  unsigned     braces = 0, brackets = 1;
  bool         quoted = false;
  for (++index; index < tokens.size(); ++index) {
    std::string argument;
    const auto& token = tokens[index];
    for (size_t offset = 0; offset < token.size(); ++offset) {
      const char c = token[offset];
      if (c == '\\' && offset + 1 < token.size()) {
        argument += token[++offset];
      } else if (c == '{' && !quoted) {
        if (braces++) {
          argument += c;
        }
      } else if (c == '}' && !quoted && braces) {
        if (--braces) {
          argument += c;
        }
      } else if (c == '"' && !braces) {
        quoted = !quoted;
      } else if (c == '[' && !braces && !quoted) {
        ++brackets;
        argument += c;
      } else if (c == ']' && !braces && !quoted) {
        if (--brackets == 0) {
          if (!argument.empty()) {
            result.args.push_back(std::move(argument));
          }
          result.closed = offset + 1 == token.size();
          return result;
        }
        argument += c;
      } else {
        argument += c;
      }
    }
    if (!argument.empty()) {
      result.args.push_back(std::move(argument));
    }
  }
  return result;
}

// Tcl string match over a port name: `*` any run, `?` one character, the rest literal (a bus index's brackets
// are literal here, as every SDC writer means them).
bool sdc_glob(std::string_view pattern, std::string_view text) {
  size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

// A token is the constraint's VALUE only if it parses as a complete number.
// `-min` must not be read as one, and neither must a port called `1a`.
bool sdc_number(const std::string& t, float& out) {
  if (t.empty()) {
    return false;
  }
  char*       endp = nullptr;
  const float v    = std::strtof(t.c_str(), &endp);
  if (endp == t.c_str() || *endp != '\0' || !std::isfinite(v)) {
    return false;
  }
  out = v;
  return true;
}

// The timing graph as the constraint reader queries it. A port is named as an SDC names it: Pin_tracker
// calls bit zero of a bus by the base name and the later bits `base.N`, which is `base[N]` here (a graph that
// already has literal bracket names keeps them). The launch and capture points the pass adds for state
// elements are not ports of the design. An instance pin is `instance/pin`, `instance:pin` in the timer.
class Timer_design final : public livehd::timing::fast::Sdc_design {
public:
  Timer_design(const ot::Timer& timer, const std::map<std::string, std::string>& start_clocks,
               const std::map<std::string, std::string>& end_clocks)
      : timer_(timer) {
    const auto add = [&](const auto& ports, const auto& hidden, bool input) {
      for (const auto& [name, port] : ports) {
        (void)port;
        if (hidden.contains(name)) {
          continue;
        }
        std::string spelled = name;
        if (const auto dot = name.rfind('.'); dot != std::string::npos && dot + 1 < name.size()
                                              && name.find_first_not_of("0123456789", dot + 1) == std::string::npos
                                              && ports.contains(name.substr(0, dot))) {
          spelled = name.substr(0, dot) + "[" + name.substr(dot + 1) + "]";
        } else if (ports.contains(name + ".1")) {
          spelled = name + "[0]";
        }
        ports_.emplace_back(std::move(spelled), input);
      }
    };
    add(timer.primary_inputs(), start_clocks, true);
    add(timer.primary_outputs(), end_clocks, false);
  }
  void ports(const std::function<void(std::string_view, bool)>& visit) const override {
    for (const auto& [name, input] : ports_) {
      visit(name, input);
    }
  }
  void pins(const std::function<void(std::string_view, std::string_view)>& visit) const override {
    for (const auto& [key, pin] : timer_.pins()) {
      (void)pin;
      if (const auto colon = key.rfind(':'); colon != std::string::npos) {
        visit(std::string_view(key).substr(0, colon), std::string_view(key).substr(colon + 1));
      }
    }
  }
  bool has_pin(std::string_view instance, std::string_view pin) const override {
    if (timer_.pins().contains(instance.empty() ? std::string(pin) : std::string(instance) + ":" + std::string(pin))) {
      return true;
    }
    const auto gate = gate_of(instance);
    return !gate.empty() && timer_.pins().contains(gate + ":" + std::string(pin));
  }
  void nets(const std::function<void(std::string_view)>& visit) const override {
    for (const auto& [name, net] : timer_.nets()) {
      (void)net;
      visit(name);
    }
  }
  // The net of a register carries the register's name in an RTL netlist; here that name is its instance.
  bool has_net(std::string_view name) const override { return timer_.nets().contains(std::string(name)) || !gate_of(name).empty(); }
  void cells(const std::function<void(std::string_view)>& visit) const override {
    for (const auto& [name, gate] : timer_.gates()) {
      (void)gate;
      visit(name);
    }
  }
  bool               has_cell(std::string_view name) const override { return !gate_of(name).empty(); }
  // The command handlers below resolve names themselves and have their own rule for a missing one.
  [[nodiscard]] bool keep_unmatched() const override { return true; }

private:
  // The gate an instance name of a constraint file means: the gate of that name, or the one gate whose name is
  // that plus the node suffix the pass adds (`reg[3]__n412`). A file written against a Yosys netlist names a
  // register by its fine-grain cell, `reg[3]$_DFF_PP0_`: the gate of the register is `reg[3]`.
  [[nodiscard]] std::string gate_of(std::string_view instance) const {
    if (instance.empty()) {
      return {};
    }
    if (timer_.gates().contains(std::string(instance))) {
      return std::string(instance);
    }
    if (!indexed_) {
      indexed_ = true;
      for (const auto& [name, gate] : timer_.gates()) {
        (void)gate;
        if (const auto tail = name.rfind("__n"); tail != std::string::npos && tail != 0 && tail + 3 < name.size()
                                                 && name.find_first_not_of("0123456789", tail + 3) == std::string::npos) {
          // Two gates of one base name are no answer to either.
          const auto [it, fresh] = by_base_.emplace(name.substr(0, tail), name);
          if (!fresh) {
            it->second.clear();
          }
        }
      }
    }
    if (const auto it = by_base_.find(std::string(instance)); it != by_base_.end()) {
      return it->second;
    }
    if (const auto tail = instance.rfind("$_"); tail != std::string_view::npos && tail != 0 && instance.back() == '_') {
      return gate_of(instance.substr(0, tail));
    }
    return {};
  }

  const ot::Timer&                                     timer_;
  std::vector<std::pair<std::string, bool>>           ports_;
  mutable bool                                         indexed_ = false;
  mutable absl::flat_hash_map<std::string, std::string> by_base_;
};

}  // namespace

void Pass_opentimer::read_sdc(std::string_view sdc_file) {
  // Circuit construction is queued in OpenTimer. Materialize the ports before
  // expanding all_inputs/all_outputs; otherwise those collections are empty.
  timer.update_netlist();
  std::ifstream file(std::string{sdc_file});
  if (!file.is_open()) {
    livehd::diag::err("pass.opentimer", "missing-file", "io").msg("could not open sdc:{}", sdc_file).fatal();
    return;
  }
  std::stringstream text;
  text << file.rdbuf();
  file.close();
  // The file is a Tcl script. The shared constraint reader runs it against this timing graph (variables,
  // expressions, lists, loops, conditions, object queries) and hands back the flat list of the SDC commands it
  // executed; the handlers below read those. The numbers stay in the units of the file.
  const auto script = livehd::timing::fast::read_sdc(text.str(), {}, Timer_design(timer, native_start_clocks_, native_end_clocks_));

  std::string line;
  size_t      lineno = 0;

  // Ports named by a create_clock, so `[all_inputs -no_clocks]` can exclude
  // them. Both spellings are recorded: the clock's -name and its [get_ports].
  absl::flat_hash_set<std::string> clock_ports;
  // One report per ignored SDC command, however many times it appears.
  absl::flat_hash_set<std::string> reported_ignored;

  const auto fail = [&](std::string_view code, const std::string& what) {
    livehd::diag::err("pass.opentimer", code, "io").msg("{}:{}: {}", sdc_file, lineno, what).hint(std::string{line}).fatal();
  };
  const auto port_name = [&](const std::string& name) {
    const auto exists = [&](const std::string& candidate) {
      return timer.primary_inputs().contains(candidate) || timer.primary_outputs().contains(candidate);
    };
    if (exists(name) || name.empty() || name.back() != ']') {
      return name;
    }
    const auto open = name.rfind('[');
    if (open == std::string::npos || open + 2 >= name.size()) {
      return name;
    }
    const auto index = name.substr(open + 1, name.size() - open - 2);
    if (index.find_first_not_of("0123456789") != std::string::npos) {
      return name;
    }
    // Pin_tracker names bit zero by the base port and later bits base.N.
    // A graph whose port already has the literal bracket name wins above.
    auto candidate = name.substr(0, open);
    if (index != "0") {
      candidate += "." + index;
    }
    return exists(candidate) ? candidate : name;
  };

  // A command this pass cannot honor: fatal when annotations are critical,
  // else flag the report incomplete and say so once per command.
  const auto warn_ignored = [&](const std::string& directive, std::string_view why) {
    if (critical_) {
      fail("critical-constraints", std::format("critical annotations cannot honor unsupported SDC command '{}'", directive));
    }
    constraints_complete_ = false;
    // Recognized SDC, not modelled here. Say so once: a dropped exception or
    // load changes the number this pass reports.
    if (reported_ignored.insert(directive).second) {
      livehd::diag::warn("pass.opentimer", "sdc-ignored", "unsupported")
          .msg("{}:{}: SDC command '{}' {} and was ignored", sdc_file, lineno, directive, why)
          .emit();
    }
  };
  // Directives whose lines this reader models: a bad Tcl substitution there is a hard error.
  static const absl::flat_hash_set<std::string> modelled{"create_clock",
                                                         "set_input_delay",
                                                         "set_input_transition",
                                                         "set_output_delay",
                                                         "set_load",
                                                         "set_driving_cell",
                                                         "set_false_path",
                                                         "set_ideal_network",
                                                         "set_clock_transition",
                                                         "set_propagated_clock",
                                                         "current_design"};

  for (const auto& entry : script.commands) {
    lineno = entry.line;
    line   = entry.error.empty() ? entry.text : entry.name;
    if (!entry.error.empty()) {
      // A command that could not be evaluated: a Tcl error (also the file's own `error`), a failed object query.
      if (modelled.contains(entry.name)) {
        fail("sdc-syntax", entry.error);
      }
      warn_ignored(entry.name.empty() ? std::string("<script>") : entry.name,
                   std::format("could not be evaluated by the constraint reader ({})", entry.error));
      continue;
    }
    const std::string&             expanded = entry.text;
    const std::vector<std::string> tok      = absl::StrSplit(expanded, absl::ByAnyChar(" \t\r"), absl::SkipWhitespace());
    if (tok.empty()) {
      continue;
    }
    const std::string& directive = tok[0];
    if (directive == "current_design") {
      continue;  // the selected timing graph is already bound by lhd
    }

    if (directive == "set_false_path" || directive == "set_ideal_network") {
      // A port-launched or port-captured exception has no path-history state: removing the port's arrival (its
      // required time) removes exactly the paths that start (end) there. `set_false_path -from [get_ports ..]`,
      // `-to [get_ports ..]`, and an ideal network rooted at a port or at a port's net are that. The network is
      // still wired, loaded and repaired (a reset still gets its tree): it only stops being a timing path that is
      // reported or ranked. A -from together with a -to (one pair of ports only), -through, clocks, pins and
      // edge- or corner-specific forms remain unsupported.
      const bool               ideal = directive == "set_ideal_network";
      std::vector<std::string> from, to;
      std::vector<std::string>* into       = ideal ? &from : nullptr;
      bool                      understood = true;
      for (size_t i = 1; i < tok.size() && understood; ++i) {
        const auto& t = tok[i];
        if (t == "-from" && !ideal) {
          into = &from;
        } else if (t == "-to" && !ideal) {
          into = &to;
        } else if (t == "-no_propagate" && ideal) {
          // the net alone, not what its loads drive: for a port's net that is the same start
        } else if (const std::string bare = sdc_strip(t); into && (bare == "get_ports" || (ideal && bare == "get_nets"))) {
          const auto selector = sdc_selector(tok, i);
          understood          = selector.closed && !selector.args.empty();
          into->insert(into->end(), selector.args.begin(), selector.args.end());
        } else {
          understood = false;
        }
      }
      understood = understood && (from.empty() != to.empty());
      // Every bit of the ports a pattern names: a scalar, a whole bus (`d`, `d.1`, .. as Pin_tracker names its
      // bits), one bit `d[3]`, or a glob over any of those spellings.
      const auto matching = [&](const std::vector<std::string>& patterns, const auto& ports) {
        std::set<std::string> found;
        for (const auto& [name, port] : ports) {
          (void)port;
          std::string base = name, bit = name + "[0]";
          if (const auto dot = name.rfind('.');
              dot != std::string::npos && dot + 1 < name.size() && name.find_first_not_of("0123456789", dot + 1) == std::string::npos) {
            base = name.substr(0, dot);
            bit  = base + "[" + name.substr(dot + 1) + "]";
          }
          for (const auto& pattern : patterns) {
            if (sdc_glob(pattern, name) || sdc_glob(pattern, base) || sdc_glob(pattern, bit)) {
              found.insert(name);
            }
          }
        }
        return found;
      };
      if (understood) {
        const auto starts = matching(from, timer.primary_inputs());
        const auto ends   = matching(to, timer.primary_outputs());
        // A pattern that names nothing here is not an exception this netlist needs (a port another partition
        // has); a clock port keeps its arrival, it is the clock.
        for (const auto& name : starts) {
          if (!native_start_clocks_.contains(name) && !clock_ports.contains(name)) {
            false_start_ports_.insert(name);
          }
        }
        false_end_ports_.insert(ends.begin(), ends.end());
        if (!starts.empty() || !ends.empty() || !ideal) {
          continue;
        }
        // An ideal network on a net inside the design (NVDLA's nvdla_core_rstn behind its reset synchronizer):
        // the mapped netlist does not keep that net's name, so it stays timed like any net.
        warn_ignored(directive, "names no port or port net of the mapped design (an internal net is not found by name)");
        continue;
      }
    }
    if (directive == "set_clock_transition") {
      // set_clock_transition [-rise] [-fall] [-min] [-max] value [get_clocks name]: the slew at the clock's source.
      bool                     rise = false, fall = false, lo = false, hi = false, have = false, ok = true;
      float                    value = 0;
      std::vector<std::string> clocks;
      for (size_t i = 1; i < tok.size() && ok; ++i) {
        const auto& t = tok[i];
        if (t == "-rise") {
          rise = true;
        } else if (t == "-fall") {
          fall = true;
        } else if (t == "-min") {
          lo = true;
        } else if (t == "-max") {
          hi = true;
        } else if (!have && sdc_number(t, value)) {
          have = true;
        } else if (sdc_strip(t) == "get_clocks") {
          const auto selector = sdc_selector(tok, i);
          ok                  = selector.closed && !selector.args.empty();
          clocks              = selector.args;
        } else {
          ok = false;
        }
      }
      std::vector<std::string> sources;
      for (const auto& clock : clocks) {
        const auto target = timing_targets_.find(clock);
        ok                = ok && target != timing_targets_.end() && target->second.pin.empty() && !target->second.ports.empty();
        if (ok) {
          sources.insert(sources.end(), target->second.ports.begin(), target->second.ports.end());
        }
      }
      if (ok && have && value >= 0 && !sources.empty()) {
        for (const auto& port : sources) {
          if (!timer.primary_inputs().contains(port)) {
            continue;
          }
          for (auto split : {ot::MIN, ot::MAX}) {
            for (auto tran : {ot::RISE, ot::FALL}) {
              if ((split == ot::MIN ? lo || !hi : hi || !lo) && (tran == ot::RISE ? rise || !fall : fall || !rise)) {
                timer.set_slew(port, split, tran, value);
              }
            }
          }
        }
        continue;
      }
    }
    if (directive == "set_propagated_clock") {
      if (expanded.find("[all_clocks]") != std::string::npos) {
        for (const auto& [name, target] : timing_targets_) {
          (void)target;
          propagated_clocks_.insert(name);
        }
      } else if (expanded.find("[get_clocks ") != std::string::npos) {
        const auto begin = expanded.find("[get_clocks ") + 12, end = expanded.find(']', begin);
        if (end == std::string::npos) {
          fail("sdc-syntax", "set_propagated_clock requires a known clock");
        }
        // `[get_clocks {clk}]`, `"clk"` and `{a b}` name clocks like a bare word does.
        const std::vector<std::string> names
            = absl::StrSplit(expanded.substr(begin, end - begin), absl::ByAnyChar(" \t\r"), absl::SkipWhitespace());
        bool any = false;
        for (auto name : names) {
          name.erase(0, name.find_first_not_of("{}\""));
          name.erase(name.find_last_not_of("{}\"") + 1);
          if (name.empty()) {
            continue;
          }
          if (!timing_targets_.contains(name)) {
            fail("sdc-syntax", "set_propagated_clock requires a known clock");
          }
          propagated_clocks_.insert(name);
          any = true;
        }
        if (!any) {
          fail("sdc-syntax", "set_propagated_clock requires a known clock");
        }
      } else {
        constraints_complete_ = false;
      }
      continue;
    }
    if (directive == "create_clock") {
      ++sdc_clocks_;
      bool                     supported = true, have_period = false, have_name = false;
      float                    period = 1000;
      std::vector<float>       waveform;  // -waveform {rise fall}
      std::string              pname = "clock";
      std::vector<std::string> source_ports, source_pins;
      for (std::size_t i = 1; i < tok.size(); i++) {
        if (tok[i] == "-period" || tok[i] == "-name") {
          if (i + 1 >= tok.size()) {
            fail("sdc-syntax", std::format("create_clock {} needs a value", tok[i]));
          }
          if (tok[i] == "-name") {
            if (have_name) {
              supported = false;
            }
            have_name = true;
            pname     = sdc_strip(tok[i + 1]);
          } else if (float v = 0; sdc_number(tok[i + 1], v)) {
            if (have_period || v <= 0) {
              supported = false;
            }
            have_period = true;
            period      = v;
          } else {
            fail("sdc-syntax", std::format("create_clock -period expects a number, got '{}'", tok[i + 1]));
          }
          ++i;
        } else if (tok[i] == "-waveform") {
          // A braced edge list: `{0 750}` (one or two tokens). Only a single rise/fall pair is modelled.
          bool closed = false;
          for (std::size_t j = i + 1; j < tok.size(); ++j) {
            for (const auto& edge : absl::StrSplit(tok[j], absl::ByAnyChar("{}"), absl::SkipEmpty())) {
              if (float v = 0; sdc_number(std::string(edge), v)) {
                waveform.push_back(v);
              } else {
                fail("sdc-syntax", std::format("create_clock -waveform expects numbers, got '{}'", tok[j]));
              }
            }
            if (!tok[j].empty() && tok[j].back() == '}') {
              i      = j;
              closed = true;
              break;
            }
          }
          supported &= closed && waveform.size() == 2 && waveform[0] >= 0 && waveform[1] > waveform[0];
        } else if (sdc_strip(tok[i]) == "get_ports") {
          const auto selector = sdc_selector(tok, i);
          for (const auto& port : selector.args) {
            clock_ports.insert(port_name(port));
            source_ports.push_back(port_name(port));
          }
          supported &= selector.closed;
        } else if (sdc_strip(tok[i]) == "get_pins") {
          // A clock on an INTERNAL pin: the output of a hard macro that generates the design clock
          // (litepci's `pcie_us/user_clk`). SDC spells the pin `instance/pin`, OpenTimer `instance:pin`.
          const auto selector = sdc_selector(tok, i);
          source_pins.insert(source_pins.end(), selector.args.begin(), selector.args.end());
          supported &= selector.closed;
        } else {
          supported = false;  // Waveforms, generated clocks and Tcl expressions are not guessed.
        }
      }
      if (!have_name && source_ports.size() == 1) {
        pname = source_ports.front();
      }
      // The one pin the clock is attached to, as OpenTimer names it; empty unless a single plain pin of
      // the timing graph is given (a missing or wildcard pin leaves a virtual clock, reported incomplete).
      std::string clock_pin;
      if (supported && source_ports.empty() && source_pins.size() == 1
          && source_pins.front().find_first_of("*?[") == std::string::npos) {
        auto candidate = source_pins.front();
        if (const auto slash = candidate.rfind('/'); slash != std::string::npos) {
          candidate[slash] = ':';
        }
        if (timer.pins().contains(candidate)) {
          clock_pin = std::move(candidate);
        } else if (const auto colon = candidate.rfind(':'); colon != std::string::npos) {
          // The mapped netlist is flattened out of region modules, so an instance carries a hierarchy
          // prefix and a uniquifier (`sub_16.pcie_us__n6780`). Accept exactly one pin on an instance
          // that is the SDC's name once those are removed.
          const std::string        wanted = candidate.substr(0, colon), pin = candidate.substr(colon);
          std::vector<std::string> match;
          for (const auto& [key, pin_data] : timer.pins()) {
            (void)pin_data;
            const auto at = key.rfind(':');
            if (at == std::string::npos || key.compare(at, std::string::npos, pin) != 0) {
              continue;
            }
            std::string_view instance(key.data(), at);
            if (const auto tail = instance.rfind("__n");
                tail != std::string_view::npos && tail + 3 < instance.size()
                && instance.find_first_not_of("0123456789", tail + 3) == std::string_view::npos) {
              instance = instance.substr(0, tail);
            }
            if (instance == wanted
                || (instance.size() > wanted.size() && instance.ends_with(wanted)
                    && instance[instance.size() - wanted.size() - 1] == '.')) {
              match.push_back(key);
            }
          }
          if (match.size() == 1) {
            clock_pin = match.front();
          }
        }
      }
      if (!source_pins.empty() && clock_pin.empty()) {
        supported = false;
        std::string similar;
        // Say what the graph does have under that instance (or pin) name, to make a typo or a hierarchy
        // prefix obvious.
        const auto  slash = source_pins.front().rfind('/');
        const auto  leaf  = slash == std::string::npos ? source_pins.front() : source_pins.front().substr(0, slash);
        size_t      shown = 0;
        for (const auto& [key, pin_data] : timer.pins()) {
          (void)pin_data;
          if (shown < 4 && key.find(leaf) != std::string::npos) {
            similar += (shown++ ? ", " : "") + key;
          }
        }
        livehd::diag::warn("pass.opentimer", "sdc-clock-pin-missing", "unsupported")
            .msg("{}:{}: create_clock {} names no single pin of the timing graph; the clock stays virtual{}",
                 sdc_file,
                 lineno,
                 pname,
                 similar.empty() ? std::format(" (the graph has {} pins, none containing '{}')", timer.num_pins(), leaf)
                                 : " (pins containing '" + leaf + "': " + similar + ")")
            .emit();
      }
      if ((critical_ || !target_overrides_.empty()) && (!supported || !have_period || period <= 0)) {
        fail("critical-constraints", "critical annotations require a positive create_clock period without unsupported modifiers");
      }
      period = override_period(pname, source_ports, period);
      if (critical_) {
        if (timing_targets_.contains(pname)) {
          fail("critical-constraints", std::format("clock '{}' is defined more than once", pname));
        }
        for (const auto& port : source_ports) {
          if (!timer.primary_inputs().contains(port) || native_start_clocks_.contains(port)) {
            fail("critical-constraints", std::format("clock port '{}' does not exist", port));
          }
        }
      }
      if ((critical_ || !target_overrides_.empty()) && !clock_pin.empty()) {
        fail("critical-constraints", "critical annotations cannot honor a clock created on an internal pin");
      }
      if (supported && !waveform.empty() && waveform[1] - waveform[0] >= period) {
        supported = false;  // edges must fit one period
      }
      const bool default_waveform
          = waveform.empty() || (waveform[0] == 0 && std::abs(waveform[1] - period / 2) <= 1e-6f * std::max(1.0f, period));
      if (!default_waveform && source_ports.empty() && clock_pin.empty()) {
        supported = false;  // a virtual clock has no source to carry its edges
      }
      timing_targets_[pname] = {period, source_ports, clock_pin};
      if (supported && source_ports.empty() && clock_pin.empty() && have_period && have_name && !pname.empty()
          && sdc_clocks_ == 1) {
        virtual_clock_name_   = pname;
        virtual_clock_period_ = period;
      } else {
        constraints_complete_ = false;
      }
      if (supported && source_ports.size() == 1 && timer.primary_inputs().contains(source_ports.front())) {
        if (default_waveform) {
          timer.create_clock(pname, source_ports.front(), period);
        } else {
          timer.create_clock(pname, source_ports.front(), period, {waveform[0], waveform[1]});
        }
      } else if (supported && !default_waveform && !clock_pin.empty()) {
        timer.create_clock(pname, clock_pin, period, {waveform[0], waveform[1]});
      } else if (!clock_pin.empty()) {
        timer.create_clock(pname, clock_pin, period);
      } else {
        timer.create_clock(pname, period);
      }
      continue;
    }

    const bool is_at    = directive == "set_input_delay";
    const bool is_slew  = directive == "set_input_transition";
    const bool is_rat   = directive == "set_output_delay";
    const bool is_load  = directive == "set_load";
    const bool is_drive = directive == "set_driving_cell";
    if (!is_at && !is_slew && !is_rat && !is_load && !is_drive) {
      warn_ignored(directive, "is not modelled by pass.opentimer");
      continue;
    }

    float                            value      = 0;
    bool                             have_value = is_drive;
    bool                             want_min = false, want_max = false, want_rise = false, want_fall = false;
    bool                             all_inputs = false, all_outputs = false, no_clocks = false;
    std::vector<std::string>         ports;
    absl::flat_hash_set<std::string> excluded_inputs, excluded_outputs;
    std::string                      reference_clock;
    std::string                      drive_cell, drive_pin, drive_from;
    std::array<float, 2>             drive_slew{0, 0};
    bool                             skipped = false;

    for (std::size_t i = 1; i < tok.size(); i++) {
      const std::string& t = tok[i];
      if (is_drive
          && (t == "-lib_cell" || t == "-pin" || t == "-from_pin" || t == "-input_transition_rise"
              || t == "-input_transition_fall")) {
        if (++i >= tok.size()) {
          fail("sdc-syntax", std::format("{} {} needs a value", directive, t));
        }
        if (t == "-lib_cell") {
          drive_cell = sdc_strip(tok[i]);
        } else if (t == "-pin") {
          drive_pin = sdc_strip(tok[i]);
        } else if (t == "-from_pin") {
          drive_from = sdc_strip(tok[i]);
        } else {
          float transition = 0;
          if (!sdc_number(tok[i], transition) || transition < 0) {
            fail("sdc-syntax", "driving-cell input transition must be nonnegative");
          }
          drive_slew[t == "-input_transition_rise" ? ot::RISE : ot::FALL] = transition;
        }
      } else if (t == "-min") {
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
        if (t == "-reference_pin" || !reference_clock.empty() || is_slew || is_load) {
          if (critical_) {
            fail("critical-constraints", std::format("unsupported or repeated clock reference '{}'", t));
          }
          constraints_complete_ = false;
        }
        reference_clock = sdc_strip(tok[++i]);
      } else if (t == "-add_delay" || t == "-clock_fall" || t == "-level_sensitive" || t == "-network_latency_included"
                 || t == "-source_latency_included") {
        if (critical_) {
          fail("critical-constraints", std::format("critical annotations cannot honor '{}'", t));
        }
        // no argument, no effect on what this pass computes
        constraints_complete_ = false;
      } else if (const std::string bare = sdc_strip(t); bare == "get_ports" || bare == "all_inputs" || bare == "all_outputs") {
        if (bare == "all_inputs") {
          all_inputs = true;
        } else if (bare == "all_outputs") {
          all_outputs = true;
        }
        if (!t.empty() && t.back() == ']') {
          continue;  // `[all_outputs]` is self-closing
        }
        const auto selector = sdc_selector(tok, i);
        for (size_t j = 0; j < selector.args.size(); ++j) {
          const auto& arg = selector.args[j];
          if (arg == "-no_clocks" && bare == "all_inputs") {
            no_clocks = true;
          } else if (arg == "-exclude_exact" && (bare == "all_inputs" || bare == "all_outputs")) {
            if (++j >= selector.args.size()) {
              fail("sdc-syntax", "port exclusion needs a port name");
            }
            (bare == "all_inputs" ? excluded_inputs : excluded_outputs).insert(selector.args[j]);
          } else if (bare == "get_ports" && !arg.empty()) {
            ports.push_back(arg);
          } else if (!arg.empty()) {
            if (critical_) {
              fail("critical-constraints", std::format("unsupported port selector '{}'", arg));
            }
            constraints_complete_ = false;
          }
        }
        constraints_complete_ &= selector.closed;
        if (critical_ && !selector.closed) {
          fail("sdc-syntax", "port selector is missing its closing bracket");
        }
      } else if (!have_value && sdc_number(t, value)) {
        have_value = true;
      } else if (is_load || is_drive) {
        // Options (-pin_load, -library, ...) and selectors (get_nets) not modelled:
        // skip the whole command, as before these directives were parsed.
        warn_ignored(directive, "uses an option or selector pass.opentimer does not model");
        skipped = true;
        break;
      } else {
        fail("sdc-unsupported", std::format("{} does not understand '{}'", directive, t));
      }
    }
    if (skipped) {
      continue;
    }

    if (!have_value) {
      fail("sdc-syntax", std::format("{} has no delay/transition value", directive));
    }
    if (ports.empty() && !all_inputs && !all_outputs) {
      fail("sdc-unsupported", std::format("{} needs [get_ports X], [all_inputs] or [all_outputs]", directive));
    }
    if (all_inputs) {
      for (const auto& [pname, _] : timer.primary_inputs()) {
        if (!native_start_clocks_.contains(pname) && !excluded_inputs.contains(pname)
            && (!no_clocks || !clock_ports.contains(pname))) {
          ports.push_back(pname);
        }
      }
    }
    if (all_outputs) {
      for (const auto& [pname, _] : timer.primary_outputs()) {
        if (!native_end_clocks_.contains(pname) && !excluded_outputs.contains(pname)) {
          ports.push_back(pname);
        }
      }
    }
    for (auto& port : ports) {
      port = port_name(port);
    }

    const bool clocked
        = !reference_clock.empty() && reference_clock == virtual_clock_name_ && virtual_clock_period_ > 0 && sdc_clocks_ == 1;
    if (critical_ && is_rat && reference_clock.empty()) {
      fail("critical-constraints", "critical annotations require set_output_delay to name its -clock");
    }
    if ((!reference_clock.empty() && !clocked) || (is_rat && !clocked) || (is_at && virtual_clock_period_ > 0 && !clocked)) {
      constraints_complete_ = false;
    }

    // No -min/-max selects both splits, no -rise/-fall both transitions --
    // the SDC default, and what the three inlined ladders this replaces did.
    const bool do_min  = want_min || !want_max;
    const bool do_max  = want_max || !want_min;
    const bool do_rise = want_rise || !want_fall;
    const bool do_fall = want_fall || !want_rise;
    if (!critical_ && (is_load || is_drive)) {
      // Validate before touching the timer so a skipped command leaves no partial state.
      bool applicable = true;
      for (const auto& pname : ports) {
        applicable &= is_load ? timer.primary_outputs().contains(pname) : timer.primary_inputs().contains(pname);
      }
      for (const auto split : {ot::MIN, ot::MAX}) {
        if (is_drive && applicable && ((split == ot::MIN && do_min) || (split == ot::MAX && do_max))) {
          const auto& library  = timer.celllib(split);
          applicable          &= library && library->cell(drive_cell);
        }
      }
      if (!applicable) {
        warn_ignored(directive, "names ports or a library cell this design does not have");
        continue;
      }
    }
    for (const auto& pname : ports) {
      if ((is_at || is_slew || is_drive) ? !timer.primary_inputs().contains(pname) : !timer.primary_outputs().contains(pname)) {
        if (critical_) {
          fail("critical-constraints", std::format("SDC port '{}' does not exist", pname));
        }
        constraints_complete_ = false;
      }
      if (critical_ && !reference_clock.empty() && !timing_targets_.contains(reference_clock)) {
        fail("critical-constraints", std::format("SDC clock '{}' is not defined", reference_clock));
      }
      if (is_rat && !reference_clock.empty()) {
        if (critical_ && output_clocks_.contains(pname) && output_clocks_.at(pname) != reference_clock) {
          fail("critical-constraints", std::format("multiple clocks constrain output '{}'", pname));
        }
        output_clocks_[pname] = reference_clock;
      }
      if (is_at && !reference_clock.empty()) {
        if (critical_ && input_clocks_.contains(pname) && input_clocks_.at(pname) != reference_clock) {
          fail("critical-constraints", std::format("multiple clocks constrain input '{}'", pname));
        }
        input_clocks_[pname] = reference_clock;
      }
      for (const auto split : {ot::MIN, ot::MAX}) {
        if ((split == ot::MIN && !do_min) || (split == ot::MAX && !do_max)) {
          continue;
        }
        for (const auto tran : {ot::RISE, ot::FALL}) {
          if ((tran == ot::RISE && !do_rise) || (tran == ot::FALL && !do_fall)) {
            continue;
          }
          const auto bit = static_cast<uint8_t>(1U << (2 * static_cast<unsigned>(split) + static_cast<unsigned>(tran)));
          if (clocked && (is_at || is_rat)) {
            (is_at ? clock_inputs_ : clock_outputs_)[pname] |= bit;
          }
          if (is_drive) {
            if (!timer.primary_inputs().contains(pname)) {
              fail("sdc-unsupported", "set_driving_cell requires primary inputs");
            }
            const auto& library = timer.celllib(split);
            const auto* cell    = library ? library->cell(drive_cell) : nullptr;
            if (!cell) {
              fail("sdc-unsupported", "set_driving_cell needs a known -lib_cell");
            }
            auto output = drive_pin;
            if (output.empty()) {
              for (const auto& [name, pin] : cell->cellpins) {
                if (pin.direction == ot::CellpinDirection::OUTPUT) {
                  if (!output.empty()) {
                    fail("sdc-unsupported", "set_driving_cell requires -pin for multiple outputs");
                  }
                  output = name;
                }
              }
            }
            const auto* pin = cell->cellpin(output);
            if (!pin || pin->direction != ot::CellpinDirection::OUTPUT) {
              fail("sdc-unsupported", "set_driving_cell requires a characterized output pin");
            }
            bool characterized = false;
            for (const auto& arc : pin->timings) {
              if (arc.is_constraint() || (!drive_from.empty() && arc.related_pin != drive_from)) {
                continue;
              }
              for (auto input_tran : {ot::RISE, ot::FALL}) {
                characterized |= arc.delay(input_tran, tran, drive_slew[input_tran], 0).has_value()
                                 && arc.slew(input_tran, tran, drive_slew[input_tran], 0).has_value();
              }
            }
            if (!characterized) {
              fail("sdc-unsupported", "set_driving_cell output has no matching delay/slew arc");
            }
            timer.set_input_driver(pname, drive_cell, drive_from, output, split, tran, drive_slew);
          } else if (is_at) {
            timer.set_at(pname, split, tran, value);
          } else if (is_slew) {
            timer.set_slew(pname, split, tran, value);
          } else if (is_load) {
            if (value < 0) {
              fail("sdc-syntax", "set_load requires nonnegative capacitance");
            }
            if (!timer.primary_outputs().contains(pname)) {
              fail("sdc-unsupported", "set_load requires primary outputs");
            }
            timer.set_load(pname, split, tran, value);
          } else {
            if (split == ot::MAX) {
              output_max_constraints_[pname] |= static_cast<uint8_t>(1U << tran);
            }
            // Setup captures at the next rising edge; hold captures at this
            // edge. SDC output delay subtracts from the required arrival.
            const bool  resolved = timing_targets_.contains(reference_clock);
            const float required = resolved  ? (split == ot::MAX ? timing_targets_.at(reference_clock).period - value : -value)
                                   : clocked ? (split == ot::MAX ? virtual_clock_period_ - value : -value)
                                             : value;
            timer.set_rat(pname, split, tran, required);
          }
        }
      }
    }
  }
  for (const auto& name : false_start_ports_) {
    for (auto split : {ot::MIN, ot::MAX}) {
      for (auto tran : {ot::RISE, ot::FALL}) {
        timer.set_at(name, split, tran, std::nullopt);
      }
    }
  }
  for (const auto& name : false_end_ports_) {
    for (auto split : {ot::MIN, ot::MAX}) {
      for (auto tran : {ot::RISE, ot::FALL}) {
        timer.set_rat(name, split, tran, std::nullopt);
      }
    }
  }
}
