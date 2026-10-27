// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Synthetic, repeatable read-only request workload over a supplied mapped DEF.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>

#include "inou/lefdef/routing_geometry.hpp"
#include "pass/place/pin_escape_guard.hpp"
#include "pass/place/signal_router.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace {
namespace ph = livehd::physical;
namespace pl = livehd::placement;
using Clock  = std::chrono::steady_clock;
double  seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
int64_t timestamp() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
template <class Fn>
void emit(Fn fn) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  fn(w);
  w.EndObject();
  std::cout << buffer.GetString() << '\n' << std::flush;
}
double quantile(std::vector<double> values, double fraction) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  return values[static_cast<size_t>(fraction * (values.size() - 1))];
}
}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> lefs;
    std::string              def, guard = "shared";
    size_t                   samples = 200, seed = 1, access = 10000, patterns = 10000, budget_ms = 200;
    size_t                   min_sinks = 1, max_sinks = std::numeric_limits<size_t>::max();
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (++i == argc) {
        throw std::invalid_argument("missing argument value");
      }
      const std::string value = argv[i];
      if (key == "--lef") {
        lefs.push_back(value);
      } else if (key == "--def") {
        def = value;
      } else if (key == "--guard") {
        guard = value;
      } else {
        size_t     n      = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), n);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || (!n && key != "--seed")) {
          throw std::invalid_argument("invalid unsigned argument: " + key);
        }
        if (key == "--samples") {
          samples = n;
        } else if (key == "--seed") {
          seed = n;
        } else if (key == "--min-sinks") {
          min_sinks = n;
        } else if (key == "--max-sinks") {
          max_sinks = n;
        } else if (key == "--access") {
          access = n;
        } else if (key == "--patterns") {
          patterns = n;
        } else if (key == "--budget-ms" && n <= 3600000) {
          budget_ms = n;
        } else {
          throw std::invalid_argument("unknown argument or invalid budget: " + key);
        }
      }
    }
    if (def.empty() || lefs.empty() || (guard != "shared" && guard != "request")) {
      throw std::invalid_argument("requires --def and --lef; --guard is shared or request");
    }
    const auto start        = Clock::now();
    const auto tech         = ph::read_lef(lefs);
    const auto frame        = ph::read_def(def);
    const auto read_seconds = seconds(start);
    std::cerr << "loaded " << frame.components.size() << " cells, " << frame.nets.size() << " nets\n";
    const ph::Route_components         components(frame);
    std::map<std::string, std::string> ports;
    for (const auto& port : frame.terminals) {
      ports.emplace(port.name, port.direction);
    }
    struct Target {
      size_t      net;
      std::string driver;
    };
    std::vector<Target> targets;
    for (size_t n = 0; n < frame.nets.size(); ++n) {
      const auto& net = frame.nets[n];
      if (net.connections.size() < 2 || net.connections.size() - 1 < min_sinks || net.connections.size() - 1 > max_sinks) {
        continue;
      }
      std::vector<std::string> drivers;
      for (const auto& pin : net.connections) {
        bool drives = false;
        if (pin.instance == "PIN") {
          const auto p = ports.find(pin.pin);
          drives       = p != ports.end() && p->second == "INPUT";
        } else if (const auto cell = components.find(pin.instance); cell != components.end()) {
          const auto master = tech.macros.find(cell->second->macro);
          if (master != tech.macros.end()) {
            const auto p = master->second.pins.find(pin.pin);
            drives       = p != master->second.pins.end() && p->second.direction == "OUTPUT";
          }
        }
        if (drives) {
          drivers.push_back(ph::connection_label(pin));
        }
      }
      if (drivers.size() == 1) {
        targets.push_back({n, std::move(drivers.front())});
      }
    }
    if (targets.empty()) {
      throw std::runtime_error("no single-driver nets with sinks");
    }
    auto       phase             = Clock::now();
    const auto occupancy         = ph::routing_occupancy(tech, frame, frame.dbu);
    const auto occupancy_seconds = seconds(phase);
    phase                        = Clock::now();
    const ph::Signal_metal_rules rules(tech, frame);
    const auto                   profile       = pl::signal_route_profile(ph::resolve_tech_profile(tech), tech);
    const auto                   rules_seconds = seconds(phase);
    phase                                      = Clock::now();
    std::unique_ptr<pl::Pin_escape_guard> escapes;
    if (guard == "shared") {
      const auto deadline = Clock::now() + std::chrono::minutes(5);
      escapes             = std::make_unique<pl::Pin_escape_guard>(
          tech,
          frame,
          occupancy,
          access,
          [&] {
            if (Clock::now() >= deadline) {
              throw std::runtime_error("shared guard setup exceeded five minutes");
            }
          },
          profile.access,
          &rules);
    }
    const auto        guard_seconds = seconds(phase);
    pl::Signal_router router(tech, frame, occupancy, rules, profile, escapes.get());
    emit([&](auto& w) {
      w.Key("type");
      w.String("setup");
      w.Key("def");
      w.String(def.c_str());
      w.Key("cells");
      w.Uint64(frame.components.size());
      w.Key("nets");
      w.Uint64(frame.nets.size());
      w.Key("eligible_driver_pins");
      w.Uint64(targets.size());
      w.Key("min_sinks");
      w.Uint64(min_sinks);
      w.Key("max_sinks");
      w.Uint64(max_sinks);
      w.Key("samples");
      w.Uint64(samples);
      w.Key("seed");
      w.Uint64(seed);
      w.Key("access_budget");
      w.Uint64(access);
      w.Key("pattern_budget");
      w.Uint64(patterns);
      w.Key("budget_ms");
      w.Uint64(budget_ms);
      w.Key("guard");
      w.String(guard.c_str());
      w.Key("read_seconds");
      w.Double(read_seconds);
      w.Key("occupancy_seconds");
      w.Double(occupancy_seconds);
      w.Key("rules_seconds");
      w.Double(rules_seconds);
      w.Key("guard_seconds");
      w.Double(guard_seconds);
      w.Key("setup_seconds");
      w.Double(seconds(start));
      w.Key("occupancy_shapes");
      w.Uint64(occupancy.size());
      w.Key("protected_pins");
      w.Uint64(escapes ? escapes->protected_pins() : 0);
      w.Key("unresolved_pins");
      w.Uint64(escapes ? escapes->unresolved_pins() : 0);
      w.Key("route_start_ns");
      w.Int64(timestamp());
    });
    std::mt19937_64               random(seed);
    // Rejection sampling avoids modulo bias and keeps the sequence independent
    // of a standard library's uniform_int_distribution implementation.
    const uint64_t                threshold = -uint64_t(targets.size()) % targets.size();
    std::vector<double>           all, successful, failed;
    size_t                        timeouts = 0;
    double                        total    = 0;
    std::map<std::string, size_t> reasons;
    for (size_t i = 0; i < samples; ++i) {
      uint64_t draw;
      do {
        draw = random();
      } while (draw < threshold);
      const auto& target   = targets[draw % targets.size()];
      const auto& net      = frame.nets[target.net];
      const auto  before   = Clock::now();
      const auto  result   = router.try_route(net,
                                              {
                                                  {access, patterns, before + std::chrono::milliseconds(budget_ms)},
                                                  {},
                                                  false
      });
      const auto  elapsed  = seconds(before);
      total               += elapsed;
      all.push_back(elapsed);
      const bool success = !result.candidates.empty();
      (success ? successful : failed).push_back(elapsed);
      timeouts += result.timed_out;
      if (!success) {
        ++reasons[result.reason];
      }
      // Independent complete-net connectivity check is deliberately outside
      // the measured API call; neither proposals nor reservations are committed.
      for (const auto& candidate : result.candidates) {
        auto routed = net;
        routed.wires.push_back({"FIXED", candidate.paths});
        const auto checked = ph::check_route_connectivity(tech, frame, routed, &components, &candidate.nondefault_rules);
        if (!checked.connected) {
          throw std::runtime_error("returned disconnected route: " + checked.reason);
        }
      }
      emit([&](auto& w) {
        w.Key("type");
        w.String("attempt");
        w.Key("sample");
        w.Uint64(i);
        w.Key("net");
        w.String(net.name.c_str());
        w.Key("driver");
        w.String(target.driver.c_str());
        w.Key("pins");
        w.Uint64(net.connections.size());
        w.Key("seconds");
        w.Double(elapsed);
        w.Key("success");
        w.Bool(success);
        w.Key("timed_out");
        w.Bool(result.timed_out);
        w.Key("reason");
        w.String(result.reason.c_str());
        w.Key("access_examined");
        w.Uint64(result.access_examined);
        w.Key("patterns_examined");
        w.Uint64(result.patterns_examined);
        w.Key("geometry");
        w.StartArray();
        for (const auto& candidate : result.candidates) {
          ph::Net routed;
          routed.name = net.name;
          routed.wires.push_back({"FIXED", candidate.paths});
          w.StartObject();
          w.Key("wire_um");
          w.Double(candidate.wire_length_um);
          w.Key("vias");
          w.Uint64(candidate.vias);
          w.Key("exposure");
          w.Double(candidate.parallel_exposure);
          w.Key("shapes");
          w.StartArray();
          for (const auto& shape : ph::route_geometry(tech, frame, routed, false, &candidate.nondefault_rules)) {
            w.StartArray();
            w.String(shape.layer.c_str());
            for (auto v : {shape.rect.xl, shape.rect.yl, shape.rect.xh, shape.rect.yh}) {
              w.Double(v);
            }
            w.EndArray();
          }
          w.EndArray();
          w.EndObject();
        }
        w.EndArray();
      });
    }
    emit([&](auto& w) {
      w.Key("type");
      w.String("summary");
      w.Key("route_end_ns");
      w.Int64(timestamp());
      w.Key("attempts");
      w.Uint64(samples);
      w.Key("successes");
      w.Uint64(successful.size());
      w.Key("failures");
      w.Uint64(failed.size());
      w.Key("timeouts");
      w.Uint64(timeouts);
      w.Key("route_seconds");
      w.Double(total);
      w.Key("attempts_per_second");
      w.Double(samples / total);
      w.Key("successful_routes_per_second");
      w.Double(successful.size() / total);
      w.Key("failed_attempts_per_second");
      w.Double(failed.size() / total);
      for (const auto& [name, values] : {
               std::pair{    "all",        &all},
               {"success", &successful},
               {"failure",     &failed}
      }) {
        w.Key(name);
        w.StartObject();
        w.Key("p50_ms");
        w.Double(1000 * quantile(*values, .50));
        w.Key("p95_ms");
        w.Double(1000 * quantile(*values, .95));
        w.Key("p99_ms");
        w.Double(1000 * quantile(*values, .99));
        w.EndObject();
      }
      w.Key("failure_reasons");
      w.StartObject();
      for (const auto& [reason, count] : reasons) {
        w.Key(reason.c_str());
        w.Uint64(count);
      }
      w.EndObject();
    });
  } catch (const std::exception& e) {
    std::cerr << "signal_route_bench: " << e.what() << '\n';
    return 1;
  }
}
