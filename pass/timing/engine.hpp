// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "netlist.hpp"

// The propagation of the fast timing engine (todo 2q-fasttimer, step C): arrival and slew forward, required
// time backward, for every Liberty corner in one walk of the levelized pins, in single precision.
//
// What it times: delay arcs of combinational cells; rising-edge flip-flops with ideal clocks (clock to
// output, setup and hold checks); primary inputs with an input delay and a transition; primary outputs
// with an output delay and a load. Cell delay and output slew come from the NLDM tables at the input
// slew and the total load of the net (pin capacitances plus wire capacitance, no effective capacitance);
// rise and fall are kept apart. A wire contributes an optional delay and slew term per sink, a load per
// net (Wire_model; no_index means zero wire). Everything it does not time is listed in Report::unsupported.
//
// What it computes: per pin, per corner and transition, the late arrival, slew and required time; the
// early arrival and slew on the hold corner; the endpoint slack; per clock the worst and total negative
// slack and the achievable period; per pin a criticality (see `window_fraction`); the electrical limit
// violations; the worst path.
namespace livehd::timing::fast {

struct Clock {
  std::string name;
  std::string port;  // the input port that is the ideal clock source
  float       period_ps      = 1000;
  float       uncertainty_ps = 0;  // taken off every setup requirement of the clock
  float       slew_ps        = 0;  // the ideal transition at every clock pin
};
struct Input_delay {
  std::string port, clock;  // an empty clock is the first one
  float       delay_ps = 0, slew_ps = 0;
};
struct Output_delay {
  std::string port, clock;
  float       delay_ps = 0, load_ff = 0;
};
struct Constraints {
  std::vector<Clock>        clocks;
  std::vector<Input_delay>  inputs;   // an input port without an entry: arrival 0, default slew
  std::vector<Output_delay> outputs;  // an output port without an entry is not an endpoint
  float                     default_input_slew_ps = 0;
  // False paths: no arrival or required time passes through a cut pin, and an excluded endpoint has no check.
  std::vector<uint32_t>     cut_pins, excluded_endpoints;
  // Items the constraint reader could not honor; copied into the report so that nothing is dropped silently.
  std::vector<std::string>  unsupported;
};
// Interconnect, from the wire trees of step D. Empty vectors are zero.
struct Wire_model {
  std::vector<float> net_cap_ff;     // [net * corners + corner]
  std::vector<float> sink_delay_ps;  // [pin * corners + corner], on the sink pins of a net
  std::vector<float> sink_slew_ps;   // same; added to the driver slew in quadrature
};

struct Options {
  // Corners whose setup is timed (the worst of them rules), as a bit mask; all corners by default.
  uint32_t           setup_corners   = ~0u;
  // The corner of the early walk and the hold check; no_index: the last corner. Hold is only computed by evaluate().
  size_t             hold_corner     = no_index;
  // The corner whose worst-path predecessors are recorded.
  size_t             path_corner     = 0;
  // Criticality falls from 1 on the worst path of a clock to 0 this far (a fraction of the achievable period) away.
  float              window_fraction = 0.10f;
  // rank() leaves the required times against the true requirements instead of rebased ones (one more backward walk).
  bool               true_slack_in_rank = false;
  // Random variation (R14): at an endpoint the arrival grows by margin[corner] / sqrt(logic depth) of itself.
  std::vector<float> depth_margin;
};

struct Clock_report {
  std::string name;
  float       period_ps = 0, wns_ps = 0, tns_ps = 0, achievable_period_ps = 0;
  float       hold_wns_ps = 0, hold_tns_ps = 0;
  size_t      endpoints = 0;
};
struct Corner_report {
  std::string name;
  float       wns_ps = 0, tns_ps = 0;
};
struct Report {
  std::vector<Clock_report>  clocks;
  std::vector<Corner_report> corners;
  float                      wns_ps = 0, tns_ps = 0;
  size_t                     endpoints = 0, unclocked_endpoints = 0, unclocked_flops = 0;
  size_t                     slew_violations = 0, capacitance_violations = 0;
  double                     electrical_excess = 0;
  bool                       hold_computed     = false;
  std::vector<std::string>   unsupported;
};

// A pin beyond its electrical limits after evaluate(): how many limits (slew, capacitance) and the summed excess ratio.
struct Electrical_pin {
  uint32_t pin   = 0;
  uint8_t  count = 0;
  float    excess = 0;
};

struct Path_point {
  uint32_t pin        = 0;
  unsigned transition = 0;
  float    arrival_ps = 0;
};

class Engine {
public:
  Engine(Netlist& netlist, Constraints constraints, Options options = {});

