// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Saved tiny graphs for differential export, independent of RTL elaboration.
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "graph_library_singleton.hpp"
#include "node_util.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: lean_legacy_fixtures LGDB\n";
    return 2;
  }
  std::filesystem::create_directories(argv[1]);
  auto& lib = livehd::Hhds_graph_library::instance(argv[1]);
  using namespace livehd::graph_util;
  const std::vector<std::pair<std::string, Ntype_op>> ops = {
      {        "sum",      Ntype_op::Sum},
      {       "mult",     Ntype_op::Mult},
      {        "div",      Ntype_op::Div},
      {        "and",      Ntype_op::And},
      {         "or",       Ntype_op::Or},
      {        "xor",      Ntype_op::Xor},
      {        "ror",      Ntype_op::Ror},
      {         "eq",       Ntype_op::EQ},
      {        "not",      Ntype_op::Not},
      {         "lt",       Ntype_op::LT},
      {         "gt",       Ntype_op::GT},
      {        "shl",      Ntype_op::SHL},
      {        "sra",      Ntype_op::SRA},
      {        "mux",      Ntype_op::Mux},
      {       "sext",     Ntype_op::Sext},
      {    "getmask", Ntype_op::Get_mask},
      {    "setmask", Ntype_op::Set_mask},
      {"unsupported",      Ntype_op::LUT}
  };
  for (const auto& [name, op] : ops) {
    const auto top = "tiny_" + name;
    auto       io  = lib.create_io(top);
    io->add_input("a", 1);
    io->set_bits("a", 8);
    io->add_output("y", 1);
    const auto width = op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::Ror ? 1 : 8;
    io->set_bits("y", width);
    auto g = io->create_graph();
    for (const auto& decl : io->get_input_pin_decls()) {
      auto pin = g->get_input_pin(decl.name);
      set_bits(pin, bits_of(pin, *io, decl.name));
    }
    auto n = create_typed_node(*g, op);
    set_bits(n.create_driver_pin(0), width);
    auto constant
        = [&](int port, int value) { create_const(*g, *Dlop::create_integer(value)).connect_sink(n.create_sink_pin(port)); };
    if (op == Ntype_op::Mux) {
      constant(0, 1);
      g->get_input_pin("a").connect_sink(n.create_sink_pin(1));
      constant(2, 7);
    } else {
      g->get_input_pin("a").connect_sink(n.create_sink_pin(0));
      if (op != Ntype_op::Not && op != Ntype_op::Ror) {
        constant(1, op == Ntype_op::Get_mask ? -1 : 3);
      }
      if (op == Ntype_op::Set_mask) {
        constant(2, 2);
      }
    }
    n.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    std::cout << top << '\n';
  }
  for (const std::string kind : {"reset", "async", "active_low", "nonzero", "negedge", "pipeline"}) {
    const auto top = "tiny_" + kind;
    auto       io  = lib.create_io(top);
    for (const auto& [name, port] : std::vector<std::pair<std::string, int>>{
             {"rst", 1},
             { "en", 2}
    }) {
      io->add_input(name, port);
      io->set_bits(name, 1);
    }
    io->add_output("q", 1);
    io->set_bits("q", 8);
    auto g = io->create_graph();
    for (const auto& decl : io->get_input_pin_decls()) {
      auto pin = g->get_input_pin(decl.name);
      set_bits(pin, bits_of(pin, *io, decl.name));
    }
    auto f = create_typed_node(*g, Ntype_op::Flop);
    set_bits(f.create_driver_pin(0), 8);
    auto pin    = [&](std::string_view name) { return f.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, name)); };
    auto policy = [&](std::string_view name, int value) { create_const(*g, *Dlop::create_integer(value)).connect_sink(pin(name)); };
    policy("din", 12);
    policy("initial", kind == "nonzero" ? 7 : 0);
    policy("async", kind == "async" ? 1 : 0);
    policy("negreset", kind == "active_low" ? 1 : 0);
    policy("posclk", kind == "negedge" ? 0 : 1);
    policy("pipe_max", kind == "pipeline" ? 2 : 1);
    g->get_input_pin("rst").connect_sink(pin("reset_pin"));
    g->get_input_pin("en").connect_sink(pin("enable"));
    f.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    std::cout << top << '\n';
  }
  for (const std::string kind : {"ram",
                                 "sync_ram",
                                 "byte_ram",
                                 "forward_ram",
                                 "rom",
                                 "sync_rom",
                                 "ram_init",
                                 "whole_array",
                                 "undef",
                                 "unknown_fwd",
                                 "bad_type",
                                 "bad_wensize",
                                 "array_init",
                                 "dynamic_init"}) {
    const auto top = "tiny_" + kind;
    auto       io  = lib.create_io(top);
    io->add_input("addr", 1);
    io->set_bits("addr", 2);
    io->add_input("en", 2);
    io->set_bits("en", 2);
    io->add_output("q", 1);
    io->set_bits("q", 8);
    auto g = io->create_graph();
    for (const auto& decl : io->get_input_pin_decls()) {
      auto pin = g->get_input_pin(decl.name);
      set_bits(pin, bits_of(pin, *io, decl.name));
    }
    auto m = create_typed_node(*g, Ntype_op::Memory);
    set_bits(m.create_driver_pin(0), 8);
    auto pin = [&](std::string_view name, int port = 0) {
      return m.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name) + port * 16);
    };
    auto policy = [&](std::string_view name, int value, int port = 0) {
      create_const(*g, *Dlop::create_integer(value)).connect_sink(pin(name, port));
    };
    const bool immutable = kind == "rom" || kind == "sync_rom" || kind == "array_init" || kind == "dynamic_init";
    policy("bits", 8);
    policy("size", 4);
    policy("type", kind == "bad_type" ? 3 : kind == "array_init" ? 2 : kind.starts_with("sync") ? 1 : 0);
    policy("wensize", kind == "bad_wensize" ? 3 : kind == "byte_ram" ? 2 : 1);
    policy("rdport", 1);
    g->get_input_pin("addr").connect_sink(pin("addr"));
    g->get_input_pin("en").connect_sink(pin("enable"));
    if (!immutable) {
      policy("rdport", 0, 1);
      g->get_input_pin("addr").connect_sink(pin("addr", 1));
      g->get_input_pin("en").connect_sink(pin("enable", 1));
      policy("din", 171, 1);
    }
    if (immutable || kind == "ram_init") {
      if (kind == "dynamic_init") {
        g->get_input_pin("addr").connect_sink(pin("initial"));
      } else {
        policy("initial", 0x04030201);
      }
    }
    if (kind == "whole_array") {
      policy("update", 0);
    }
    if (kind == "undef") {
      policy("undef", 1);
    }
    if (kind == "unknown_fwd") {
      g->get_input_pin("en").connect_sink(pin("fwd"));
    } else {
      policy("fwd", kind == "forward_ram" ? 1 : 0);
    }
    m.create_driver_pin(immutable ? 0 : 1).connect_sink(g->get_output_pin("q"));
    std::cout << top << '\n';
  }
  livehd::Hhds_graph_library::save(argv[1]);
}
