// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
// Shared by the final-cell placer, boundary refinement and legroute. Callbacks
// belong to each advance(), never to this owner (their captures are stack-local).
class Placement_session {
public:
  struct Snapshot {
    DreamPlace::native::GpState optimizer;
    std::vector<std::string>    identities;
    DreamPlace::native::Design  physical;  // caller geometry, without private halo padding
  };
  struct Event {
    std::string phase;
    int         begin = 0, end = 0;
    size_t      fillers = 0;
    bool        rebind = false, restore = false;
    double      density_weight          = 0;
    bool        density_reset           = false;
    bool        macro_replan            = false;
    bool        wirelength_restart      = false;
    int         wirelength_epoch        = 0;
    bool        routing_inflation_reset = false;
  };

private:
  Snapshot           current;
  std::vector<Event> events;
  unsigned           starts = 0;
  unsigned long long spent  = 0;

public:
  DreamPlace::native::Result advance(DreamPlace::native::Design&, const DreamPlace::native::Options&,
                                     const std::vector<std::string>& identities, std::string phase, unsigned minimum_steps = 0,
                                     const std::function<std::vector<std::string>()>& updated_identities = {},
                                     bool macro_replan = false, bool recover = false);
  void     synchronize(DreamPlace::native::Design&, const DreamPlace::native::Options&, const std::vector<std::string>& identities,
                       std::string phase);
  size_t   seed_movable(DreamPlace::native::Design&, const std::vector<std::string>& identities) const;
  Snapshot snapshot() const { return current; }
  // The last successfully advanced physical frame, without copying optimizer
  // particles or connectivity. An uninitialized session has no retained frame.
  std::optional<DreamPlace::native::Layout> current_layout() const {
    if (!live()) {
      return std::nullopt;
    }
    return current.physical.layout;
  }
  void               restore(const Snapshot&);
  bool               live() const { return current.optimizer.signature != 0; }
  int                iteration() const { return current.optimizer.iteration; }
  const auto&        history() const { return events; }
  unsigned           cold_starts() const { return starts; }
  unsigned long long spent_iterations() const { return spent; }
  void               write(const std::string& path) const;
};
}  // namespace livehd::placement
