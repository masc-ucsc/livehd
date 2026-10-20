// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "resize_engine.hpp"

#include <fnmatch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair_internal.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace {
using namespace detail;
namespace gu          = graph_util;
namespace fs          = std::filesystem;
using Clock           = std::chrono::steady_clock;
using Whatif          = livehd::timing::Whatif;
constexpr double kInf = std::numeric_limits<double>::infinity();

double      elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
std::string instance_of(const std::string& label) {
  const auto colon = label.rfind(':');
  return colon == std::string::npos ? std::string{} : label.substr(0, colon);
}
std::string pin_of(const std::string& label) {
  const auto colon = label.rfind(':');
  return colon == std::string::npos ? label : label.substr(colon + 1);
}
double      manhattan(Point a, Point b) { return std::abs(a.first - b.first) + std::abs(a.second - b.second); }
Point       lerp(Point a, Point b, double f) { return {a.first + (b.first - a.first) * f, a.second + (b.second - a.second) * f}; }
std::string json_string(const std::string& text) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.String(text.c_str(), text.size());
  return buffer.GetString();
}

// ---- Liberty/LEF view shared by planning and materialization ---------------
struct Library {
  physical::Technology                                                 tech;
  Inventory                                                            cells;
  Special_pins                                                         special;
  std::map<std::pair<std::string, std::string>, liberty::Output_drive> drives;   // (master, output pin)
  std::map<std::string, std::vector<const Buffer*>>                    ladders;  // same function/footprint, ascending area
  std::vector<const Buffer*>                                           buffers;  // repeater candidates, ascending area
  Wire_rc                                                              wire;
  double                                                               r_dbu = 0, c_dbu = 0;  // ohm and fF per DBU
};
bool matches(const std::string& name, const std::vector<std::string>& patterns) {
  return std::any_of(patterns.begin(), patterns.end(), [&](const auto& p) { return fnmatch(p.c_str(), name.c_str(), 0) == 0; });
}
std::shared_ptr<const Library> build_library(const Options& options) {
  auto lib     = std::make_shared<Library>();
  lib->tech    = physical::read_lef(options.lefs);
  lib->cells   = inventory(options, lib->tech);
  lib->special = scan_special_pins(options.liberty, !options.repair_reset);
  for (auto& d : liberty::scan_output_drives(options.liberty)) {
    lib->drives.emplace(std::pair{d.name, d.output}, d);
  }
  // Threshold-voltage families: with several Liberty files and no vt_swap a
  // ladder stays inside the file of the current master (same VT library).
  std::map<std::string, unsigned> family;
  if (!options.resize_vt_swap && options.liberty.find(',') != std::string::npos) {
    std::stringstream files(options.liberty);
    std::string       file;
    unsigned          index = 0;
    while (std::getline(files, file, ',')) {
      if (file.ends_with(".lib") || file.ends_with(".lib.gz") || file.find(".lib") != std::string::npos) {
        for (const auto& c : liberty::scan_comb_cells(file)) {
          family.emplace(c.name, index);
        }
      }
      ++index;
    }
  }
  const auto same_family = [&](const std::string& a, const std::string& b) {
    const auto x = family.find(a), y = family.find(b);
    return x == family.end() || y == family.end() || x->second == y->second;
  };
  std::map<std::string, std::optional<uint64_t>> truth;
  for (const auto& [name, c] : lib->cells) {
    truth.emplace(name, liberty::cell_truth(c));
  }
  const auto height = [&](const std::string& name) { return lib->tech.macros.at(name).height; };
  for (const auto& [master, current] : lib->cells) {
    auto& ladder = lib->ladders[master];
    for (const auto& [name, c] : lib->cells) {
      if (c.inputs != current.inputs || c.output != current.output || c.footprint != current.footprint
          || truth.at(name) != truth.at(master) || std::abs(height(name) - height(master)) > 1e-9 || !same_family(name, master)
          || (name != master && matches(name, options.resize_dont_use))) {
        continue;
      }
      ladder.push_back(&c);
    }
    std::sort(ladder.begin(), ladder.end(), [](const auto* a, const auto* b) {
      return std::tie(a->area, a->name) < std::tie(b->area, b->name);
    });
  }
  double fastest = kInf;
  for (const auto& [name, c] : lib->cells) {
    if (identity(c) && c.intrinsic_ps > 0) {
      fastest = std::min(fastest, c.intrinsic_ps);
    }
  }
  for (const auto& [name, c] : lib->cells) {
    // Delay/hold cells are identities too; a repeater must be a real driver.
    if (!identity(c) || matches(name, options.resize_dont_use) || (c.max_fanout && *c.max_fanout < 2)
        || (std::isfinite(fastest) && c.intrinsic_ps > 2.5 * fastest)) {
      continue;
    }
    lib->buffers.push_back(&c);
  }
  std::sort(lib->buffers.begin(), lib->buffers.end(), [](const auto* a, const auto* b) {
    return std::tie(a->area, a->name) < std::tie(b->area, b->name);
  });
  lib->wire
      = technology_wire_rc(lib->tech, options.liberty, options.tech_rc_file, options.resistance_ohm_um, options.capacitance_ff_um);
  lib->r_dbu = (lib->wire.horizontal_r + lib->wire.vertical_r) / 2 / lib->tech.dbu;
  lib->c_dbu = (lib->wire.horizontal_c + lib->wire.vertical_c) / 2 / lib->tech.dbu;
  return lib;
}
// Liberty and LEF parses dominate small FP checkpoints; one immutable view per configuration.
std::shared_ptr<const Library> library(const Options& options) {
  static std::mutex                                            lock;
  static std::map<std::string, std::shared_ptr<const Library>> cache;
  std::string                                                  key = options.liberty + "|" + options.tech_rc_file;
  for (const auto& l : options.lefs) {
    key += "|" + l;
  }
  for (const auto& p : options.resize_dont_use) {
    key += "|!" + p;
  }
  key += std::format("|{}|{}|{}|{}",
                     options.resize_vt_swap,
                     options.repair_reset,
                     options.resistance_ohm_um,
                     options.capacitance_ff_um);
  std::scoped_lock guard(lock);
  if (const auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  auto lib = build_library(options);
  cache.emplace(key, lib);
  return lib;
}
Point pin_offset(const Library& lib, const std::string& master, const std::string& pin) {
  const auto& macro = lib.tech.macros.at(master);
  const auto  p     = macro.pins.find(pin);
  if (p == macro.pins.end() || p->second.shapes.empty()) {
    return {0, 0};
  }
  const auto& r = p->second.shapes.front().rect;
  return {((r.xl + r.xh) / 2 + macro.origin_x - macro.width / 2) * lib.tech.dbu,
          ((r.yl + r.yh) / 2 + macro.origin_y - macro.height / 2) * lib.tech.dbu};
}

// ---- the planner -----------------------------------------------------------
struct Inst {
  std::string              master;
  const Buffer*            cell     = nullptr;  // combinational inventory cell, else null
  bool                     editable = false;
  bool                     added    = false;
  Point                    center{};
  std::vector<std::string> inputs;
  std::string              output;
  // Non-inventory combinational cells (several outputs, wide functions) are
  // traversed by path tracing but never edited.
  bool                     through = false;
  std::vector<std::string> outputs;
};
struct Stage {
  std::string driver, sink, input, inst;  // input/inst empty for a port startpoint
  double      at_in = 0, at_out = 0, at_sink = 0, slew_in = 0, slew_out = 0;
};
struct Move {
  enum class Kind { upsize, isolate, repeater, shed };
  Kind                                               kind = Kind::upsize;
  double                                             est  = 0;
  std::string                                        inst;
  const Buffer*                                      to = nullptr;
  std::string                                        driver, owner;
  std::vector<std::string>                           sinks;
  const Buffer*                                      buffer = nullptr;
  Point                                              at{};
  std::vector<Point>                                 chain;  // repeater chain seeds, driver to sink
  std::vector<std::pair<std::string, const Buffer*>> downs;
  [[nodiscard]] bool                                 adds_cells() const { return kind == Kind::isolate || kind == Kind::repeater; }
};
struct Counters {
  unsigned probes = 0, accepted = 0;
  double   seconds = 0;
};

class Planner {
public:
  Planner(const Result& state, const Options& options, const Timing_feedback& timing, Whatif& w, Resize_stage stage,
          const Library& lib)
      : state_(state), options_(options), timing_(timing), w_(w), stage_(stage), lib_(lib) {}

  Resize_plan run() {
    const auto start = Clock::now();
    build_model();
    start_mark_ = w_.mark();
    wns_        = w_.wns();
    tns_        = w_.tns();
    // Only gates with more than this slack above the worst one are "non-critical"
    // for down-sizing. A wide margin (40% of the shortest clock period) keeps
    // the recovery safe where another timer's view of the worst path differs.
    period_     = w_.clock_period();
    margin_     = std::max(25.0, period_ > 0 ? .4 * period_ : 0.0);
    Resize_plan plan;
    plan.wns_before = wns_;
    plan.tns_before = tns_;
    try {
      auto t = Clock::now();
      std::cout << std::format("resize: plan start wns {:.2f} tns {:.1f} margin {:.1f}\n", wns_, tns_, margin_) << std::flush;
      // LHD_RESIZE_PHASES (debug): subset of "eta" (electrical, timing, area).
      const char*       phases_env = std::getenv("LHD_RESIZE_PHASES");
      const std::string phases     = phases_env ? phases_env : "eta";
      if (phases.find('e') != std::string::npos) {
        electrical_phase();
      }
      electrical_s_ = elapsed(t);
      std::cout << std::format("resize: electrical {} nets {} fixed wns {:.2f} tns {:.1f} {:.1f}s\n",
                               electrical_nets_,
                               electrical_fixed_,
                               wns_,
                               tns_,
                               electrical_s_)
                << std::flush;
      t = Clock::now();
      if (wns_ < 0 && phases.find('t') != std::string::npos) {
        batch_phase();
        timing_phase();
      }
      timing_s_ = elapsed(t);
      t         = Clock::now();
      if (phases.find('a') != std::string::npos) {
        recovery_phase();
      }
      recovery_s_ = elapsed(t);
    } catch (...) {
      w_.rollback(start_mark_);
      throw;
    }
    plan.wns_after = wns_;
    plan.tns_after = tns_;
    w_.rollback(start_mark_);
    plan.edits  = std::move(edits_);
    plan.groups = group_;
    for (const auto& e : plan.edits) {
      if (e.kind == Resize_edit::Kind::buffer) {
        ++plan.buffers;
      }
    }
    // Net master changes of pre-existing instances (several edits may touch one).
    std::unordered_map<std::string, std::string> final_master;
    for (const auto& e : plan.edits) {
      if (e.kind == Resize_edit::Kind::resize) {
        final_master[e.instance] = e.master;
      }
    }
    for (const auto& [name, master] : final_master) {
      const auto original = original_area_.find(name);
      if (original == original_area_.end()) {
        continue;  // a buffer this plan created
      }
      const double area  = lib_.cells.at(master).area;
      plan.upsized      += area > original->second;
      plan.downsized    += area < original->second;
    }
    plan.report = report(plan, elapsed(start));
    return plan;
  }

private:
  const Result&          state_;
  const Options&         options_;
  const Timing_feedback& timing_;
  Whatif&                w_;
  Resize_stage           stage_;
  const Library&         lib_;

  std::unordered_map<std::string, Inst>                     insts_;
  std::unordered_map<std::string, std::vector<std::string>> nets_;    // driver -> sinks
  std::unordered_map<std::string, std::string>              net_of_;  // sink -> driver
  std::unordered_map<std::string, Point>                    pos_;
  std::unordered_set<std::string>                           blocked_drivers_;  // clock/protected nets: never touched
  std::unordered_set<std::string>                           names_, constant_drivers_;
  std::unordered_map<std::string, double>                   pin_cap_, original_area_;
  std::vector<std::function<void()>>                        journal_;
  std::vector<Resize_edit>                                  edits_;
  unsigned                                                  group_ = 0, serial_ = 0;
  size_t                                                    start_mark_ = 0;
  double                                                    wns_ = 0, tns_ = 0, margin_ = 25, period_ = 0;
  // statistics
  Counters                                                  upsize_, isolate_, repeater_, shed_, electrical_, recover_, batch_;
  std::unordered_set<std::string>                           rejected_;  // batched moves that failed alone
  unsigned    batch_moves_ = 0, batch_reject_tns_ = 0, batch_reject_wns_ = 0, batch_reject_electrical_ = 0;
  unsigned    upsized_ = 0, downsized_ = 0, endpoints_worked_ = 0, endpoints_stuck_ = 0, infeasible_paths_ = 0, feasible_paths_ = 0;
  unsigned    electrical_nets_ = 0, electrical_fixed_ = 0, rounds_ = 0, recover_batches_ = 0;
  double      electrical_s_ = 0, timing_s_ = 0, recovery_s_ = 0, probe_s_ = 0;
  unsigned    probes_       = 0;
  unsigned    reject_slack_ = 0, reject_wns_ = 0, reject_tns_ = 0, reject_electrical_ = 0, no_moves_ = 0;
  unsigned    generated_[4]  = {0, 0, 0, 0};
  std::string stop_reason_   = "none";
  bool        debug_         = std::getenv("LHD_RESIZE_DEBUG") != nullptr;
  unsigned    debug_printed_ = 0;

  // ---- model -------------------------------------------------------------
  void build_model() {
    auto&      graph           = *state_.graph;
    const auto protected_names = protected_buffers(graph, lib_.cells, lib_.special, !options_.repair_reset);
    std::unordered_map<std::string, const physical::Component*> components;
    for (const auto& c : state_.floorplan.components) {
      components.emplace(c.name, &c);
    }
    for (auto n : graph.body().nodes()) {
      const std::string name(n.get_name());
      if (name.empty()) {
        continue;
      }
      names_.insert(name);
      const auto io = n.get_subnode_io();
      if (!io) {
        continue;
      }
      Inst inst;
      inst.master = std::string(io->get_name());
      if (const auto c = components.find(name); c != components.end()) {
        const auto& p = c->second->place;
        inst.center   = {p.x + p.width / 2.0, p.y + p.height / 2.0};
      }
      if (const auto* cell = combinational(n, lib_.cells)) {
        original_area_.emplace(name, cell->area);
        inst.cell   = cell;
        inst.output = name + ":" + cell->output;
        for (const auto& pin : cell->inputs) {
          inst.inputs.push_back(name + ":" + pin);
        }
        bool scalar = true;
        for (const auto& pin : cell->inputs) {
          const auto drivers = n.get_sink_pin(pin).get_driver_pins();
          scalar             = scalar && drivers.size() == 1 && gu::bits_of(drivers.front()) == 1;
        }
        const auto c  = components.find(name);
        inst.editable = scalar && !fixed(n) && !protected_names.contains(name) && c != components.end()
                        && c->second->place.status != Ann_place::Status::FIXED
                        && c->second->place.status != Ann_place::Status::LOCKED && lib_.ladders.contains(cell->name);
      } else {
        bool combinational_outputs = !io->get_output_pin_decls().empty();
        for (const auto& d : io->get_output_pin_decls()) {
          const auto drive      = lib_.drives.find({inst.master, d.name});
          combinational_outputs = combinational_outputs && drive != lib_.drives.end() && !drive->second.sequential;
          inst.outputs.push_back(name + ":" + d.name);
        }
        if (combinational_outputs) {
          inst.through = true;
          for (const auto& d : io->get_input_pin_decls()) {
            if (io->get_bits(d.name) <= 1) {
              inst.inputs.push_back(name + ":" + d.name);
            }
          }
        }
      }
      if (io->get_input_pin_decls().empty()) {
        // Tie cells (and any other input-less driver) drive constants: never
        // buffered or sized, whatever their fanout.
        for (const auto& d : io->get_output_pin_decls()) {
          constant_drivers_.insert(name + ":" + d.name);
        }
      }
      insts_.emplace(name, std::move(inst));
    }
    for (const auto& net : state_.parasitics->nets) {
      auto& sinks = nets_[net.driver];
      for (const auto& node : net.nodes) {
        if (node.pin.empty() || node.pin == net.driver) {
          continue;
        }
        sinks.push_back(node.pin);
        net_of_.emplace(node.pin, net.driver);
      }
      const auto owner = instance_of(net.driver);
      const auto pin   = timing_.pins.find(net.driver);
      if (constant_drivers_.contains(net.driver)) {
        blocked_drivers_.insert(net.driver);
      }
      if ((pin != timing_.pins.end() && pin->second.clock) || (owner.empty() && protected_names.contains("@port:" + net.driver))
          || (!owner.empty() && protected_names.contains(owner))) {
        blocked_drivers_.insert(net.driver);
      }
    }
    // A sink of a clock net is part of the clock network as well.
    for (const auto& [label, pin] : timing_.pins) {
      if (pin.clock) {
        if (const auto d = net_of_.find(label); d != net_of_.end()) {
          blocked_drivers_.insert(d->second);
        }
      }
    }
    for (const auto& [label, xy] : state_.pin_locations) {
      pos_.emplace(label, xy);
    }
  }
  // A same-cycle slack can never exceed the clock period; a larger one means the
  // timer does not model the real capture (a half-cycle negative-edge check, an
  // unmodelled exception), so such gates are never down-sized.
  [[nodiscard]] bool believable(double slack) const { return period_ <= 0 || slack <= period_; }
  [[nodiscard]] bool editable(const std::string& name) const {
    const auto it = insts_.find(name);
    return it != insts_.end() && it->second.editable && !blocked_drivers_.contains(it->second.output);
  }
  double pin_cap(const std::string& label) {
    if (const auto it = pin_cap_.find(label); it != pin_cap_.end()) {
      return it->second;
    }
    double     cap  = 0;
    const auto inst = insts_.find(instance_of(label));
    if (inst != insts_.end()) {
      cap = w_.pin_capacitance(inst->second.master, pin_of(label)).value_or(0);
    }
    pin_cap_.emplace(label, cap);
    return cap;
  }
  double cell_pin_cap(const Buffer& cell, const std::string& pin) { return w_.pin_capacitance(cell.name, pin).value_or(0); }
  Point  position(const std::string& label) const {
    if (const auto it = pos_.find(label); it != pos_.end()) {
      return it->second;
    }
    const auto inst = insts_.find(instance_of(label));
    return inst == insts_.end() ? Point{0, 0} : inst->second.center;
  }
  physical::Rc_net net_rc(const std::string& driver) {
    std::vector<std::pair<std::string, std::pair<double, double>>> pins;
    pins.emplace_back(driver, position(driver));
    for (const auto& s : nets_.at(driver)) {
      pins.emplace_back(s, position(s));
    }
    return steiner_rc(driver, pins, lib_.wire, lib_.tech.dbu);
  }

  // ---- journaled model edits ----------------------------------------------
  void set_sinks(const std::string& driver, std::vector<std::string> sinks) {
    auto it = nets_.find(driver);
    if (it == nets_.end()) {
      nets_.emplace(driver, std::move(sinks));
      journal_.emplace_back([this, driver] { nets_.erase(driver); });
      return;
    }
    auto old   = std::move(it->second);
    it->second = std::move(sinks);
    journal_.emplace_back([this, driver, old = std::move(old)] { nets_[driver] = old; });
  }
  void set_net_of(const std::string& sink, const std::string& driver) {
    std::optional<std::string> old;
    if (const auto it = net_of_.find(sink); it != net_of_.end()) {
      old = it->second;
    }
    net_of_[sink] = driver;
    journal_.emplace_back([this, sink, old] {
      if (old) {
        net_of_[sink] = *old;
      } else {
        net_of_.erase(sink);
      }
    });
  }
  void set_pos(const std::string& label, Point xy) {
    pos_[label] = xy;
    journal_.emplace_back([this, label] { pos_.erase(label); });
  }
  void resize(const std::string& name, const Buffer* to) {
    auto&       inst = insts_.at(name);
    const auto* from = inst.cell;
    if (from == to) {
      return;
    }
    w_.resize(name, to->name);
    inst.cell   = to;
    inst.master = to->name;
    journal_.emplace_back([this, name, from] {
      insts_.at(name).cell   = from;
      insts_.at(name).master = from->name;
    });
    for (const auto& in : inst.inputs) {
      pin_cap_.erase(in);
    }
    journal_.emplace_back([this, name] {
      for (const auto& in : insts_.at(name).inputs) {
        pin_cap_.erase(in);
      }
    });
    edits_.push_back({Resize_edit::Kind::resize, name, to->name, {}, {}, {}, {}, group_});
  }
  std::string insert_buffer(const std::string& driver, const std::vector<std::string>& moved, const Buffer* cell, Point center,
                            const std::string& owner) {
    std::string name;
    do {
      name = std::format("__place_rebuffer_{}_{}", owner.empty() ? std::string("port") : owner, serial_++);
    } while (names_.contains(name) || insts_.contains(name));
    const auto in = name + ":" + cell->inputs.front(), out = name + ":" + cell->output;
    const auto oi = pin_offset(lib_, cell->name, cell->inputs.front()), oo = pin_offset(lib_, cell->name, cell->output);
    set_pos(in, {center.first + oi.first, center.second + oi.second});
    set_pos(out, {center.first + oo.first, center.second + oo.second});
    Inst inst;
    inst.master   = cell->name;
    inst.cell     = cell;
    inst.editable = true;
    inst.added    = true;
    inst.center   = center;
    inst.inputs   = {in};
    inst.output   = out;
    insts_.emplace(name, std::move(inst));
    names_.insert(name);
    journal_.emplace_back([this, name] {
      insts_.erase(name);
      names_.erase(name);
    });
    w_.add_cell(name, cell->name);
    std::unordered_set<std::string> gone(moved.begin(), moved.end());
    std::vector<std::string>        rest;
    for (const auto& s : nets_.at(driver)) {
      if (!gone.contains(s)) {
        rest.push_back(s);
      }
    }
    rest.push_back(in);
    set_sinks(driver, std::move(rest));
    set_net_of(in, driver);
    set_sinks(out, moved);
    for (const auto& s : moved) {
      set_net_of(s, out);
    }
    w_.set_net(net_rc(driver));
    w_.set_net(net_rc(out));
    edits_.push_back({Resize_edit::Kind::buffer, name, cell->name, driver, moved, center, owner, group_});
    return name;
  }
  // RAII transaction over the timer, the model and the edit list.
  struct Attempt {
    Planner& p;
    size_t   w_mark, j_mark, e_mark;
    bool     done = false;
    explicit Attempt(Planner& planner)
        : p(planner), w_mark(planner.w_.mark()), j_mark(planner.journal_.size()), e_mark(planner.edits_.size()) {}
    Attempt(const Attempt&)            = delete;
    Attempt& operator=(const Attempt&) = delete;
    void     commit() {
      done = true;
      ++p.group_;
    }
    void rollback() {
      if (done) {
        return;
      }
      done = true;
      p.w_.rollback(w_mark);
      while (p.journal_.size() > j_mark) {
        auto undo = std::move(p.journal_.back());
        p.journal_.pop_back();
        undo();
      }
      p.edits_.resize(e_mark);
    }
    ~Attempt() { rollback(); }
  };

  // ---- delay helpers -------------------------------------------------------
  double                load(const std::string& driver) { return w_.load(driver).value_or(0); }
  // Delay of the instance driving `driver` from its input `input` at a given load.
  std::optional<double> stage_delay(const std::string& master, const std::string& input, const std::string& output, double slew,
                                    double load_ff) {
    const auto a = w_.arc(master, input, output, slew, load_ff);
    return a ? std::optional<double>(a->first) : std::nullopt;
  }
  // Delay sensitivity of a non-resizable source (port, register, macro) to load.
  double source_resistance(const std::string& driver) const {
    const auto inst = insts_.find(instance_of(driver));
    if (inst == insts_.end()) {
      return 0;
    }
    const auto drive = lib_.drives.find({inst->second.master, pin_of(driver)});
    return drive == lib_.drives.end() ? 0 : drive->second.resistance_ohm;
  }
  // Driver delay change when the load of `driver`'s net moves from l0 to l1.
  double driver_delta(const Stage& st, double l0, double l1) {
    if (!st.input.empty()) {
      const auto& inst = insts_.at(st.inst);
      const auto  a    = stage_delay(inst.master, pin_of(st.input), pin_of(st.driver), st.slew_in, l0);
      const auto  b    = stage_delay(inst.master, pin_of(st.input), pin_of(st.driver), st.slew_in, l1);
      if (a && b) {
        return *b - *a;
      }
    }
    return source_resistance(st.driver) * (l1 - l0) * 1e-3;
  }
  double electrical_limit_ok(const Buffer& cell, double load_ff) const {
    return !cell.max_capacitance_ff || load_ff <= *cell.max_capacitance_ff;
  }

  // ---- probing -------------------------------------------------------------
  std::vector<std::string> net_pins(const std::string& driver) const {
    std::vector<std::string> pins{driver};
    if (const auto it = nets_.find(driver); it != nets_.end()) {
      pins.insert(pins.end(), it->second.begin(), it->second.end());
    }
    return pins;
  }
  // Pins whose slew/cap/fanout an edit of these nets can change.
  std::vector<std::string> touched(const std::vector<std::string>& drivers) const {
    std::vector<std::string>        pins;
    std::unordered_set<std::string> seen;
    for (const auto& d : drivers) {
      for (auto& p : net_pins(d)) {
        if (seen.insert(p).second) {
          pins.push_back(std::move(p));
        }
      }
    }
    return pins;
  }
  std::vector<std::string> nets_of_instance(const std::string& name) const {
    std::vector<std::string> drivers;
    const auto&              inst = insts_.at(name);
    drivers.push_back(inst.output);
    for (const auto& in : inst.inputs) {
      if (const auto d = net_of_.find(in); d != net_of_.end()) {
        drivers.push_back(d->second);
      }
    }
    return drivers;
  }
  struct Probe {
    double wns = 0, tns = 0;
  };
  Probe measure() {
    const auto t = Clock::now();
    Probe      p{w_.wns(), w_.tns()};
    probe_s_ += elapsed(t);
    ++probes_;
    return p;
  }
  static bool electrical_not_worse(std::pair<unsigned, double> before, std::pair<unsigned, double> after) {
    return after.first < before.first || (after.first == before.first && after.second <= before.second + 1e-9);
  }
  static constexpr double kWnsEps = 1e-3, kTnsEps = 1e-2;
  [[nodiscard]] bool      timing_not_worse(const Probe& p) const { return p.wns >= wns_ - kWnsEps && p.tns >= tns_ - kTnsEps; }

  // ---- phase E: real Liberty violations only --------------------------------
  void electrical_phase() {
    std::set<std::string> drivers;
    for (const auto& [label, pin] : timing_.pins) {
      if (pin.violations == 0 || pin.clock) {
        continue;
      }
      if (nets_.contains(label)) {
        drivers.insert(label);
      } else if (const auto d = net_of_.find(label); d != net_of_.end()) {
        drivers.insert(d->second);
      }
    }
    std::vector<std::string> todo;
    for (const auto& driver : drivers) {
      if (!blocked_drivers_.contains(driver) && nets_.contains(driver)) {
        todo.push_back(driver);
      }
    }
    electrical_nets_ = todo.size();
    // Many violating nets (an unbuffered synthesis netlist): plan each repair
    // locally and probe them together; a rejected batch is bisected down to the
    // per-net search, which also tries every alternative.
    std::vector<std::pair<size_t, size_t>> pending;
    const size_t                           batch = todo.size() > 32 ? std::max<size_t>(32, todo.size() / 8) : 1;
    for (size_t i = 0; i < todo.size(); i += batch) {
      pending.emplace_back(i, std::min(todo.size(), i + batch));
    }
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
      const auto [begin, end] = pending[cursor];
      if (end - begin == 1) {
        electrical_fixed_ += fix_electrical(todo[begin]);
        continue;
      }
      std::vector<std::string> drivers_in;
      for (size_t i = begin; i < end; ++i) {
        drivers_in.push_back(todo[i]);
      }
      auto                     pins   = touched(drivers_in);
      const auto               before = w_.electrical(pins);
      Attempt                  a(*this);
      std::vector<std::string> created;
      unsigned                 applied = 0;
      for (const auto& d : drivers_in) {
        applied += quick_electrical(d, created);
      }
      for (const auto& b : created) {
        for (auto& q : net_pins(insts_.at(b).output)) {
          pins.push_back(std::move(q));
        }
      }
      for (const auto& d : drivers_in) {  // upsized drivers load their input nets too
        if (const auto inst = insts_.find(instance_of(d)); inst != insts_.end() && inst->second.output == d) {
          for (auto& q : touched(nets_of_instance(instance_of(d)))) {
            pins.push_back(std::move(q));
          }
        }
      }
      const auto p     = measure();
      const auto after = w_.electrical(pins);
      ++electrical_.probes;
      if (applied && after.first < before.first && timing_not_worse(p)) {
        a.commit();
        wns_ = p.wns;
        tns_ = p.tns;
        ++electrical_.accepted;
        electrical_fixed_ += applied;
        continue;
      }
      a.rollback();
      const auto mid = begin + (end - begin) / 2;
      pending.emplace_back(begin, mid);
      pending.emplace_back(mid, end);
    }
  }
  // One local repair without probing: the smallest stronger driver that meets
  // its Liberty limits at the present load, otherwise a fanout tree.
  unsigned quick_electrical(const std::string& driver, std::vector<std::string>& created) {
    const auto owner = instance_of(driver);
    if (editable(owner) && insts_.at(owner).output == driver) {
      const auto& inst    = insts_.at(owner);
      const auto* current = inst.cell;
      const auto  l       = load(driver);
      const auto  fanout  = nets_.at(driver).size();
      double      slew_in = 0;
      for (const auto& in : inst.inputs) {
        slew_in = std::max(slew_in, w_.pin(in).slew.value_or(0));
      }
      for (const auto* v : lib_.ladders.at(current->name)) {
        if (v->area <= current->area || (v->max_capacitance_ff && *v->max_capacitance_ff < l * 1.1)
            || (v->max_fanout && *v->max_fanout < fanout)) {
          continue;
        }
        double slew = 0;
        for (const auto& in : inst.inputs) {
          if (const auto a = w_.arc(v->name, pin_of(in), v->output, slew_in, l)) {
            slew = std::max(slew, a->second);
          }
        }
        if (v->max_transition_ps && slew > .9 * *v->max_transition_ps) {
          continue;
        }
        resize(owner, v);
        ++upsized_;
        return 1;
      }
    }
    if (nets_.at(driver).size() >= 4 && build_tree(driver, nets_.at(driver), 0, owner, created)) {
      return 1;
    }
    return wire_repair(driver, created) ? 1 : 0;
  }
  // A long wire to few sinks: a repeater chain toward the farthest sink.
  bool wire_repair(const std::string& driver, std::vector<std::string>& created) {
    const auto& sinks = nets_.at(driver);
    if (sinks.empty()) {
      return false;
    }
    const Point d   = position(driver);
    std::string far = sinks.front();
    for (const auto& s : sinks) {
      if (manhattan(d, position(s)) > manhattan(d, position(far))) {
        far = s;
      }
    }
    Stage st;
    st.driver     = driver;
    st.sink       = far;
    st.inst       = instance_of(driver);
    const auto td = w_.pin(driver);
    st.at_out     = td.arrival.value_or(0);
    st.slew_out   = td.slew.value_or(0);
    st.at_sink    = std::max(w_.pin(far).arrival.value_or(0), st.at_out + 3);
    std::vector<Move> moves;
    repeater_moves({st}, 0, moves);
    if (moves.empty()) {
      return false;
    }
    const auto before = edits_.size();
    apply(moves.front());
    for (size_t i = before; i < edits_.size(); ++i) {
      if (edits_[i].kind == Resize_edit::Kind::buffer) {
        created.push_back(edits_[i].instance);
      }
    }
    return true;
  }
  bool fix_electrical(const std::string& driver) {
    auto       pins   = touched({driver});
    const auto before = w_.electrical(pins);
    if (before.first == 0) {
      return true;  // an earlier repair already fixed it
    }
    const auto owner = instance_of(driver);
    // 1. A stronger driver, smallest first.
    if (editable(owner) && insts_.at(owner).output == driver) {
      const auto* current = insts_.at(owner).cell;
      for (const auto* v : lib_.ladders.at(current->name)) {
        if (v->area <= current->area || v == current) {
          continue;
        }
        Attempt    a(*this);
        const auto upstream = touched(nets_of_instance(owner));
        const auto e0       = w_.electrical(upstream);
        resize(owner, v);
        const auto p  = measure();
        const auto e1 = w_.electrical(upstream);
        ++electrical_.probes;
        if (e1.first < e0.first && timing_not_worse(p)) {
          a.commit();
          wns_ = p.wns;
          tns_ = p.tns;
          ++electrical_.accepted;
          ++upsized_;
          return true;
        }
      }
    }
    // 2. A variable-base buffer tree over the sinks, 3. a repeater chain.
    for (const bool tree : {true, false}) {
      if (try_topology(driver, owner, pins, before, tree)) {
        return true;
      }
    }
    return false;
  }
  bool try_topology(const std::string& driver, const std::string& owner, const std::vector<std::string>& pins,
                    std::pair<unsigned, double> before, bool tree) {
    Attempt                  a(*this);
    std::vector<std::string> created;
    if (tree ? !build_tree(driver, nets_.at(driver), 0, owner, created) : !wire_repair(driver, created)) {
      return false;
    }
    const auto p   = measure();
    auto       all = pins;
    for (const auto& b : created) {
      for (auto& q : net_pins(insts_.at(b).output)) {
        all.push_back(std::move(q));
      }
    }
    const auto after = w_.electrical(all);
    ++electrical_.probes;
    if (after.first < before.first && timing_not_worse(p)) {
      a.commit();
      wns_ = p.wns;
      tns_ = p.tns;
      ++electrical_.accepted;
      return true;
    }
    return false;
  }
  // Smallest repeater able to drive load_ff with margin; the strongest otherwise.
  const Buffer* buffer_for(double load_ff, double margin = 1.25) const {
    for (const auto* b : lib_.buffers) {
      if (!b->max_capacitance_ff || *b->max_capacitance_ff >= load_ff * margin) {
        return b;
      }
    }
    return lib_.buffers.empty() ? nullptr : lib_.buffers.back();
  }
  double group_load(const std::vector<std::string>& sinks, Point at) {
    double cap = 0;
    for (const auto& s : sinks) {
      cap += pin_cap(s) + lib_.c_dbu * manhattan(at, position(s));
    }
    return cap;
  }
  // Split sinks into k spatial groups by recursive median bisection along the wider spread.
  static void bisect(std::vector<std::pair<Point, std::string>>& items, size_t begin, size_t end, unsigned k,
                     std::vector<std::vector<std::string>>& groups) {
    if (k <= 1 || end - begin <= 1) {
      std::vector<std::string> g;
      for (size_t i = begin; i < end; ++i) {
        g.push_back(items[i].second);
      }
      if (!g.empty()) {
        groups.push_back(std::move(g));
      }
      return;
    }
    double x0 = kInf, x1 = -kInf, y0 = kInf, y1 = -kInf;
    for (size_t i = begin; i < end; ++i) {
      x0 = std::min(x0, items[i].first.first);
      x1 = std::max(x1, items[i].first.first);
      y0 = std::min(y0, items[i].first.second);
      y1 = std::max(y1, items[i].first.second);
    }
    const bool by_x = x1 - x0 >= y1 - y0;
    const auto kl   = k / 2;
    const auto mid  = begin + (end - begin) * kl / k;
    std::nth_element(items.begin() + begin, items.begin() + mid, items.begin() + end, [&](const auto& a, const auto& b) {
      return by_x ? std::tie(a.first.first, a.second) < std::tie(b.first.first, b.second)
                  : std::tie(a.first.second, a.second) < std::tie(b.first.second, b.second);
    });
    bisect(items, begin, mid, kl, groups);
    bisect(items, mid, end, k - kl, groups);
  }
  // Variable-base fanout tree: branching 2..8 chosen from the load the root and
  // a repeater may carry and from criticality; deeper levels only when a group
  // still exceeds a repeater's limits. Critical sinks stay on the root.
  bool build_tree(const std::string& driver, std::vector<std::string> sinks, unsigned depth, const std::string& owner,
                  std::vector<std::string>& created) {
    if (lib_.buffers.empty() || sinks.size() < 2 || depth > 5) {
      return false;
    }
    const Point root  = position(driver);
    const auto  limit = [&](const Buffer* b) {
      return std::pair{b && b->max_capacitance_ff ? *b->max_capacitance_ff : kInf, b && b->max_fanout ? *b->max_fanout : kInf};
    };
    const auto* reference = buffer_for(0);
    for (const auto* b : lib_.buffers) {  // a mid-strength repeater sets the group budget
      if (b->area <= lib_.buffers.front()->area * 2.5) {
        reference = b;
      }
    }
    const auto [ref_cap, ref_fanout] = limit(reference);
    // Keep the most critical sinks on the root when the net fails timing.
    std::vector<std::pair<double, std::string>> by_slack;
    for (const auto& s : sinks) {
      by_slack.emplace_back(w_.pin(s).slack.value_or(kInf), s);
    }
    std::sort(by_slack.begin(), by_slack.end());
    std::vector<std::string> keep;
    if (by_slack.front().first < 0) {
      for (const auto& [slack, s] : by_slack) {
        if (keep.size() >= 2 || slack > by_slack.front().first + 10) {
          break;
        }
        keep.push_back(s);
      }
    }
    std::vector<std::pair<Point, std::string>> items;
    double                                     total = 0;
    for (const auto& s : sinks) {
      if (std::find(keep.begin(), keep.end(), s) == keep.end()) {
        items.emplace_back(position(s), s);
        total += pin_cap(s);
      }
    }
    if (items.size() < 2) {
      return false;
    }
    // Wire to the group centroids is part of the load a repeater must carry.
    Point centroid{0, 0};
    for (const auto& [xy, s] : items) {
      centroid.first  += xy.first / items.size();
      centroid.second += xy.second / items.size();
    }
    double spread = 0;
    for (const auto& [xy, s] : items) {
      spread += manhattan(xy, centroid);
    }
    total      += lib_.c_dbu * spread;
    unsigned k  = static_cast<unsigned>(std::ceil(total / std::max(1e-9, .6 * ref_cap)));
    if (std::isfinite(ref_fanout)) {
      k = std::max(k, static_cast<unsigned>(std::ceil(items.size() / std::max(1.0, .75 * ref_fanout))));
    }
    if (by_slack.front().first < 0) {
      k += k / 2;  // criticality: smaller groups, faster branches
    }
    k = std::clamp(k, 2u, std::min(8u, static_cast<unsigned>(items.size())));
    std::vector<std::vector<std::string>> groups;
    bisect(items, 0, items.size(), k, groups);
    for (auto& group : groups) {
      Point c{0, 0};
      for (const auto& s : group) {
        const auto xy  = position(s);
        c.first       += xy.first / group.size();
        c.second      += xy.second / group.size();
      }
      // Seed on the Steiner edge from the root toward the group, near the group.
      const auto  at   = lerp(c, root, .25);
      const auto  gl   = group_load(group, at);
      const auto* b    = buffer_for(gl);
      const auto  name = insert_buffer(driver, group, b, at, owner);
      created.push_back(name);
      const auto [cap, fanout] = limit(b);
      if ((gl > .9 * cap || group.size() > fanout) && group.size() >= 4) {
        build_tree(insts_.at(name).output, group, depth + 1, owner, created);
      }
    }
    return true;
  }

  // ---- phase T: path-based sizing and buffering ---------------------------
  std::vector<Stage> trace(const std::string& endpoint) {
    std::vector<Stage>              stages;
    std::string                     sink = endpoint;
    std::unordered_set<std::string> seen;
    while (stages.size() < 2000) {
      const auto d = net_of_.find(sink);
      if (d == net_of_.end() || !seen.insert(d->second).second) {
        break;
      }
      Stage st;
      st.driver       = d->second;
      st.sink         = sink;
      const auto ts   = w_.pin(sink);
      const auto td   = w_.pin(st.driver);
      st.at_sink      = ts.arrival.value_or(0);
      st.at_out       = td.arrival.value_or(0);
      st.slew_out     = td.slew.value_or(0);
      st.inst         = instance_of(st.driver);
      const auto inst = insts_.find(st.inst);
      const bool comb = inst != insts_.end()
                        && ((inst->second.cell && inst->second.output == st.driver)
                            || (inst->second.through
                                && std::find(inst->second.outputs.begin(), inst->second.outputs.end(), st.driver)
                                       != inst->second.outputs.end()));
      if (!comb) {
        stages.push_back(std::move(st));
        break;
      }
      double best = kInf;
      for (const auto& in : inst->second.inputs) {
        const auto t = w_.pin(in);
        if (t.slack && *t.slack < best) {
          best       = *t.slack;
          st.input   = in;
          st.at_in   = t.arrival.value_or(0);
          st.slew_in = t.slew.value_or(0);
        }
      }
      if (st.input.empty()) {
        stages.push_back(std::move(st));
        break;
      }
      sink = st.input;
      stages.push_back(std::move(st));
    }
    std::reverse(stages.begin(), stages.end());
    return stages;
  }
  // Optimistic slack with ideal (zero-RC) wires and the fastest same-function
  // drive at every stage, each loaded only by its on-path pin. Below zero no
  // buffer can fix the path, so none is spent on it.
  double ideal_slack(const std::vector<Stage>& stages, double slack) {
    constexpr double kIdealSlew = 1;
    double           gain       = 0;
    for (const auto& st : stages) {
      gain              += std::max(0.0, st.at_sink - st.at_out);
      const double next  = pin_cap(st.sink);
      if (!st.input.empty()) {
        const auto& inst   = insts_.at(st.inst);
        const auto  actual = st.at_out - st.at_in;
        double      best   = actual;
        const auto  ladder = inst.cell ? lib_.ladders.find(inst.cell->name) : lib_.ladders.end();
        if (ladder != lib_.ladders.end()) {
          for (const auto* v : ladder->second) {
            if (const auto d = stage_delay(v->name, pin_of(st.input), v->output, kIdealSlew, next)) {
              best = std::min(best, *d);
            }
          }
        } else if (const auto d = stage_delay(inst.master, pin_of(st.input), pin_of(st.driver), kIdealSlew, next)) {
          best = std::min(best, *d);
        }
        gain += std::max(0.0, actual - best);
      } else {
        const auto inst = insts_.find(st.inst);
        if (inst != insts_.end()) {
          const auto drive = lib_.drives.find({inst->second.master, pin_of(st.driver)});
          if (drive != lib_.drives.end() && drive->second.sequential) {
            gain += std::max(0.0, st.at_out - (drive->second.intrinsic_ps + drive->second.resistance_ohm * next * 1e-3));
          }
        }
      }
    }
    return slack + gain;
  }
  void upsize_moves(const std::vector<Stage>& stages, size_t k, std::vector<Move>& moves) {
    const auto& st = stages[k];
    if (st.input.empty() || !editable(st.inst)) {
      return;
    }
    const auto&  inst = insts_.at(st.inst);
    const double l    = load(st.driver);
    const auto   cur  = stage_delay(inst.master, pin_of(st.input), pin_of(st.driver), st.slew_in, l);
    if (!cur) {
      return;
    }
    const double cin = pin_cap(st.input);
    Move         best;
    best.est = .05;
    for (const auto* v : lib_.ladders.at(inst.cell->name)) {
      if (v == inst.cell || !electrical_limit_ok(*v, l)) {
        continue;
      }
      const auto d = stage_delay(v->name, pin_of(st.input), v->output, st.slew_in, l);
      if (!d) {
        continue;
      }
      double up = 0;
      if (k > 0) {
        const double dc = cell_pin_cap(*v, pin_of(st.input)) - cin;
        const double ul = load(stages[k - 1].driver);
        up              = driver_delta(stages[k - 1], ul, ul + dc);
      }
      const double gain = *cur - *d - up;
      if (gain > best.est) {
        best.kind   = Move::Kind::upsize;
        best.est    = gain;
        best.inst   = st.inst;
        best.to     = v;
        best.driver = st.driver;
      }
    }
    if (best.to) {
      moves.push_back(std::move(best));
    }
  }
  void isolate_moves(const std::vector<Stage>& stages, size_t k, double path_slack, std::vector<Move>& moves) {
    const auto& st    = stages[k];
    const auto& sinks = nets_.at(st.driver);
    if (sinks.size() < 2 || lib_.buffers.empty() || blocked_drivers_.contains(st.driver)) {
      return;
    }
    std::vector<std::string> moved;
    for (const auto& s : sinks) {
      if (s == st.sink) {
        continue;
      }
      const auto t = w_.pin(s);
      // A sink keeps margin for the repeater delay it will see.
      if (!t.slack || *t.slack >= path_slack + 25) {
        moved.push_back(s);
      }
    }
    if (moved.empty()) {
      return;
    }
    const Point d = position(st.driver);
    Point       c{0, 0};
    for (const auto& s : moved) {
      const auto xy  = position(s);
      c.first       += xy.first / moved.size();
      c.second      += xy.second / moved.size();
    }
    const auto   at   = lerp(c, d, .3);
    const double gl   = group_load(moved, at);
    const auto*  b    = buffer_for(gl);
    const double l    = load(st.driver);
    double       shed = 0;
    for (const auto& s : moved) {
      shed += pin_cap(s) + lib_.c_dbu * manhattan(d, position(s)) * .5;
    }
    const double l1   = std::max(cell_pin_cap(*b, b->inputs.front()), l - shed + cell_pin_cap(*b, b->inputs.front()));
    const double gain = -driver_delta(st, l, l1);
    if (gain > 1) {
      Move m;
      m.kind   = Move::Kind::isolate;
      m.est    = gain;
      m.driver = st.driver;
      m.sinks  = std::move(moved);
      m.buffer = b;
      m.at     = at;
      m.owner  = st.inst;
      moves.push_back(std::move(m));
    }
  }
  // Repeaters on a long on-path wire: one buffer at 30/50/70% of the edge, or
  // a chain of N evenly spaced ones (N from the optimal repeater spacing), each
  // seeded on the driver->sink edge it cuts. Elmore estimates pick the size,
  // count and cut; the incremental timer decides.
  void repeater_moves(const std::vector<Stage>& stages, size_t k, std::vector<Move>& moves) {
    const auto& st = stages[k];
    if (lib_.buffers.empty() || blocked_drivers_.contains(st.driver) || st.at_sink - st.at_out < 3) {
      return;
    }
    const Point  d = position(st.driver), s = position(st.sink);
    const double l0 = manhattan(d, s);
    if (l0 < 5 * lib_.tech.dbu) {
      return;
    }
    const double cs = pin_cap(st.sink), load0 = load(st.driver);
    const double r = lib_.r_dbu, c = lib_.c_dbu;
    const auto   wire  = [&](double l, double tail) { return r * l * (c * l / 2 + tail) * 1e-3; };
    const double t_old = wire(l0, cs);
    Move         best;
    best.est = 1;
    for (const auto* b : lib_.buffers) {
      const double   cin       = cell_pin_cap(*b, b->inputs.front());
      const double   unit      = b->intrinsic_ps + b->resistance_ohm * cin * 1e-3;
      const double   l_opt     = r > 0 && c > 0 ? std::sqrt(2 * std::max(unit, 1.0) / (r * c * 1e-3)) : kInf;
      const unsigned max_chain = std::isfinite(l_opt) ? std::clamp(static_cast<unsigned>(std::ceil(l0 / l_opt)), 1u, 12u) : 1u;
      for (unsigned n = 1; n <= max_chain; ++n) {
        std::vector<std::vector<double>> cuts;
        if (n == 1) {
          cuts = {{.3}, {.5}, {.7}};
        } else {
          std::vector<double> even;
          for (unsigned i = 1; i <= n; ++i) {
            even.push_back(double(i) / (n + 1));
          }
          cuts.push_back(std::move(even));
        }
        for (const auto& fractions : cuts) {
          std::vector<Point> chain;
          for (const double f : fractions) {
            chain.push_back(lerp(d, s, f));
          }
          std::vector<std::string> moved{st.sink};
          for (const auto& other : nets_.at(st.driver)) {
            if (other != st.sink && manhattan(position(other), chain.front()) < manhattan(position(other), d)) {
              moved.push_back(other);
            }
          }
          double moved_cap = 0;
          for (const auto& m : moved) {
            moved_cap += pin_cap(m) + c * manhattan(d, position(m));
          }
          const double down = group_load(moved, chain.back());
          if (!electrical_limit_ok(*b, down)) {
            continue;
          }
          const double lp    = manhattan(d, chain.front());
          const double l1    = std::max(cin, load0 - moved_cap + c * lp + cin);
          double       t_new = driver_delta(st, load0, l1) + wire(lp, cin);
          double       slew  = std::max(5.0, st.slew_out);
          bool         ok    = true;
          for (size_t i = 0; i < chain.size() && ok; ++i) {
            const bool   last = i + 1 == chain.size();
            const double seg  = manhattan(chain[i], last ? s : chain[i + 1]);
            const auto   a    = w_.arc(b->name, b->inputs.front(), b->output, slew, last ? down : c * seg + cin);
            if (!a) {
              ok = false;
              break;
            }
            t_new += a->first + wire(seg, last ? cs : cin);
            slew   = std::max(5.0, a->second);
          }
          const double gain = ok ? t_old - t_new : 0;
          // Each extra repeater must pay for itself.
          if (ok && gain - (n - 1) * 1.0 > best.est) {
            best.kind   = Move::Kind::repeater;
            best.est    = gain;
            best.driver = st.driver;
            best.sinks  = moved;
            best.buffer = b;
            best.at     = chain.front();
            best.chain  = chain;
            best.owner  = st.inst;
          }
        }
      }
    }
    if (best.buffer) {
      moves.push_back(std::move(best));
    }
  }
  // Downsize non-critical gates loading an on-path net: less load, less area.
  void shed_moves(const std::vector<Stage>& stages, size_t k, double path_slack, std::vector<Move>& moves) {
    const auto& st = stages[k];
    Move        m;
    m.kind    = Move::Kind::shed;
    m.driver  = st.driver;
    double dc = 0;
    for (const auto& s : nets_.at(st.driver)) {
      const auto owner = instance_of(s);
      if (s == st.sink || !editable(owner)) {
        continue;
      }
      const auto& inst = insts_.at(owner);
      const auto  out  = w_.pin(inst.output);
      if (!out.slack || *out.slack < path_slack + 50 || !believable(*out.slack)) {
        continue;
      }
      // Load shedding on a failing path: the gate keeps 50 ps above the path.
      const auto* small = smaller(owner, *out.slack - path_slack - 50);
      if (small) {
        dc += pin_cap(s) - cell_pin_cap(*small, pin_of(s));
        m.downs.emplace_back(owner, small);
      }
    }
    if (m.downs.empty() || dc <= 0) {
      return;
    }
    const double l = load(st.driver);
    m.est          = -driver_delta(st, l, l - dc);
    if (m.est > .2) {
      moves.push_back(std::move(m));
    }
  }
  // Smallest same-function variant whose worst arc stays within half the budget.
  const Buffer* smaller(const std::string& name, double budget) {
    if (budget <= 0) {
      return nullptr;
    }
    const auto&                                 inst = insts_.at(name);
    const auto                                  l    = load(inst.output);
    double                                      cur  = 0;
    std::vector<std::pair<std::string, double>> arcs;
    for (const auto& in : inst.inputs) {
      const auto slew = w_.pin(in).slew.value_or(10);
      arcs.emplace_back(pin_of(in), slew);
      cur = std::max(cur, stage_delay(inst.master, pin_of(in), pin_of(inst.output), slew, l).value_or(0));
    }
    for (const auto* v : lib_.ladders.at(inst.cell->name)) {
      if (v->area >= inst.cell->area) {
        break;
      }
      if (!electrical_limit_ok(*v, l * 1.1)) {
        continue;
      }
      double worst = 0, slew = 0;
      for (const auto& [pin, s] : arcs) {
        const auto a = w_.arc(v->name, pin, v->output, s, l);
        if (!a) {
          worst = kInf;
          break;
        }
        worst = std::max(worst, a->first);
        slew  = std::max(slew, a->second);
      }
      if (v->max_transition_ps && slew > *v->max_transition_ps * .9) {
        continue;
      }
      if (worst - cur <= budget / 2) {
        return v;
      }
    }
    return nullptr;
  }
  void apply(const Move& m) {
    switch (m.kind) {
      case Move::Kind::upsize: resize(m.inst, m.to); break;
      case Move::Kind::shed:
        for (const auto& [name, to] : m.downs) {
          resize(name, to);
        }
        break;
      case Move::Kind::isolate : insert_buffer(m.driver, m.sinks, m.buffer, m.at, m.owner); break;
      case Move::Kind::repeater: {
        auto driver = m.driver;
        for (const auto& at : m.chain.empty() ? std::vector<Point>{m.at} : m.chain) {
          driver = insts_.at(insert_buffer(driver, m.sinks, m.buffer, at, m.owner)).output;
        }
        break;
      }
    }
  }
  std::vector<std::string> move_drivers(const Move& m) const {
    std::vector<std::string> drivers;
    switch (m.kind) {
      case Move::Kind::upsize: drivers = nets_of_instance(m.inst); break;
      case Move::Kind::shed:
        for (const auto& [name, to] : m.downs) {
          for (auto& d : nets_of_instance(name)) {
            drivers.push_back(std::move(d));
          }
        }
        break;
      case Move::Kind::isolate:
      case Move::Kind::repeater: drivers.push_back(m.driver); break;
    }
    return drivers;
  }
  Counters& counter(const Move& m) {
    switch (m.kind) {
      case Move::Kind::upsize  : return upsize_;
      case Move::Kind::isolate : return isolate_;
      case Move::Kind::repeater: return repeater_;
      case Move::Kind::shed    : return shed_;
    }
    return upsize_;
  }
  bool attempt(const Move& m, const std::string& endpoint, double slack) {
    const auto t     = Clock::now();
    auto&      stats = counter(m);
    auto       pins  = touched(move_drivers(m));
    const auto e0    = w_.electrical(pins);
    Attempt    a(*this);
    apply(m);
    unsigned cells = 0;
    for (size_t i = a.e_mark; i < edits_.size(); ++i) {
      if (edits_[i].kind == Resize_edit::Kind::buffer) {
        ++cells;
        for (auto& p : net_pins(insts_.at(edits_[i].instance).output)) {
          pins.push_back(std::move(p));
        }
      }
    }
    const auto   p  = measure();
    const double s1 = w_.pin(endpoint).slack.value_or(-kInf);
    const auto   e1 = w_.electrical(pins);
    ++stats.probes;
    stats.seconds     += elapsed(t);
    const double need  = cells ? cells * std::max(1.0, .005 * std::abs(slack)) : .05;
    if (s1 < slack + need) {
      ++reject_slack_;
      return false;
    }
    if (p.wns < wns_ - kWnsEps) {
      ++reject_wns_;
      return false;
    }
    if (p.tns < tns_ - kTnsEps) {
      ++reject_tns_;
      return false;
    }
    if (!electrical_not_worse(e0, e1)) {
      ++reject_electrical_;
      return false;
    }
    a.commit();
    wns_ = p.wns;
    tns_ = p.tns;
    ++stats.accepted;
    if (m.kind == Move::Kind::upsize) {
      ++upsized_;
    } else if (m.kind == Move::Kind::shed) {
      downsized_ += m.downs.size();
    }
    return true;
  }
  std::vector<Move> path_moves(const std::vector<Stage>& stages, double slack, bool feasible) {
    std::vector<Move> moves;
    for (size_t k = 0; k < stages.size(); ++k) {
      upsize_moves(stages, k, moves);
      shed_moves(stages, k, slack, moves);
      if (feasible) {
        isolate_moves(stages, k, slack, moves);
        repeater_moves(stages, k, moves);
      }
    }
    std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
      // Prefer moves that add no cell when the estimates are comparable.
      const double ea = a.est * (a.adds_cells() ? .7 : 1), eb = b.est * (b.adds_cells() ? .7 : 1);
      return ea > eb;
    });
    return moves;
  }
  static std::string move_key(const Move& m) {
    return std::format("{}|{}|{}|{}|{}|{}",
                       static_cast<int>(m.kind),
                       m.driver,
                       m.inst,
                       m.to ? m.to->name : "",
                       m.buffer ? m.buffer->name : "",
                       m.chain.size());
  }
  [[nodiscard]] static unsigned cells_of(const Move& m) {
    return m.kind == Move::Kind::isolate ? 1 : m.kind == Move::Kind::repeater ? std::max<unsigned>(1, m.chain.size()) : 0;
  }
  // Many failing endpoints (long macro wires, a slow block): the best estimated
  // move of each path, on disjoint nets, probed together; a rejected batch is
  // bisected. Per-endpoint refinement follows for what remains.
  void batch_phase() {
    unsigned     stall = 0;
    for (unsigned round = 0; round < 10000; ++round) {
      const auto failing = w_.failing_endpoints(0);
      if (failing.size() < 64) {
        break;
      }
      const double                    wns_round = wns_, tns_round = tns_;
      const size_t                    take = std::max<size_t>(64, failing.size() / 4);
      std::vector<Move>               batch;
      std::unordered_set<std::string> used;
      for (size_t i = 0; i < failing.size() && i < take; ++i) {
        const auto& [endpoint, slack] = failing[i];
        const auto stages             = trace(endpoint);
        if (stages.empty()) {
          continue;
        }
        const bool feasible = ideal_slack(stages, slack) >= 0;
        ++(feasible ? feasible_paths_ : infeasible_paths_);
        for (auto& m : path_moves(stages, slack, feasible)) {
          // Shedding and isolation judge "non-critical" against this one path;
          // batched together they slow other paths, so they stay per endpoint.
          if (m.kind == Move::Kind::shed || m.kind == Move::Kind::isolate || rejected_.contains(move_key(m))) {
            continue;
          }
          const unsigned cells = cells_of(m);
          if (cells && m.est < 2 * cells * std::max(1.0, .005 * std::abs(slack))) {
            continue;  // a batched repeater must clearly pay for itself
          }
          std::vector<std::string> keys{m.driver};
          if (!m.inst.empty()) {
            keys.push_back(m.inst);
          }
          for (const auto& [name, to] : m.downs) {
            keys.push_back(name);
          }
          if (std::any_of(keys.begin(), keys.end(), [&](const auto& k) { return used.contains(k); })) {
            continue;
          }
          used.insert(keys.begin(), keys.end());
          ++generated_[static_cast<int>(m.kind)];
          batch.push_back(std::move(m));
          break;
        }
      }
      if (batch.empty()) {
        break;
      }
      std::vector<std::pair<size_t, size_t>> pending{
          {0, batch.size()}
      };
      for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
        const auto [begin, end] = pending[cursor];
        std::vector<std::string> drivers;
        for (size_t i = begin; i < end; ++i) {
          for (auto& d : move_drivers(batch[i])) {
            drivers.push_back(std::move(d));
          }
        }
        auto       pins = touched(drivers);
        const auto e0   = w_.electrical(pins);
        Attempt    a(*this);
        for (size_t i = begin; i < end; ++i) {
          apply(batch[i]);
        }
        for (size_t i = a.e_mark; i < edits_.size(); ++i) {
          if (edits_[i].kind == Resize_edit::Kind::buffer) {
            for (auto& q : net_pins(insts_.at(edits_[i].instance).output)) {
              pins.push_back(std::move(q));
            }
          }
        }
        const auto p  = measure();
        const auto e1 = w_.electrical(pins);
        ++batch_.probes;
        if (p.tns > tns_ + kTnsEps && timing_not_worse(p) && electrical_not_worse(e0, e1)) {
          a.commit();
          wns_ = p.wns;
          tns_ = p.tns;
          ++batch_.accepted;
          batch_moves_ += end - begin;
          continue;
        }
        ++(p.tns <= tns_ + kTnsEps ? batch_reject_tns_ : !timing_not_worse(p) ? batch_reject_wns_ : batch_reject_electrical_);
        if (debug_) {
          std::cout << std::format("resize-debug: batch [{},{}) wns {:.2f}->{:.2f} tns {:.1f}->{:.1f} elec {}/{:.3f}->{}/{:.3f}\n",
                                   begin,
                                   end,
                                   wns_,
                                   p.wns,
                                   tns_,
                                   p.tns,
                                   e0.first,
                                   e0.second,
                                   e1.first,
                                   e1.second);
        }
        a.rollback();
        if (end - begin <= 2) {
          for (size_t i = begin; i < end; ++i) {
            rejected_.insert(move_key(batch[i]));  // the next round tries the path's next move
          }
        }
        if (end - begin > 2) {
          const auto mid = begin + (end - begin) / 2;
          pending.emplace_back(begin, mid);
          pending.emplace_back(mid, end);
        }
      }
      std::cout << std::format(
          "resize: batch round {} failing {} moves {} wns {:.2f} tns {:.1f} probes {} ({:.1f}s) edits {} reject tns/wns/elec "
          "{}/{}/{}\n",
          round,
          failing.size(),
          batch.size(),
          wns_,
          tns_,
          probes_,
          probe_s_,
          edits_.size(),
          batch_reject_tns_,
          batch_reject_wns_,
          batch_reject_electrical_)
                << std::flush;
      const bool slow = tns_ - tns_round < std::max(1.0, 2e-3 * std::abs(tns_round)) && wns_ - wns_round < .5;
      stall           = slow ? stall + 1 : 0;
      if (stall >= 2) {
        break;
      }
    }
  }
  unsigned work_endpoint(const std::string& endpoint) {
    unsigned accepted = 0;
    for (unsigned iteration = 0; iteration < 24; ++iteration) {
      const auto slack = w_.pin(endpoint).slack;
      if (!slack || *slack >= 0) {
        break;
      }
      const auto stages = trace(endpoint);
      if (stages.empty()) {
        break;
      }
      const double ideal    = ideal_slack(stages, *slack);
      const bool   feasible = ideal >= 0;
      if (iteration == 0 && debug_ && debug_printed_ < 4) {
        ++debug_printed_;
        std::cout << std::format("resize-debug: endpoint {} slack {:.2f} ideal {:.2f} stages {}\n",
                                 endpoint,
                                 *slack,
                                 ideal,
                                 stages.size());
        for (const auto& st : stages) {
          const auto inst = insts_.find(st.inst);
          std::cout << std::format("  {} -> {} master {} edit {} gate {:.2f} wire {:.2f} load {:.2f} slew {:.2f}\n",
                                   st.input.empty() ? st.driver : st.input,
                                   st.sink,
                                   inst == insts_.end() ? std::string("-") : inst->second.master,
                                   editable(st.inst),
                                   st.input.empty() ? st.at_out : st.at_out - st.at_in,
                                   st.at_sink - st.at_out,
                                   load(st.driver),
                                   st.slew_out);
        }
      }
      if (iteration == 0) {
        ++(feasible ? feasible_paths_ : infeasible_paths_);
      }
      auto moves = path_moves(stages, *slack, feasible);
      for (const auto& m : moves) {
        ++generated_[static_cast<int>(m.kind)];
      }
      no_moves_     += moves.empty();
      bool progress  = false;
      for (size_t i = 0; i < moves.size() && i < 8; ++i) {
        if (attempt(moves[i], endpoint, *slack)) {
          progress = true;
          ++accepted;
          break;
        }
      }
      if (!progress) {
        break;
      }
    }
    return accepted;
  }
  void timing_phase() {
    std::unordered_set<std::string> stuck;
    unsigned                        stall = 0;
    stop_reason_                          = "no failing endpoint";
    // At FP checkpoints the coordinates still move: batched moves plus a short
    // per-endpoint pass; the legal placement gets the full refinement.
    const unsigned max_rounds = stage_ == Resize_stage::floorplan ? 2 : 100000;
    for (rounds_ = 0; rounds_ < max_rounds; ++rounds_) {
      const double wns_round = wns_, tns_round = tns_;
      const auto   failing = w_.failing_endpoints(0);
      unsigned     worked  = 0;
      for (const auto& [endpoint, slack] : failing) {
        if (stuck.contains(endpoint)) {
          continue;
        }
        if (worked >= 32) {
          break;
        }
        ++worked;
        ++endpoints_worked_;
        if (work_endpoint(endpoint) == 0) {
          stuck.insert(endpoint);
        }
      }
      if (worked == 0) {
        stop_reason_ = failing.empty() ? "no failing endpoint" : "every failing endpoint is stuck or provably infeasible";
        break;
      }
      std::cout << std::format(
          "resize: timing round {} worked {} stuck {} wns {:.2f} tns {:.1f} probes {} ({:.1f}s) moves u/s/i/r {}/{}/{}/{} none {} "
          "reject slack/wns/tns/elec {}/{}/{}/{} infeasible {}\n",
          rounds_,
          worked,
          stuck.size(),
          wns_,
          tns_,
          probes_,
          probe_s_,
          generated_[0],
          generated_[3],
          generated_[1],
          generated_[2],
          no_moves_,
          reject_slack_,
          reject_wns_,
          reject_tns_,
          reject_electrical_,
          infeasible_paths_)
                << std::flush;
      // Lack of progress: two consecutive rounds below 2% of the round's TNS and
      // 1 ps of WNS end the phase, independent of any cell budget.
      const bool slow = tns_ - tns_round < std::max(.1, .02 * std::abs(tns_round)) && wns_ - wns_round < 1;
      stall           = slow ? stall + 1 : 0;
      if (stall >= 2) {
        stop_reason_ = "no progress";
        break;
      }
    }
    endpoints_stuck_ = stuck.size();
  }

  // ---- phase A: area recovery (down-size slack-rich gates) -----------------
  void recovery_phase() {
    struct Candidate {
      std::string   name;
      const Buffer* to;
      double        saving;
    };
    std::vector<Candidate> candidates;
    const double           floor = std::max(0.0, wns_) + margin_;
    for (const auto& [name, inst] : insts_) {
      if (!editable(name) || inst.added) {
        continue;
      }
      const auto slack = w_.pin(inst.output).slack;
      if (!slack || *slack < floor + 10 || !believable(*slack)) {
        continue;
      }
      if (const auto* v = smaller(name, *slack - floor)) {
        candidates.push_back({name, v, inst.cell->area - v->area});
      }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
      return std::tie(b.saving, a.name) < std::tie(a.saving, b.name);
    });
    // Large batches first; a rejected batch is bisected a few levels.
    std::vector<std::pair<size_t, size_t>> pending;
    const size_t                           batch = std::max<size_t>(64, candidates.size() / 4);
    for (size_t i = 0; i < candidates.size(); i += batch) {
      pending.emplace_back(i, std::min(candidates.size(), i + batch));
    }
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
      const auto [begin, end] = pending[cursor];
      const auto t            = Clock::now();
      ++recover_batches_;
      std::vector<std::string> drivers;
      for (size_t i = begin; i < end; ++i) {
        for (auto& d : nets_of_instance(candidates[i].name)) {
          drivers.push_back(std::move(d));
        }
      }
      const auto pins = touched(drivers);
      const auto e0   = w_.electrical(pins);
      Attempt    a(*this);
      for (size_t i = begin; i < end; ++i) {
        resize(candidates[i].name, candidates[i].to);
      }
      const auto p  = measure();
      const auto e1 = w_.electrical(pins);
      ++recover_.probes;
      recover_.seconds += elapsed(t);
      if (timing_not_worse(p) && electrical_not_worse(e0, e1)) {
        a.commit();
        wns_ = p.wns;
        tns_ = p.tns;
        ++recover_.accepted;
        downsized_ += end - begin;
        continue;
      }
      a.rollback();
      if (end - begin > 8) {
        const auto mid = begin + (end - begin) / 2;
        pending.emplace_back(begin, mid);
        pending.emplace_back(mid, end);
      }
    }
  }

  std::string report(const Resize_plan& plan, double seconds) const {
    const auto counters = [](const Counters& c) {
      return std::format(R"({{"probes":{},"accepted":{},"seconds":{:.3f}}})", c.probes, c.accepted, c.seconds);
    };
    return std::format(
        R"({{"stage":"{}","period_ps":{:.1f},"margin_ps":{:.1f},"whatif_wns_ps":[{:.4f},{:.4f}],"whatif_tns_ps":[{:.4f},{:.4f}],"edits":{},"groups":{},"buffers":{},"upsized":{},"downsized":{},"electrical":{{"nets":{},"fixed":{},"moves":{},"seconds":{:.3f}}},"timing":{{"rounds":{},"endpoints_worked":{},"endpoints_stuck":{},"feasible_paths":{},"infeasible_paths":{},"stop":{},"upsize":{},"isolate":{},"repeater":{},"shed":{},"seconds":{:.3f}}},"recover":{{"batches":{},"moves":{},"seconds":{:.3f}}},"batch":{{"moves":{},"probes":{}}},"probes":{},"probe_seconds":{:.3f},"rejects":{{"slack":{},"wns":{},"tns":{},"electrical":{}}},"generated":{{"upsize":{},"isolate":{},"repeater":{},"shed":{},"none":{}}},"seconds":{:.3f}}})",
        stage_ == Resize_stage::floorplan ? "floorplan" : "placed",
        period_,
        margin_,
        plan.wns_before,
        plan.wns_after,
        plan.tns_before,
        plan.tns_after,
        plan.edits.size(),
        plan.groups,
        plan.buffers,
        plan.upsized,
        plan.downsized,
        electrical_nets_,
        electrical_fixed_,
        counters(electrical_),
        electrical_s_,
        rounds_,
        endpoints_worked_,
        endpoints_stuck_,
        feasible_paths_,
        infeasible_paths_,
        json_string(stop_reason_),
        counters(upsize_),
        counters(isolate_),
        counters(repeater_),
        counters(shed_),
        timing_s_,
        recover_batches_,
        counters(recover_),
        recovery_s_,
        batch_moves_,
        counters(batch_),
        probes_,
        probe_s_,
        reject_slack_,
        reject_wns_,
        reject_tns_,
        reject_electrical_,
        generated_[0],
        generated_[1],
        generated_[2],
        generated_[3],
        no_moves_,
        seconds);
  }
};

