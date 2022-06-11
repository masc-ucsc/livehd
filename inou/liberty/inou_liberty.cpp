//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ot/timer/timer.hpp"

#include "inou_liberty.hpp"

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

// WARNING: opentimer has a nasty "define has_member" that overlaps with perfetto methods
#undef has_member
#include "perf_tracing.hpp"

static Pass_plugin sample("inou_liberty", Inou_liberty::setup);

void Inou_liberty::setup() {

  Eprp_method m2("inou.liberty", "Read liberty and populate blackboxes", &Inou_liberty::liberty_open);

  register_pass(m2);
}

Inou_liberty::Inou_liberty(const Eprp_var &var) : Pass("pass.opentimer", var) {
}

void Inou_liberty::liberty_open(Eprp_var &var) {

  TRACE_EVENT("inou", "INOUT_liberty");
  Lbench b("inou.liberty");

  Inou_liberty p(var);

  auto *library = Graph_library::instance(p.path);

  for (const auto f : absl::StrSplit(p.files, ',')) {
    fmt::print("reading liberty {}\n", f);

    ot::Timer timer;

    timer.read_celllib(f, ot::MAX);

    timer.update_timing();

    const auto &cl=timer.celllib(ot::MAX);
    for(const auto &it:cl->cells) {
      auto *sub = library->ref_or_create_sub(it.first);
      sub->reset_pins();

      bool has_clock = false;
      for(const auto &pin:it.second.cellpins) {
        if (pin.second.is_clock && *pin.second.is_clock) {
          sub->set_loop_last();
          has_clock = true;
        }
        if (pin.second.direction) {
          Port_ID pid=0;
          if (*pin.second.direction == ot::CellpinDirection::INPUT)
            pid = sub->add_input_pin(pin.first);
          else if (*pin.second.direction == ot::CellpinDirection::OUTPUT)
            pid = sub->add_output_pin(pin.first);

          if (pid)
            sub->set_bits(pid, 1); // how does the liberty specify multiple bits?
        }

        if (pin.second.is_clock)
          has_clock |= *pin.second.is_clock;
      }

      if (!has_clock)
        sub->clear_loop_last();
    }
  }
}

