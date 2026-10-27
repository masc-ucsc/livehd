// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <set>

#include "absl/strings/str_split.h"
#include "hash_util.hpp"
#include "json_util.hpp"
#include "node_util.hpp"
#include "critical_identity.hpp"
#include "pass_opentimer.hpp"
#include "rapidjson/document.h"

namespace {
namespace gu = livehd::graph_util;
std::string esc(std::string_view value) { return livehd::json_util::escape(value); }
void        invalid(std::string_view code, const std::string& message) {
  livehd::diag::err("pass.opentimer", code, "io").msg("{}", message).fatal();
}
std::string identity(const hhds::Node_class& node) {
  return std::format("{}:{}", node.get_graph()->get_name(), node.get_debug_nid());
}
}  // namespace

void Pass_opentimer::parse_critical_options(const Eprp_var& var) {
  const std::string flag{var.get("critical", "false")};
  if (flag != "true" && flag != "false" && flag != "1" && flag != "0") {
    invalid("bad-option", "critical expects true or false");
  }
  critical_ = flag == "true" || flag == "1";
  const std::string percent{var.get("critical_percent", "5")};
  char*             end = nullptr;
  critical_percent_     = std::strtof(percent.c_str(), &end);
  if (end == percent.c_str() || *end || !std::isfinite(critical_percent_) || critical_percent_ < 0 || critical_percent_ > 100) {
    invalid("bad-option", "critical_percent must be a finite percentage between 0 and 100");
  }
  targets_text_ = var.get("targets", "");
  if (!targets_text_.empty()) {
    for (auto token : absl::StrSplit(targets_text_, ',')) {
      const std::string entry{token};
      const auto        colon = entry.find(':');
      if (colon == std::string::npos || colon == 0 || colon + 1 == entry.size()
          || !target_overrides_.emplace(entry.substr(0, colon), entry.substr(colon + 1)).second) {
        invalid("bad-option", "targets expects unique clock:period entries, e.g. clk:1ns,bus:4ns");
      }
    }
  }
}

float Pass_opentimer::target_period(std::string_view value) const {
  std::string text{value};
  char*       end    = nullptr;
  double      number = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || !std::isfinite(number) || number <= 0) {
    invalid("bad-option", std::format("invalid timing target '{}'", value));
  }
  const std::string_view suffix{end};
  if (!suffix.empty()) {
    double scale = suffix == "ps" ? 1e-12 : suffix == "ns" ? 1e-9 : suffix == "us" ? 1e-6 : suffix == "s" ? 1.0 : 0.0;
    if (scale == 0 || !timer.time_unit()) {
      invalid("bad-option", "target units must be ps, ns, us or s, with a declared Liberty time unit");
    }
    number *= scale / timer.time_unit()->value();
  }
  if (!std::isfinite(number) || number > std::numeric_limits<float>::max() || static_cast<float>(number) <= 0) {
    invalid("bad-option", "timing target is outside the supported positive finite range");
  }
  return static_cast<float>(number);
}

float Pass_opentimer::override_period(const std::string& name, const std::vector<std::string>& ports, float period) const {
  const std::string* override = nullptr;
  for (const auto& [key, value] : target_overrides_) {
    if (key == name || std::find(ports.begin(), ports.end(), key) != ports.end()) {
      if (override != nullptr) {
        invalid("bad-option", std::format("multiple overrides select clock '{}'", name));
      }
      override = &value;
    }
  }
  return override == nullptr ? period : target_period(*override);
}