double      scaled(const Timing_feedback& f, std::optional<double> v) { return v ? *v * f.time_unit_ps : 0; }
std::string feedback_json(const Timing_feedback& f) {
  return std::format(
      R"({{"wns_ps":{:.4f},"tns_ps":{:.4f},"hold_wns_ps":{},"violations":{},"electrical_excess":{:.6f},"area":{:.6f}}})",
      scaled(f, f.wns),
      scaled(f, f.tns),
      f.hold_wns ? std::format("{:.4f}", *f.hold_wns * f.time_unit_ps) : std::string("null"),
      f.violations,
      f.electrical_excess,
      f.area);
}
// Group-prefix of a plan; prefixes are closed under the dependence of later
// edits on buffers created earlier.
std::vector<Resize_edit> prefix(const std::vector<Resize_edit>& edits, unsigned groups) {
  std::vector<Resize_edit> result;
  for (const auto& e : edits) {
    if (e.group < groups) {
      result.push_back(e);
    }
  }
  return result;
}
std::vector<std::string> changed_names(const std::vector<Resize_edit>& edits) {
  std::set<std::string> names;
  for (const auto& e : edits) {
    names.insert(e.instance);
  }
  return {names.begin(), names.end()};
}
}  // namespace

Resize_plan plan_resize(const Result& state, const Options& options, const Timing_feedback& timing, Whatif& whatif,
                        Resize_stage stage) {
  const auto lib = library(options);
  Planner    planner(state, options, timing, whatif, stage, *lib);
  return planner.run();
}

