// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>

#include "late_timing.hpp"

// JSON fragments of a native timing observation for any rapidjson-style
// Writer. Times are ps; a missing value is null. Callers own the enclosing
// document layout, so diagnostics and probes keep their own key order.
namespace livehd::placement::late::json {
template <class Writer>
void write(Writer& writer, std::optional<double> value) {
  if (value) {
    writer.Double(*value);
  } else {
    writer.Null();
  }
}
// A predecessor is written as [pin index, transition] or {"pin": name, "transition": t}.
enum class Predecessor_style { index, name };
template <class Writer>
void write(Writer& writer, const std::optional<Predecessor>& predecessor, const Result& result, Predecessor_style style) {
  if (!predecessor) {
    writer.Null();
  } else if (style == Predecessor_style::index) {
    writer.StartArray();
    writer.Uint64(predecessor->pin);
    writer.Uint(predecessor->transition);
    writer.EndArray();
  } else {
    writer.StartObject();
    writer.Key("pin");
    writer.String(result.pins[predecessor->pin].name.c_str());
    writer.Key("transition");
    writer.Uint(predecessor->transition);
    writer.EndObject();
  }
}
// Aggregate pin transition: arrival, required, slew, load and predecessor.
template <class Writer>
void write(Writer& writer, const State& state, const Result& result, Predecessor_style style) {
  writer.StartObject();
  for (const auto& [name, quantity] : {
           std::pair{ "arrival_ps",  state.arrival_ps},
           {"required_ps", state.required_ps},
           {    "slew_ps",     state.slew_ps},
           {    "load_ff",     state.load_ff}
  }) {
    writer.Key(name);
    write(writer, quantity);
  }
  writer.Key("predecessor");
  write(writer, state.predecessor, result, style);
  writer.EndObject();
}
// One launch domain's transition: arrival, required and predecessor.
template <class Writer>
void write(Writer& writer, const Result::Domain_state& state, const Result& result, Predecessor_style style) {
  writer.StartObject();
  writer.Key("arrival_ps");
  write(writer, state.arrival_ps);
  writer.Key("required_ps");
  write(writer, state.required_ps);
  writer.Key("predecessor");
  write(writer, state.predecessor, result, style);
  writer.EndObject();
}
// Rise and fall states as a two-element array.
template <class Writer, class Transitions>
void write_transitions(Writer& writer, const Transitions& transitions, const Result& result, Predecessor_style style) {
  writer.StartArray();
  for (const auto& state : transitions) {
    write(writer, state, result, style);
  }
  writer.EndArray();
}
// Clock summaries keyed by name. The compact form keeps only the period and
// timed endpoints; the full form adds slack, achievable period and independence.
template <class Writer>
void write_clocks(Writer& writer, const Result& result, bool full) {
  writer.StartObject();
  for (const auto& [name, clock] : result.clocks) {
    writer.Key(name.c_str());
    writer.StartObject();
    writer.Key("period_ps");
    writer.Double(clock.period_ps);
    if (full) {
      writer.Key("wns_ps");
      write(writer, clock.wns_ps);
      writer.Key("tns_ps");
      write(writer, clock.tns_ps);
      writer.Key("achievable_period_ps");
      write(writer, clock.achievable_period_ps);
      writer.Key("independent_period");
      writer.Bool(clock.independent_period);
    }
    writer.Key("timed_endpoints");
    writer.Uint64(clock.timed_endpoints);
    writer.EndObject();
  }
  writer.EndObject();
}
}  // namespace livehd::placement::late::json