  void set_wire(Wire_model wire);
  // Setup only: late walk, endpoint slacks, criticality. Required times are left rebased (the worst path of
  // each clock and corner has zero slack), which is what criticality is measured on.
  void rank();
  // Everything: rank, then true required times, the hold walk and check, the electrical checks.
  void evaluate();

  [[nodiscard]] const Report& report() const { return report_; }
  [[nodiscard]] size_t        corners() const { return corners_; }
  // Per pin. Arrival -inf: not reached; required +inf: no endpoint downstream.
  [[nodiscard]] float         arrival(uint32_t pin, size_t corner, unsigned t) const { return arrival_[index(pin, corner, t)]; }
  [[nodiscard]] float         slew(uint32_t pin, size_t corner, unsigned t) const { return slew_[index(pin, corner, t)]; }
  [[nodiscard]] float         required(uint32_t pin, size_t corner, unsigned t) const { return required_[index(pin, corner, t)]; }
  // The hold walk's state exists after evaluate(); before it, +inf and 0.
  [[nodiscard]] float         early_arrival(uint32_t pin, unsigned t) const {
    return early_arrival_.empty() ? std::numeric_limits<float>::infinity() : early_arrival_[pin * 2 + t];
  }
  [[nodiscard]] float         early_slew(uint32_t pin, unsigned t) const { return early_slew_.empty() ? 0.f : early_slew_[pin * 2 + t]; }
  [[nodiscard]] uint16_t      depth(uint32_t pin, size_t corner, unsigned t) const { return depth_[index(pin, corner, t)]; }
  // Setup slack over the setup corners and both transitions; +inf when no path passes the pin.
  [[nodiscard]] float         slack(uint32_t pin) const;
  [[nodiscard]] float         criticality(uint32_t pin) const { return criticality_[pin]; }
  // True for a pin of the ideal clock network (clock source to flip-flop clock pins).
  [[nodiscard]] bool          clock_network(uint32_t pin) const { return clock_pin_[pin] != 0; }
  // Hold slack of a data pin (after evaluate); +inf without a check.
  [[nodiscard]] float         hold_slack(uint32_t pin) const {
    return hold_slack_.empty() ? std::numeric_limits<float>::infinity() : hold_slack_[pin];
  }
  // ---- Edit sessions (todo 2q-fasttimer, step G): transient edits on top of an evaluate(), settled
  // incrementally and undone exactly. The late setup state (arrival, slew, depth, required, endpoint
  // slack, loads) is kept up to date; the hold walk and the criticality are not (they are stale until the
  // next evaluate()). Edits are journaled; rollback(mark) restores every value stored after the mark.
  // A mark is a settled state: mark() settles first.
  [[nodiscard]] size_t mark();
  void                 rollback(size_t mark);
  // Undo everything and leave edit mode; the engine is then in the state evaluate() left it.
  void                 end_edits();
  // The same cell with another type of the same interface (Netlist::retype), at the engine's corners.
  void                 resize_cell(uint32_t cell, uint32_t type);
  // Replace the wire of a net: its capacitance per corner, and for each listed sink pin its delay and slew per
  // corner ([pin][corner]). Sinks not listed keep theirs.
  void                 set_net_wire(uint32_t net, std::span<const float> cap_ff, std::span<const uint32_t> sink_pins,
                                    std::span<const float> delay_ps, std::span<const float> slew_ps);
  // A new cell of `type` (a buffer or inverter; not a flip-flop), without connections: connect its input with
  // set_net before its output. Returns its index.
  uint32_t             add_cell(uint32_t type);
  // Make the net driven by `driver` have exactly `sinks`, with its wire (as set_net_wire, for all sinks). The
  // pins keep a valid order: a sink must come after the driver in the design's levelized order, a new cell is
  // placed between the driver and the sinks that used to follow it; otherwise this throws std::domain_error
  // and the netlist is unchanged except for the keys of new pins (the caller rolls back, or evaluates from scratch).
  // Returns the net.
  uint32_t             set_net(uint32_t driver, std::span<const uint32_t> sinks, std::span<const float> cap_ff,
                               std::span<const float> delay_ps, std::span<const float> slew_ps);
  // Propagate the pending edits; every query below does it first.
  void                 settle();
  [[nodiscard]] float  wns();  // worst endpoint slack over the setup corners, +inf without endpoints
  [[nodiscard]] float  tns();  // sum of the negative endpoint slacks
  [[nodiscard]] float  settled_slack(uint32_t pin) {
    settle();
    return slack(pin);
  }
  // Setup slack of the endpoints below a limit, ascending.
  [[nodiscard]] std::vector<std::pair<uint32_t, float>> failing_endpoints(float below_ps);
  // The electrical limits of one pin as evaluate() counts them: {limits exceeded, excess ratio}.
  [[nodiscard]] Electrical_pin                          pin_electrical(uint32_t pin);