void Pass_opentimer::prepare_critical_targets() {
  timer.update_netlist();
  for (const auto& [name, value] : target_overrides_) {
    bool found = false;
    for (const auto& [clock, target] : timing_targets_) {
      found |= clock == name || std::find(target.ports.begin(), target.ports.end(), name) != target.ports.end();
    }
    if (!found) {
      Timing_target target{target_period(value), {}};
      if (timer.primary_inputs().contains(name)) {
        target.ports.push_back(name);
      }
      timing_targets_.emplace(name, target);
      timer.create_clock(name, target.period);
    }
  }
  if (critical_ && timing_targets_.empty()) {
    invalid("critical-target-missing", "critical annotations require an SDC clock or pass.opentimer.targets (e.g. clk:1ns)");
  }
  if (!critical_ && target_overrides_.empty()) {
    return;
  }
  // Explicitly referenced output constraints already include their clock's
  // overridden period. Unconstrained outputs may use one unambiguous target.
  for (const auto& [pin, output] : timer.primary_outputs()) {
    (void)output;
    if (native_end_clocks_.contains(pin)) {
      continue;
    }
    if (!output_clocks_.contains(pin)) {
      if (timing_targets_.size() != 1) {
        invalid("critical-target-ambiguous", std::format("output '{}' needs an SDC set_output_delay -clock binding", pin));
      }
      output_clocks_[pin] = timing_targets_.begin()->first;
    }
    const auto target = timing_targets_.find(output_clocks_.at(pin));
    if (target == timing_targets_.end()) {
      invalid("critical-target-missing", std::format("output '{}' references SDC clock '{}', which is not defined", pin, output_clocks_.at(pin)));
      continue;
    }
    for (auto tran : {ot::RISE, ot::FALL}) {
      if ((output_max_constraints_[pin] & (1U << tran)) == 0) {
        timer.set_rat(pin, ot::MAX, tran, target->second.period);
      }
    }
  }
}

std::string Pass_opentimer::annotation_identity(const std::shared_ptr<hhds::Graph>& g) const {
  return critical_ ? livehd::opentimer::critical_identity(g) : std::string{};
}

std::string livehd::opentimer::critical_identity(const std::shared_ptr<hhds::Graph>& g) {
  // Canonical timing digests deliberately ignore local node ids/provenance.
  // Annotation replay additionally requires those identities to match.
  std::vector<std::string> nodes;
  for (const auto& def : g->definitions().graphs()) {
    for (auto n : def->body().nodes()) {
      std::string src;
      if (auto s = n.attr(hhds::attrs::srcid); s.has()) {
        auto span = def->source_locator().resolve_span(s.get());
        src       = std::format("{}:{}", span.file, span.start_line.value_or(0));
      }
      nodes.push_back(identity(n) + ":" + std::string{gu::node_name_of(n)} + ":" + src);
    }
  }
  std::sort(nodes.begin(), nodes.end());
  uint64_t hash = livehd::hash_util::kFnv1a64_offset;
  for (const auto& node : nodes) {
    hash = livehd::hash_util::fnv1a64(node, hash);
    hash = livehd::hash_util::fnv1a64("\n", hash);
  }
  return std::format("{:016x}", hash);
}