void apply_resize_edits(hhds::Graph& graph, const Options& options, const std::vector<Resize_edit>& edits,
                        std::map<std::string, std::string>* origins) {
  const auto lib = library(options);
  Node_index nodes;
  for (auto n : graph.body().nodes()) {
    if (!n.get_name().empty()) {
      nodes.emplace(std::string(n.get_name()), n);
    }
  }
  for (const auto& e : edits) {
    if (e.kind == Resize_edit::Kind::resize) {
      auto        n    = nodes.at(e.instance);
      const auto* from = combinational(n, lib->cells);
      if (!from) {
        throw std::runtime_error("resize plan names a non-combinational instance: " + e.instance);
      }
      change_master(n, *from, lib->cells.at(e.master));
      continue;
    }
    const auto& b      = lib->cells.at(e.master);
    const auto  source = physical_pin(graph, e.driver, nodes, true);
    Pin         root   = source.pin;
    if (root.is_invalid() || root.is_const()) {
      throw std::runtime_error("resize plan buffer has no driver: " + e.driver);
    }
    if (source.width > 1) {
      root = gu::create_get_mask(graph, root, source.bit).get_driver_pin(0);
      gu::set_bits(root, 1);
    }
    auto cell = gu::create_typed_node(graph, Ntype_op::Sub);
    cell.set_subnode(buffer_io(graph, b));
    cell.set_name(e.instance);
    if (const auto owner = nodes.find(e.owner); owner != nodes.end()) {
      cell.attr(attrs::color).set(gu::node_color_of(owner->second));
      if (const auto g = owner->second.attr(attrs::synth_color_group); g.has() && !g.get().empty()) {
        cell.attr(attrs::synth_color_group).set(g.get());
      }
      if (const auto blk = owner->second.attr(attrs::placement_block); blk.has() && !blk.get().empty()) {
        cell.attr(attrs::placement_block).set(blk.get());
      }
      if (const auto region = owner->second.attr(attrs::synth_region_id); region.has() && region.get()) {
        cell.attr(attrs::synth_region_id).set(region.get());
      }
    }
    const auto& macro = lib->tech.macros.at(b.name);
    Ann_place   seed(e.position.first - macro.width * lib->tech.dbu / 2,
                   e.position.second - macro.height * lib->tech.dbu / 2,
                   macro.width * lib->tech.dbu,
                   macro.height * lib->tech.dbu);
    seed.status = Ann_place::Status::PLACED;
    cell.attr(attrs::place).set(seed);
    cell.create_sink_pin(b.inputs.front()).connect_driver(root);
    auto out = cell.create_driver_pin(b.output);
    gu::set_bits(out, 1);
    nodes.emplace(e.instance, cell);
    for (const auto& s : e.sinks) {
      connect_bit(graph, physical_pin(graph, s, nodes, false), out);
    }
    if (origins) {
      origins->emplace(e.instance, e.owner);
    }
  }
}