  // The pins over a limit (after evaluate), in pin order.
  [[nodiscard]] const std::vector<Electrical_pin>& electrical_pins() const { return electrical_pins_; }
  // Total load of a net in a corner, fF (pin capacitances plus wire).
  [[nodiscard]] float                              net_load_ff(uint32_t net, size_t corner) const { return net_load(net, corner, false); }
  // The endpoint pins, in pin order.
  [[nodiscard]] std::vector<uint32_t>   endpoint_pins() const;
  // The delay of a cell arc in a corner for an (input, output) transition pair; NaN when the pair is not an arc.
  [[nodiscard]] float                   arc_delay(uint32_t cell, uint32_t arc, size_t corner, unsigned in, unsigned out) const;
  // The worst setup path of the path corner ending at `endpoint` (or at the worst endpoint), start first.
  [[nodiscard]] std::vector<Path_point> worst_path(uint32_t endpoint = no_index) const;
  [[nodiscard]] size_t                  bytes() const;

private:
  struct Predecessor {
    uint32_t pin        = no_index;
    uint8_t  transition = 0;
  };
  struct Endpoint {
    uint32_t pin   = 0;
    uint32_t clock = 0;
    bool     port  = false;  // a primary output (otherwise the data pin of a flip-flop)
  };
  [[nodiscard]] size_t index(uint32_t pin, size_t corner, unsigned t) const {
    return (static_cast<size_t>(pin) * corners_ + corner) * 2 + t;
  }

  void                prepare();
  void                mark_clock_network();
  void                compute_loads();
  bool                compute_net_load(uint32_t net);
  void                seed_late();
  void                forward_late();
  void                seed_endpoints(bool rebased);
  void                recompute_arrival(uint32_t pin);
  void                recompute_required(uint32_t pin);
  [[nodiscard]] float endpoint_requirement(size_t endpoint, size_t corner, unsigned transition, bool rebased) const;
  void                backward();
  void                measure_endpoints();
  void                criticality_pass();
  void                forward_early();
  void                check_hold();
  void                check_electrical();
  void                electrical_excess(uint32_t pin, double& slew_excess, double& cap_excess) const;
  [[nodiscard]] float wire_delay(uint32_t pin, size_t corner) const;
  [[nodiscard]] float net_load(uint32_t net, size_t corner, bool early) const;

