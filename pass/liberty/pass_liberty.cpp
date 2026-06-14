// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.liberty gensim — read a Liberty file (via ABC's read_lib) and emit one
// LGraph behavioral model per combinational cell. Each model is a Graph named
// exactly after the Liberty/Mio cell, with input pins = Mio pin names and the
// gate output pin, its function lowered from the cell SOP (Mio_GateReadSop) to
// And/Or/Not. These resolve the blackbox cell Subs emitted by pass.abc for LEC.

#include "pass_liberty.hpp"

#include <algorithm>
#include <cctype>
#include <print>
#include <string>
#include <vector>

#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

extern "C" {
#include "base/abc/abc.h"
#include "base/main/abcapis.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "map/mio/mio.h"
}

namespace gu = livehd::graph_util;

static Pass_plugin sample("pass_liberty", Pass_liberty::setup);

Pass_liberty::Pass_liberty(const Eprp_var& var) : Pass("pass.liberty", var) {}

void Pass_liberty::setup() {
  Eprp_method m("pass.liberty", "Emit an LGraph behavioral model per combinational Liberty cell (gensim)", &Pass_liberty::gensim);
  m.add_label_optional("files", "Liberty .lib file to read", "");
  m.add_label_optional("out", "output graph_library directory (the --emit-dir lg: slot)", "");
  m.add_label_optional("verbose", "report per-cell modeling", "false");
  register_pass(m);
}

namespace {

// One SOP cube: characters '0'/'1'/'-' per input pin (in pin order).
struct Cube {
  std::string lits;  // length == n_inputs
  bool        on = true;  // output value for this cube ('1' => on-set)
};

std::vector<Cube> parse_sop(const char* sop, int n_inputs) {
  std::vector<Cube> cubes;
  if (sop == nullptr) {
    return cubes;
  }
  std::string s{sop};
  size_t      i = 0;
  while (i < s.size()) {
    size_t eol = s.find('\n', i);
    if (eol == std::string::npos) {
      eol = s.size();
    }
    std::string line = s.substr(i, eol - i);
    i                = eol + 1;
    // Split on whitespace into [cube?, out].
    std::vector<std::string> tok;
    size_t                   p = 0;
    while (p < line.size()) {
      while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) {
        ++p;
      }
      size_t q = p;
      while (q < line.size() && !std::isspace(static_cast<unsigned char>(line[q]))) {
        ++q;
      }
      if (q > p) {
        tok.push_back(line.substr(p, q - p));
      }
      p = q;
    }
    if (tok.empty()) {
      continue;
    }
    Cube c;
    if (tok.size() == 1) {        // 0-input cell: just the output char
      c.lits = std::string(static_cast<size_t>(std::max(0, n_inputs)), '-');
      c.on   = tok[0] == "1";
    } else {
      c.lits = tok[0];
      c.on   = tok.back() == "1";
    }
    cubes.push_back(std::move(c));
  }
  return cubes;
}

bool truthy(std::string_view v) { return v != "false" && v != "0" && v != ""; }