bool resize_accepts(const Timing_feedback& before, const Timing_feedback& after) {
  constexpr double eps = 1e-3;  // ps
  const double     wb = scaled(before, before.wns), wa = scaled(after, after.wns);
  const double     tb = scaled(before, before.tns), ta = scaled(after, after.tns);
  if (!std::isfinite(wa) || !std::isfinite(ta) || wa < wb - eps || ta < tb - eps) {
    return false;
  }
  const bool electrical_better = after.violations < before.violations;
  if (!electrical_better && (after.violations > before.violations || after.electrical_excess > before.electrical_excess + 1e-6)) {
    return false;
  }
  return wa > wb + eps || ta > tb + eps || electrical_better || after.electrical_excess + 1e-6 < before.electrical_excess
         || after.area + 1e-9 < before.area;
}

Repair_result resize_placed(Result initial, const Options& options, const Evaluate& evaluate) {
  if (!options.whatif) {
    throw std::invalid_argument("synth.place.resize.engine=new requires an incremental what-if timer");
  }
  if (!options.full) {
    throw std::invalid_argument("resize requires legal full placement");
  }
  const auto    start     = Clock::now();
  const auto    prior_qor = initial.qor;
  const auto    directory = options.output_dir + "/resize";
  Repair_result result;
  result.placed = std::move(initial);
  fs::create_directories(directory);
  auto        score = evaluate(result.placed);  // synchronizes the what-if timer with this state
  const auto  first = score;
  std::string rounds;
  unsigned    commits = 0;
  for (unsigned round = 0; round < 4; ++round) {
    if (round > 0) {
      score = evaluate(result.placed);
    }
    const auto  round_start = score;
    auto        plan        = plan_resize(result.placed, options, score, *options.whatif, Resize_stage::placed);
    std::string trials;
    bool        accepted = false;
    unsigned    groups   = plan.groups;
    for (unsigned attempt = 0; groups > 0 && attempt < 4; ++attempt) {
      const auto edits = prefix(plan.edits, groups);
      if (edits.empty()) {
        break;
      }
      const auto t = Clock::now();
      ++commits;
      hhds::GraphLibrary draft;
      auto               graph = partition::flatten_hierarchy(result.placed.graph.get(),
                                                &draft,
                                                result.placed.graph->get_name(),
                                                nullptr,
                                                false,
                                                              {},
                                                              {},
                                                true);
      if (!graph) {
        throw std::runtime_error("cannot clone the placed netlist for resize");
      }
      apply_resize_edits(*graph, options, edits);
      auto trial_options           = options;
      trial_options.cache_dir      = {};
      trial_options.legalize_cells = true;
      trial_options.net_weights    = result.placed.net_weights;
      trial_options.previous       = std::make_shared<physical::Floorplan>(result.placed.floorplan);
      trial_options.changed_cells  = changed_names(edits);
      trial_options.output_dir     = std::format("{}/commit_{}", directory, commits);
      trial_options.floorplan      = directory + "/floorplan.def";
      auto frame                   = result.placed.floorplan;
      std::erase_if(frame.components, [](const auto& c) {
        return c.place.status != Ann_place::Status::FIXED && c.place.status != Ann_place::Status::LOCKED;
      });
      for (auto& port : frame.terminals) {
        port.status = Ann_place::Status::FIXED;
      }
      physical::write_def(frame, trial_options.floorplan);
      auto       owner   = std::make_unique<hhds::GraphLibrary>();
      auto       placed  = place(*graph, *owner, trial_options);
      const auto next    = evaluate(placed);
      const bool keep    = resize_accepts(score, next);
      trials            += std::format(R"({}{{"groups":{},"edits":{},"timing":{},"accepted":{},"seconds":{:.3f}}})",
                            trials.empty() ? "" : ",",
                            groups,
                            edits.size(),
                            feedback_json(next),
                            keep,
                            elapsed(t));
      if (keep) {
        result.placed = std::move(placed);
        result.libraries.clear();
        result.libraries.push_back(std::move(owner));
        score    = next;
        accepted = true;
        break;
      }
      groups /= 2;  // keep the earliest (electrical, then most critical) moves
    }
    rounds                += std::format(R"({}{{"round":{},"plan":{},"commits":[{}],"accepted":{}}})",
                          rounds.empty() ? "" : ",",
                          round,
                          plan.report,
                          trials,
                          accepted);
    // Another round re-plans on the legalized result. Stop on lack of progress:
    // nothing proposed or confirmed, met timing, or a round that gained less
    // than 1 ps WNS and 1% TNS.
    const double wns_gain  = scaled(score, score.wns) - scaled(round_start, round_start.wns);
    const double tns_gain  = scaled(score, score.tns) - scaled(round_start, round_start.tns);
    const double tns_ref   = std::abs(scaled(round_start, round_start.tns));
    if (!accepted || plan.edits.empty() || !score.wns || *score.wns >= 0
        || (wns_gain < 1 && tns_gain < std::max(1.0, .01 * tns_ref))) {
      break;
    }
  }
  evaluate(result.placed);  // leave the timer on the accepted state
  result.report = std::format(
      R"({{"engine":"new","model":"placement_rc_path_sizing_buffering","before":{},"after":{},"rounds":[{}],"seconds":{:.3f}}})",
      feedback_json(first),
      feedback_json(score),
      rounds,
      elapsed(start));
  physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  rapidjson::Document qor;
  qor.Parse(result.placed.qor.c_str());
  rapidjson::Document prior;
  prior.Parse(prior_qor.c_str());
  if (qor.IsObject()) {
    if (prior.IsObject()) {
      for (const auto* key : {"timing_driven"}) {
        if (prior.HasMember(key) && !qor.HasMember(key)) {
          qor.AddMember(rapidjson::Value(key, qor.GetAllocator()),
                        rapidjson::Value(prior[key], qor.GetAllocator()),
                        qor.GetAllocator());
        }
      }
    }
    rapidjson::Document report;
    report.Parse(result.report.c_str());
    if (qor.HasMember("resize")) {
      qor.RemoveMember("resize");
    }
    qor.AddMember("resize", rapidjson::Value(report, qor.GetAllocator()), qor.GetAllocator());
    rapidjson::StringBuffer                    out;
    rapidjson::Writer<rapidjson::StringBuffer> qw(out);
    qor.Accept(qw);
    result.placed.qor = out.GetString();
  }
  std::ofstream(options.output_dir + "/qor.json") << result.placed.qor << '\n';
  std::ofstream(options.output_dir + "/resize.json") << result.report << '\n';
  result.timing = std::move(score);
  return result;
}

