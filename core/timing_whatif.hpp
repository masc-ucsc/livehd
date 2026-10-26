// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "physical_rc.hpp"

namespace livehd::timing {
// Transient edits on top of the last fully evaluated physical state of a
// persistent incremental timer. Every edit is recorded; rollback(mark) undoes
// the edits made after mark (LIFO), and the owner discards any edits still
// pending before its next full evaluation. Pins are physical labels
// ("inst:PIN" or a port bit), times are ps and capacitances fF.
class Whatif {
public:
  virtual ~Whatif()                                                                             = default;
  [[nodiscard]] virtual size_t mark() const                                                     = 0;
  virtual void                 rollback(size_t mark)                                            = 0;
  // Same-interface master swap of an existing or what-if instance.
  virtual void                 resize(const std::string& instance, const std::string& master)   = 0;
  // A new, still unconnected instance; connect it with set_net.
  virtual void                 add_cell(const std::string& instance, const std::string& master) = 0;
  // Make net.nodes (pins) exactly the members of the net driven by net.driver and
  // attach its wire RC. A sink moved away from another net leaves that net's RC
  // stale: set_net that net in the same edit too.
  virtual void                 set_net(const physical::Rc_net& net)                             = 0;

  struct Pin_timing {
    std::optional<double> slack, arrival, required, slew;
    std::optional<double> hold_slack{};  // MIN-corner slack in ps
  };
  // Setup (MAX) values; worst over rise/fall.
  virtual double                                      wns() = 0;  // worst endpoint slack (may be positive)
  virtual double                                      tns() = 0;  // sum of negative endpoint slacks, worst check per endpoint
  virtual Pin_timing                                  pin(const std::string& label)      = 0;
  virtual std::optional<double>                       load(const std::string& driver)    = 0;  // driven net load incl. wire
  virtual std::vector<std::pair<std::string, double>> failing_endpoints(double below_ps) = 0;  // ascending slack
  // Liberty slew/capacitance/fanout checks of these pins exactly as the full
  // evaluation counts them: {violations, normalized excess}.
  virtual std::pair<unsigned, double>                 electrical(const std::vector<std::string>& labels) = 0;
  // Liberty model queries, no state change: worst delay and output slew of the
  // from->to arc at the given input slew and output load; input pin capacitance.
  virtual std::optional<std::pair<double, double>>    arc(const std::string& master, const std::string& from, const std::string& to,
                                                          double slew_ps, double load_ff)
      = 0;
  virtual std::optional<double> pin_capacitance(const std::string& master, const std::string& pin) = 0;
  // Shortest constrained clock period (ps); 0 without clocks.
  virtual double                clock_period()                                                     = 0;
};
}  // namespace livehd::timing