// Build one cell model graph into `outlib`. Returns true if modeled.
bool model_cell(hhds::GraphLibrary& outlib, Mio_Gate_t* g) {
  std::string              cell{Mio_GateReadName(g)};
  std::vector<std::string> pins;
  for (auto* pin = Mio_GateReadPins(g); pin != nullptr; pin = Mio_PinReadNext(pin)) {
    pins.emplace_back(Mio_PinReadName(pin));
  }
  std::string out_name{Mio_GateReadOutName(g)};
  auto        cubes = parse_sop(Mio_GateReadSop(g), static_cast<int>(pins.size()));
  if (cubes.empty()) {
    return false;  // sequential / unparseable: skip (flops stay native in the netlist)
  }

  if (outlib.find_io(cell)) {
    return false;  // already modeled (dedup across calls)
  }
  auto io = outlib.create_io(cell);
  hhds::Port_id pid = 1;
  for (const auto& p : pins) {
    io->add_input(p, pid++);
    io->set_bits(p, 1);
    io->set_unsign(p, true);
  }
  io->add_output(out_name, pid++);
  io->set_bits(out_name, 1);
  io->set_unsign(out_name, true);

  auto body = io->create_graph();

  auto one_bit = [&](hhds::Pin_class d) {
    gu::set_bits(d, 1);
    gu::set_unsign(d);
    return d;
  };
  auto inv = [&](hhds::Pin_class a) {
    auto n = gu::create_typed_node(*body, Ntype_op::Not);
    a.connect_sink(gu::setup_sink_by_name(n, "a"));
    return one_bit(n.create_driver_pin(0));
  };
  auto reduce = [&](const std::vector<hhds::Pin_class>& ins, Ntype_op op) -> hhds::Pin_class {
    if (ins.empty()) {
      return gu::create_const(*body, *Dlop::create_integer(op == Ntype_op::And ? 1 : 0));
    }
    if (ins.size() == 1) {
      return ins[0];
    }
    auto n = gu::create_typed_node(*body, op);
    for (const auto& d : ins) {
      d.connect_sink(n.create_sink_pin(0));
    }
    return one_bit(n.create_driver_pin(0));
  };

  // Split cubes by phase; prefer the on-set, else complement the off-set.
  std::vector<Cube> onset;
  std::vector<Cube> offset;
  for (auto& c : cubes) {
    (c.on ? onset : offset).push_back(c);
  }
  bool               complement = onset.empty();
  std::vector<Cube>& use        = complement ? offset : onset;

  std::vector<hhds::Pin_class> products;
  for (auto& c : use) {
    std::vector<hhds::Pin_class> lits;
    for (size_t k = 0; k < pins.size() && k < c.lits.size(); ++k) {
      if (c.lits[k] == '-') {
        continue;
      }
      auto ipin = body->get_input_pin(pins[k]);
      lits.push_back(c.lits[k] == '1' ? ipin : inv(ipin));
    }
    products.push_back(reduce(lits, Ntype_op::And));  // empty => const 1 (tautology cube)
  }
  hhds::Pin_class f = reduce(products, Ntype_op::Or);
  if (complement) {
    f = inv(f);
  }

  f.connect_sink(body->get_output_pin(out_name));
  body->commit();
  return true;
}

}  // namespace

void Pass_liberty::gensim(Eprp_var& var) {
  auto files   = std::string{var.get("files", "")};
  auto out     = std::string{var.get("out", "")};
  bool verbose = truthy(var.get("verbose", "false"));

  if (files.empty()) {
    livehd::diag::err("pass.liberty", "no-file", "unsupported").msg("pass.liberty gensim needs a Liberty .lib file argument").fatal();
    return;
  }
  if (out.empty()) {
    livehd::diag::err("pass.liberty", "no-out", "unsupported")
        .msg("pass.liberty gensim needs --emit-dir lg:DIR for the model library")
        .fatal();
    return;
  }

  Abc_Start();
  auto* frame = Abc_FrameGetGlobalFrame();
  auto  cmd   = std::string{"read_lib "} + files;
  if (Cmd_CommandExecute(frame, cmd.c_str()) != 0) {
    livehd::diag::err("pass.liberty", "read-lib", "unsupported").msg("ABC could not read the Liberty library '{}'", files).fatal();
    Abc_Stop();
    return;
  }
  auto* lib = static_cast<Mio_Library_t*>(Abc_FrameReadLibGen());
  if (lib == nullptr) {
    livehd::diag::err("pass.liberty", "no-lib", "unsupported").msg("ABC loaded no standard-cell library from '{}'", files).fatal();
    Abc_Stop();
    return;
  }

  auto&       outlib = livehd::Hhds_graph_library::instance(out);
  int         modeled = 0;
  int         skipped = 0;
  Mio_Gate_t* g       = nullptr;
  Mio_LibraryForEachGate(lib, g) {
    if (model_cell(outlib, g)) {
      ++modeled;
    } else {
      ++skipped;
    }
  }
  Abc_Stop();
  if (verbose) {
    std::print("[pass.liberty] gensim: {} cells modeled, {} skipped from '{}'\n", modeled, skipped, files);
  }
}