Floorplan_resize resize_floorplan_engine(Result& state, const Options& options, const Timing_feedback& before,
                                         const Evaluate& evaluate, const std::function<void(Result&)>& refresh) {
  if (!options.whatif) {
    throw std::invalid_argument("synth.place.resize.engine=new requires an incremental what-if timer");
  }
  Floorplan_resize result;
  result.timing    = before;
  auto        plan = plan_resize(state, options, before, *options.whatif, Resize_stage::floorplan);
  std::string trials;
  unsigned    groups = plan.groups;
  for (unsigned attempt = 0; groups > 0 && attempt < 3; ++attempt) {
    const auto edits = prefix(plan.edits, groups);
    if (edits.empty()) {
      break;
    }
    unsigned resized = 0, buffers = 0;
    for (const auto& e : edits) {
      ++(e.kind == Resize_edit::Kind::buffer ? buffers : resized);
    }
    result.proposed        += resized;
    result.repair_proposed += buffers;
    auto library            = std::make_shared<hhds::GraphLibrary>();
    auto graph
        = partition::flatten_hierarchy(state.graph.get(), library.get(), state.graph->get_name(), nullptr, false, {}, {}, true);
    if (!graph) {
      throw std::runtime_error("cannot clone the floorplan netlist for resize");
    }
    std::map<std::string, std::string> origins;
    apply_resize_edits(*graph, options, edits, &origins);
    Result trial     = state;
    trial.fp_library = std::move(library);
    trial.graph      = std::move(graph);
    trial.fp_buffer_origins.insert(origins.begin(), origins.end());
    refresh(trial);
    const auto next  = evaluate(trial);
    const bool keep  = resize_accepts(result.timing, next);
    trials          += std::format(R"({}{{"groups":{},"edits":{},"timing":{},"accepted":{}}})",
                          trials.empty() ? "" : ",",
                          groups,
                          edits.size(),
                          feedback_json(next),
                          keep);
    if (keep) {
      const auto previous_owner = state.fp_library;  // outlive the assignment that drops the old graph
      state                     = std::move(trial);
      result.timing             = next;
      result.accepted           = resized;
      result.repair_accepted    = buffers;
      break;
    }
    refresh(state);
    groups /= 2;
  }
  result.report
      = std::format(R"({{"engine":"new","model":"fp_placement_rc_path_sizing_buffering","plan":{},"commits":[{}],"accepted":{}}})",
                    plan.report,
                    trials,
                    result.accepted + result.repair_accepted);
  return result;
}
}  // namespace livehd::placement