  Netlist&          netlist_;
  const Cell_types& types_;
  Constraints       constraints_;
  Options           options_;
  size_t            corners_ = 0, hold_corner_ = 0;
  Wire_model        wire_;

  // Per pin.
  std::vector<float>       arrival_, slew_, required_, early_arrival_, early_slew_, hold_slack_, criticality_;
  std::vector<uint16_t>    depth_;
  std::vector<Predecessor> predecessor_;
  std::vector<uint8_t>     clock_pin_;  // 0 data, 1 inside the clock network, 2 a flop's clock pin
  std::vector<uint8_t>     pin_clock_;  // the clock id of a clock-network pin
  // Per net, per corner.
  std::vector<float>       load_, early_load_;
  std::vector<Electrical_pin> electrical_pins_;
  std::vector<uint32_t>    port_input_;  // per cell: the index of its input constraint, no_index without one
  // Delay of every timed arc of every cell: [cell block][arc][corner][combo].
  std::vector<float>       arc_delay_;
  std::vector<uint32_t>    cell_base_, arc_offset_;  // arc_offset_: per arc of the type table, in floats per corner
  std::vector<uint32_t>    type_block_;
  // Per port cell.
  std::vector<float>       port_load_, port_delay_;
  std::vector<Endpoint>    endpoints_;
  std::vector<float>       endpoint_slack_, endpoint_path_slack_;  // worst setup corner; the path corner
  std::vector<uint8_t>     endpoint_path_transition_;
  std::vector<float>       clock_wns_;  // [clock * corners + corner]
  std::vector<uint8_t>     cut_, excluded_;
  bool                     required_rebased_ = false;
  bool                     rebased_          = false;  // the requirements the endpoints were last seeded with
  static constexpr size_t  no_endpoint       = ~size_t{0};
  std::vector<float>       endpoint_required_;  // [endpoint][corner][transition], as seeded
  // The journal of an edit session (see mark/rollback): every stored value keeps its old one.
  enum class Undo_kind : uint8_t {
    arrival, slew, required, depth, predecessor, arc_delay, load, wire_cap, wire_delay, wire_slew, endpoint_required,
    endpoint_slack, cell_type, netlist_edit, grow_cells, grow_nets
  };
  struct Undo {
    Undo_kind kind;
    uint8_t   aux;  // the transition of a predecessor
    uint32_t  old_bits;
    size_t    index;
  };
  bool                     journal_on_ = false;
  std::vector<Undo>        journal_;
  // Edit sessions: dirty pins by position in the order (a forward and a backward heap), flags per pin.
  // The key of a pin orders the walk of an edit session: the position in the levelized order times 2^24 for an
  // existing pin, between its neighbours for a pin of an added cell (unset_key until connected).
  static constexpr uint64_t unset_key = ~uint64_t{0};
  using Heap_entry                    = std::pair<uint64_t, uint32_t>;  // key, pin
  std::vector<uint64_t>    key_;
  std::vector<Heap_entry>  fwd_heap_, bwd_heap_;
  std::vector<uint32_t>    touched_endpoints_;
  std::vector<uint8_t>     queued_, endpoint_touched_;
  bool                     delay_changed_ = false;
  void                     begin_edits();
  void                     shrink_cells(size_t cells, uint32_t pins);
  void                     shrink_nets(size_t nets);
  void                     push_forward(uint32_t pin);
  void                     push_backward(uint32_t pin);
  void                     refresh_endpoint(size_t endpoint);
  void                     touch_endpoint(uint32_t pin);
  [[nodiscard]] size_t     endpoint_of(uint32_t pin) const;
  template <class T>
  void store(std::vector<T>& v, size_t i, T value);
  void store_predecessor(size_t i, Predecessor value);
  Report                   report_;
};

}  // namespace livehd::timing::fast