void Pass_opentimer::annotate_critical(const std::shared_ptr<hhds::Graph>& g) {
  (void)g;
  if (!critical_) {
    return;
  }
  timer.update_timing();
  if (hier_setting_ == "stitch") {
    invalid("critical-hierarchy", "critical annotations require hier=true or hier=false; legacy stitch mode is not supported");
  }
  if (opaque_logic_nodes_ != 0) {
    invalid("critical-coverage", "critical annotations require a fully timed netlist without opaque combinational boundaries");
  }
  auto clock_for_net = [&](const std::string& wire) -> std::string {
    std::string selected;
    for (const auto& [name, target] : timing_targets_) {
      for (const auto& port : target.ports) {
        auto pin = timer.pins().find(port);
        if (port == wire || (pin != timer.pins().end() && pin->second.net() && pin->second.net()->name() == wire)) {
          if (!selected.empty() && selected != name) {
            invalid("critical-clock-ambiguous", std::format("multiple targets bind clock net '{}'", wire));
          }
          selected = name;
        }
      }
    }
    if (!selected.empty()) {
      return selected;
    }
    if (timing_targets_.size() == 1 && timing_targets_.begin()->second.ports.empty()) {
      return timing_targets_.begin()->first;
    }
    if (timing_targets_.size() == 1) {
      // A clock-gate or other cell whose timing arc the timer does not carry: with one
      // target there is only one clock it can be derived from.
      livehd::diag::warn("pass.opentimer", "critical-clock-assumed", "io")
          .msg("clock net '{}' has no traceable source; assuming target '{}'", wire, timing_targets_.begin()->first);
      return timing_targets_.begin()->first;
    }
    std::string known;
    for (const auto& [name, target] : timing_targets_) {
      known += std::format(" {}[{}]", name, target.ports.size());
      for (const auto& port : target.ports) {
        known += " " + port;
      }
    }
    invalid("critical-clock-unresolved",
            std::format("no timing target binds clock net '{}' (known targets:{})", wire, known));
    return {};
  };
  // First combinational fanin of each pin, and the driver of each net: a clock
  // pin is traced back through buffer trees and clock gates to the pin that
  // names a target port. One resolver serves launch, capture and native state.
  std::map<const ot::Pin*, const ot::Pin*> upstream;
  std::map<std::string, const ot::Pin*>    net_driver;
  for (const auto& feed : timer.arcs()) {
    const auto fv = feed.timing_view();
    if (!fv[ot::MAX] || !fv[ot::MAX]->is_constraint()) {
      upstream.emplace(&feed.to(), &feed.from());
    }
    if (feed.from().net() != nullptr && feed.from().net() == feed.to().net()) {
      net_driver.emplace(feed.from().net()->name(), &feed.from());
    }
  }
  std::set<std::string> port_names;
  for (const auto& [tname, target] : timing_targets_) {
    port_names.insert(target.ports.begin(), target.ports.end());
  }
  std::map<const ot::Pin*, std::string> pin_clock;
  auto clock_of_pin = [&](const ot::Pin* start) -> std::string {
    if (auto it = pin_clock.find(start); it != pin_clock.end()) {
      return it->second;
    }
    std::string wire = start->net() ? start->net()->name() : start->name();
    for (const ot::Pin* up = start; up != nullptr; ) {
      if (port_names.contains(up->name())) {
        wire = up->name();
        break;
      }
      auto it = upstream.find(up);
      up      = it == upstream.end() ? nullptr : it->second;
      if (up == start) {
        break;
      }
    }
    return pin_clock[start] = clock_for_net(wire);
  };
  auto clock_of_wire = [&](const std::string& wire) -> std::string {
    auto it = net_driver.find(wire);
    return it == net_driver.end() ? clock_for_net(wire) : clock_of_pin(it->second);
  };
  for (const auto& [pin, wire] : native_end_clocks_) {
    if (physical_clock_endpoints_.contains(pin)) {
      continue;  // a memory clock pin is an RC sink, not a capture
    }
    output_clocks_[pin] = clock_of_wire(wire);
    for (auto tran : {ot::RISE, ot::FALL}) {
      timer.set_rat(pin, ot::MAX, tran, timing_targets_.at(output_clocks_[pin]).period);
    }
  }
  for (const auto& [pin, wire] : native_start_clocks_) {
    input_clocks_[pin] = clock_of_wire(wire);
  }
  timer.update_timing();

  // Library state launches at Q, while native state launches at a synthetic
  // input. Both must participate in clock-domain validation.
  std::set<const ot::Gate*> sequential_gates;
  std::set<const ot::Pin*>  clock_pins, setup_pins;
  const auto non_seq = [](const ot::Test& test) {
    const auto view = test.arc().timing_view();
    const auto type = view[ot::MAX] ? view[ot::MAX]->type : std::nullopt;
    return type && (*type == ot::TimingType::NON_SEQ_SETUP_RISING || *type == ot::TimingType::NON_SEQ_SETUP_FALLING);
  };
  const auto                setup_constraint = [](const ot::Test& test, ot::Tran tran) -> std::optional<float> {
    const auto view = test.arc().timing_view();
    if (!view[ot::MAX]) {
      return {};
    }
    const auto edge       = view[ot::MAX]->is_rising_edge_triggered() ? ot::RISE : ot::FALL;
    const auto clock_slew = test.related_pin().slew(ot::MIN, edge);
    const auto data_slew  = test.constrained_pin().slew(ot::MAX, tran);
    // Clock creation after circuit materialization need not invalidate OT's
    // cached Test::constraint. Evaluate the Liberty table with current slews.
    return clock_slew && data_slew ? view[ot::MAX]->constraint(edge, tran, *clock_slew, *data_slew) : std::nullopt;
  };
  for (const auto& arc : timer.arcs()) {
    const auto view = arc.timing_view();
    if (view[ot::MAX] && !view[ot::MAX]->is_constraint()
        && (view[ot::MAX]->is_rising_edge_triggered() || view[ot::MAX]->is_falling_edge_triggered())) {
      input_clocks_[arc.to().name()] = clock_of_pin(&arc.from());
      sequential_gates.insert(arc.to().gate());
      clock_pins.insert(&arc.from());
    }
  }
  for (const auto& test : timer.tests()) {
    const auto view = test.arc().timing_view();
    if (!view[ot::MAX]) {
      continue;
    }
    if (non_seq(test)) {
      continue;  // between two asynchronous pins of one cell: not launched by a clock edge
    }
    setup_pins.insert(&test.constrained_pin());
    const auto edge = view[ot::MAX]->is_rising_edge_triggered() ? ot::RISE : ot::FALL;
    // A recovery check (async set/reset release) is characterized for one data
    // transition only; every other check must constrain each arriving transition.
    const auto type     = view[ot::MAX]->type;
    const bool recovery = type && (*type == ot::TimingType::RECOVERY_RISING || *type == ot::TimingType::RECOVERY_FALLING);
    bool       arrives = false, constrained = false, gap = false;
    for (auto tran : {ot::RISE, ot::FALL}) {
      if (test.constrained_pin().at(ot::MAX, tran)) {
        arrives       = true;
        const bool ok = setup_constraint(test, tran) && test.related_pin().at(ot::MIN, edge);
        constrained   = constrained || ok;
        gap           = gap || !ok;
      }
    }
    if (arrives && (!constrained || (gap && !recovery))) {
      invalid("critical-coverage", std::format("missing setup timing at '{}'", test.constrained_pin().name()));
    }
  }
  for (const auto& [name, pin] : timer.pins()) {
    auto cp = pin.cellpin(ot::MAX);
    if (pin.gate() && sequential_gates.contains(pin.gate()) && cp && cp->direction == ot::CellpinDirection::INPUT
        && !clock_pins.contains(&pin) && (pin.at(ot::MAX, ot::RISE) || pin.at(ot::MAX, ot::FALL)) && !setup_pins.contains(&pin)) {
      invalid("critical-coverage", std::format("missing setup constraint at '{}'", name));
    }
  }

  size_t count = 0;
  for (const auto& [name, pin] : timer.pins()) {
    (void)name;
    count = std::max(count, pin.idx() + 1);
  }
  std::vector<const ot::Pin*>              pins(count, nullptr);
  std::vector<std::vector<const ot::Arc*>> incoming(count);
  std::vector<size_t>                      successors(count, 0);
  for (const auto& [name, pin] : timer.pins()) {
    (void)name;
    pins[pin.idx()] = &pin;
  }
  for (const auto& arc : timer.arcs()) {
    // Test arcs constrain endpoints; clock pins terminate the backward data
    // walk. Never propagate a capture constraint through a sequential cycle.
    const auto view = arc.timing_view();
    const bool test = (view[ot::MAX] && view[ot::MAX]->is_constraint()) || (view[ot::MIN] && view[ot::MIN]->is_constraint());
    if (test || arc.is_loop_breaker() || arc.to().is_datapath_source()) {
      continue;
    }
    incoming[arc.to().idx()].push_back(&arc);
    ++successors[arc.from().idx()];
  }
  std::vector<size_t> reverse;
  for (size_t i = 0; i < count; ++i) {
    if (pins[i] && successors[i] == 0) {
      reverse.push_back(i);
    }
  }
  for (size_t k = 0; k < reverse.size(); ++k) {
    for (const auto* arc : incoming[reverse[k]]) {
      if (--successors[arc->from().idx()] == 0) {
        reverse.push_back(arc->from().idx());
      }
    }
  }
  if (reverse.size() != timer.pins().size()) {
    invalid("critical-cycle", "critical annotation encountered a cyclic timing graph");
  }
  struct Evidence {
    hhds::Node_class         node;
    std::vector<std::string> rows;
    bool                     critical = false;
  };
  std::map<std::string, Evidence> evidence;
  std::string                     targets_json;
  const float                     inf = std::numeric_limits<float>::infinity();
  for (const auto& [clock, target] : timing_targets_) {
    std::vector<std::array<float, 2>> required(count, {inf, inf});
    size_t                            endpoints = 0;
    float                             worst     = inf;
    auto                              seed      = [&](const ot::Pin& pin, ot::Tran tran, float rat) {
      required[pin.idx()][tran] = std::min(required[pin.idx()][tran], rat);
      if (auto at = pin.at(ot::MAX, tran); at) {
        worst = std::min(worst, rat - *at);
      }
      ++endpoints;
    };
    for (const auto& [name, output] : timer.primary_outputs()) {
      if (auto group = output_clocks_.find(name); group != output_clocks_.end() && group->second == clock) {
        const auto& pin = timer.pins().at(name);
        for (auto tran : {ot::RISE, ot::FALL}) {
          if (auto rat = output.rat(ot::MAX, tran); rat) {
            seed(pin, tran, *rat);
          }
        }
      }
    }
    for (const auto& test : timer.tests()) {
      if (non_seq(test) || test.related_pin().net() == nullptr || clock_of_pin(&test.related_pin()) != clock) {
        continue;
      }
      const auto view = test.arc().timing_view();
      if (!view[ot::MAX]) {
        continue;
      }
      const auto edge = view[ot::MAX]->is_rising_edge_triggered() ? ot::RISE : ot::FALL;
      for (auto tran : {ot::RISE, ot::FALL}) {
        const auto setup   = setup_constraint(test, tran);
        const auto capture = test.related_pin().at(ot::MIN, edge);
        if (setup && capture) {
          seed(test.constrained_pin(), tran, *capture + target.period - *setup);
        }
      }
    }
    if (endpoints == 0) {
      invalid("critical-target-unused", std::format("clock '{}' has no constrained timing endpoints", clock));
    }
    // Latest permissible arrival propagated over every valid transition arc.
    // This includes all near-critical branches, even those not on the single
    // worst path returned by report_timing. No path enumeration is needed.
    for (size_t i : reverse) {
      for (const auto* arc : incoming[i]) {
        for (auto from : {ot::RISE, ot::FALL}) {
          for (auto to : {ot::RISE, ot::FALL}) {
            if (auto delay = arc->delay(ot::MAX, from, to); delay && std::isfinite(required[i][to])) {
              auto& rat = required[arc->from().idx()][from];
              rat       = std::min(rat, required[i][to] - *delay);
            }
          }
        }
      }
    }
    struct Node_slack {
      hhds::Node_class node;
      float            slack = std::numeric_limits<float>::infinity();
      std::string      pin;
    };
    std::map<std::string, Node_slack> rows;
    size_t                            unmapped_flat_nodes = 0;
    auto                              consider = [&](hhds::Node_class node, float slack, const std::string& pin) {
      while (timing_origins_.contains(timing_node_key(node))) {
        node = timing_origins_.at(timing_node_key(node));
      }
      if (gu::type_op_of(node) == Ntype_op::IO) {
        return;  // graph boundary sentinels are not cells
      }
      if (node.get_graph()->get_name().ends_with("__ot_flat_tmp")) {
        ++unmapped_flat_nodes;  // created by flattening itself: no source node to annotate
        return;
      }
      auto& row = rows[identity(node)];
      row.node  = node;
      if (slack < row.slack || (slack == row.slack && pin < row.pin)) {
        row.slack = slack;
        row.pin   = pin;
      }
    };
    for (const auto* pin : pins) {
      if (pin == nullptr) {
        continue;
      }
      float slack = inf;
      for (auto tran : {ot::RISE, ot::FALL}) {
        if (auto at = pin->at(ot::MAX, tran); at) {
          slack = std::min(slack, required[pin->idx()][tran] - *at);
        }
      }
      if (!std::isfinite(slack)) {
        continue;
      }
      if (auto launch = input_clocks_.find(pin->name()); launch != input_clocks_.end() && launch->second != clock) {
        invalid("critical-clock-crossing",
                std::format("path from clock '{}' to '{}' needs a supported clock relationship", launch->second, clock));
      }
      if (auto cp = pin->cellpin(ot::MAX); cp && cp->is_clock.value_or(false)) {
        continue;
      }
      if (auto n = timing_pin_nodes_.find(pin->name()); n != timing_pin_nodes_.end()) {
        consider(n->second, slack, pin->name());
      }
      if (pin->net() && pin->is_rct_root()) {
        if (auto nodes = timing_net_nodes_.find(pin->net()->name()); nodes != timing_net_nodes_.end()) {
          for (auto node : nodes->second) {
            consider(node, slack, pin->name());
          }
        }
      }
    }
    if (unmapped_flat_nodes != 0) {
      livehd::diag::warn("pass.opentimer", "critical-unmapped", "io")
          .msg("{} flattening-created cells have no source node and carry no timing annotation", unmapped_flat_nodes);
    }
    const float       limit        = target.period * critical_percent_ / 100.0F;
    const double      unit_seconds = timer.time_unit() ? timer.time_unit()->value() : 0;
    const std::string unit         = std::abs(unit_seconds - 1e-9) < 1e-15    ? "ns"
                                     : std::abs(unit_seconds - 1e-12) < 1e-18 ? "ps"
                                     : std::abs(unit_seconds - 1e-6) < 1e-12  ? "us"
                                                                              : "library";
    for (const auto& [key, row] : rows) {
      auto& dst          = evidence[key];
      dst.node           = row.node;
      const bool marked  = row.slack <= limit + target.period * 1e-6F;
      dst.critical      |= marked;
      dst.rows.push_back(std::format(R"({{"clock":"{}","period":{:.9g},"slack":{:.9g},"critical":{},"pin":"{}","unit":"{}"}})",
                                     esc(clock),
                                     target.period,
                                     row.slack,
                                     marked,
                                     esc(row.pin),
                                     unit));
    }
    if (!targets_json.empty()) {
      targets_json += ',';
    }
    targets_json += std::format(R"({{"clock":"{}","period":{:.9g},"slack":{}}})",
                                esc(clock),
                                target.period,
                                std::isfinite(worst) ? std::format("{:.9g}", worst) : "null");
  }
  annotation_records_ = "[";
  size_t marked       = 0;
  for (const auto& [key, record] : evidence) {
    (void)key;
    std::string data = "[";
    for (const auto& row : record.rows) {
      if (data.size() != 1) {
        data += ',';
      }
      data += row;
    }
    data += ']';
    if (annotation_records_.size() != 1) {
      annotation_records_ += ',';
    }
    annotation_records_ += std::format(R"({{"module":"{}","nid":{},"critical":{},"data":"{}"}})",
                                       esc(record.node.get_graph()->get_name()),
                                       record.node.get_debug_nid(),
                                       record.critical,
                                       esc(data));
    marked              += record.critical;
  }
  annotation_records_ += ']';
  annotation_summary_  = std::format(R"({{"schema_version":1,"margin_percent":{:.9g},"nodes":{},"marked":{},"targets":[{}]}})",
                                     critical_percent_,
                                     evidence.size(),
                                     marked,
                                     targets_json);
}

