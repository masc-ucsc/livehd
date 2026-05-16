//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_liberty.hpp"

#include <format>

#include "absl/strings/str_split.h"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "ot/timer/timer.hpp"

// WARNING: opentimer has a nasty "define has_member" that overlaps with perfetto methods
#undef has_member
#include "perf_tracing.hpp"

using livehd::Hhds_graph_library;

static Pass_plugin sample("inou_liberty", Inou_liberty::setup);

void Inou_liberty::setup() {
  Eprp_method m2("inou.liberty", "Read liberty and populate blackboxes", &Inou_liberty::liberty_open);

  register_pass(m2);
}

Inou_liberty::Inou_liberty(const Eprp_var& var) : Pass("pass.opentimer", var) {}

void Inou_liberty::liberty_open(Eprp_var& var) {
  TRACE_EVENT("inou", "INOUT_liberty");

  Inou_liberty p(var);

  auto& library = Hhds_graph_library::instance(p.path);

  for (const auto f : absl::StrSplit(p.files, ',')) {
    std::print("reading liberty {}\n", f);

    ot::Timer timer;

    timer.read_celllib(f, ot::MAX);

    timer.update_timing();

    const auto& cl = timer.celllib(ot::MAX);
    for (const auto& it : cl->cells) {
      // Find-or-create the GraphIO that represents this Liberty cell. Each
      // pin in the cell becomes an input/output declaration; bits stay at 1
      // (Liberty has no native multi-bit pin concept).
      auto gio = library.find_io(it.first);
      if (!gio) {
        gio = library.create_io(it.first);
      }
      gio->reset_declarations();

      hhds::Port_id next_in_pid  = 0;
      hhds::Port_id next_out_pid = 0;
      bool          has_clock    = false;
      for (const auto& pin : it.second.cellpins) {
        bool is_clk = pin.second.is_clock && *pin.second.is_clock;
        if (pin.second.direction) {
          if (*pin.second.direction == ot::CellpinDirection::INPUT) {
            gio->add_input(pin.first, next_in_pid, /*loop_last=*/is_clk);
            gio->set_bits(pin.first, 1);
            ++next_in_pid;
          } else if (*pin.second.direction == ot::CellpinDirection::OUTPUT) {
            gio->add_output(pin.first, next_out_pid, /*loop_last=*/is_clk);
            gio->set_bits(pin.first, 1);
            ++next_out_pid;
          }
        }

        if (is_clk) {
          has_clock = true;
        }
      }
      (void)has_clock;  // Liberty cells with no clock pin are pure-combinational; HHDS's per-pin loop_last flag captures the
                       // clock-pin polarity directly.
    }
  }
}