bool Pass_opentimer::apply_annotations(const std::shared_ptr<hhds::Graph>& g, std::string_view records, std::string_view summary) {
  rapidjson::Document doc;
  doc.Parse(records.data(), records.size());
  if (doc.HasParseError() || !doc.IsArray() || summary.empty()) {
    return false;
  }
  std::map<std::string, hhds::Node_class> nodes;
  const auto                              definitions = g->definitions().graphs();
  for (const auto& def : definitions) {
    for (auto n : def->body().nodes()) {
      nodes.emplace(identity(n), n);
    }
  }
  for (const auto& row : doc.GetArray()) {
    if (!row.IsObject() || !row.HasMember("module") || !row["module"].IsString() || !row.HasMember("nid") || !row["nid"].IsUint64()
        || !row.HasMember("critical") || !row["critical"].IsBool() || !row.HasMember("data") || !row["data"].IsString()) {
      return false;
    }
    if (const auto key = std::format("{}:{}", row["module"].GetString(), row["nid"].GetUint64()); !nodes.contains(key)) {
      // Name the first annotation whose owning node is outside the annotated top's definitions.
      livehd::diag::err("pass.opentimer", "critical-identity", "internal")
          .msg("timing annotation for '{}' has no node in the annotated top '{}' hierarchy", key, g->get_name())
          .fatal();
      return false;
    }
  }
  for (const auto& [key, node] : nodes) {
    (void)key;
    node.attr(livehd::attrs::critical).del();
    node.attr(livehd::attrs::sta).del();
  }
  for (const auto& row : doc.GetArray()) {
    const auto node = nodes.at(std::format("{}:{}", row["module"].GetString(), row["nid"].GetUint64()));
    node.attr(livehd::attrs::sta).set(std::string{row["data"].GetString()});
    if (row["critical"].GetBool()) {
      node.attr(livehd::attrs::critical).set();
    }
  }
  // The summary carries the identity it was computed for: a later edit of the
  // graph (or a copy of another run's library) shows up as a mismatch.
  std::string stamped{summary};
  if (!stamped.empty() && stamped.back() == '}') {
    stamped.pop_back();
    stamped += std::format(R"(,"identity":"{}"}})", livehd::opentimer::critical_identity(g));
  }
  g->get_input_node().attr(livehd::attrs::sta).set(stamped);
  return true;
}
