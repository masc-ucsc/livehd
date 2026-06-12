// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "graphviz.hpp"

#include <fcntl.h>

#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <utility>

#include "RGB.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "pass.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::color_of;
using livehd::graph_util::debug_name;
using livehd::graph_util::has_color;
using livehd::graph_util::has_name;
using livehd::graph_util::hydrate_const;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

Graphviz::Graphviz(bool _bits, bool _verbose, std::string_view _odir) : verbose(_verbose), odir(_odir) { (void)_bits; }

void Graphviz::save_graph(std::string_view name, std::string_view dot_postfix, const std::string& data) {
  auto file = absl::StrCat(odir, "/", name);

  if (dot_postfix.empty()) {
    absl::StrAppend(&file, ".dot");
  } else {
    absl::StrAppend(&file, ".", dot_postfix, ".dot");
  }

  int fd = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    livehd::diag::err("inou.graphviz", "write-failed", "io").msg("unable to create {}", file).fatal();
    return;
  }
  size_t sz = write(fd, data.c_str(), data.size());
  if (sz != data.size()) {
    livehd::diag::err("inou.graphviz", "write-failed", "io").msg("unexpected write missmatch").fatal();
    return;
  }

  close(fd);
}

void Graphviz::populate_lg_handle_xedge(const hhds::Node_class& node, const hhds::Edge_class& out, std::string& data,
                                        bool verbose) {
  std::string dp_pid;
  std::string sp_pid;
  if (verbose) {
    dp_pid = graphviz_legalize_name(pin_name_of(out.driver));
    sp_pid = graphviz_legalize_name(pin_name_of(out.sink));
  } else {
    dp_pid = graphviz_legalize_name(std::to_string(out.driver.get_port_id()));
    sp_pid = graphviz_legalize_name(std::to_string(out.sink.get_port_id()));
  }

  std::string dn_name;
  if (is_graph_input_pin(out.driver) || is_graph_output_pin(out.driver)) {
    dn_name = graphviz_legalize_name(out.driver.get_pin_name());
  } else {
    dn_name = graphviz_legalize_name(debug_name(out.driver.get_master_node()));
  }

  std::string sn_name;
  if (is_graph_input_pin(out.sink) || is_graph_output_pin(out.sink)) {
    sn_name = graphviz_legalize_name(out.sink.get_pin_name());
  } else {
    sn_name = graphviz_legalize_name(debug_name(out.sink.get_master_node()));
  }

  auto dbits   = bits_of(out.driver);
  auto pn      = pin_name_of(out.driver);
  auto dp_name = graphviz_legalize_name(!pn.empty() ? pn : std::string_view{"unk"});

  if (is_const_pin(out.driver) || type_op_of(node) == Ntype_op::Nconst) {
    data += std::format(" {} -> {} [ label = <{}b:({},{})> ];\n", dn_name, sn_name, dbits, dp_pid, sp_pid);
  } else {
    data += std::format(" {} -> {} [ label = <{}b:({},{}):{}> ];\n", dn_name, sn_name, dbits, dp_pid, sp_pid, dp_name);
  }
}

std::string Graphviz::graphviz_legalize_name(std::string_view name) {
  std::string legal;
  for (auto c : name) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      legal.append(1, c);
    } else if (c == 32) {
      legal += " ";
    } else if (c == 35) {
      legal += "#";
    } else if (c == 37) {
      legal += "_percent_";
    } else if (c == 36) {
      legal += "_dollar_";
    } else if (c == 58) {
      legal += "_colon_";
    } else if (c == 95) {
      legal += "_";
    } else {
      legal += "_c" + std::to_string(c) + "_";
    }
  }

  return legal;
}

void Graphviz::do_hierarchy(hhds::Graph* lg) {
  // HHDS hierarchy traversal is via the structure-tree on the Graph; for the
  // diagnostic dump we just enumerate Sub nodes on this graph and emit one
  // edge from this graph's name to each immediate sub-instance's graph name.
  // Deep hierarchy walks use Graph::hier_range() but this entrypoint only
  // diagrams the immediate level.
  std::string data = "digraph {\n node [fontname = \"Source Code Pro\"];\n";

  auto gio = lg->get_io();
  if (!gio) {
    data += "\n}\n";
    save_graph(std::string{}, "hier", data);
    return;
  }

  absl::flat_hash_set<std::pair<std::string, std::string>> added;
  auto                                                     parent_name = std::string{gio->get_name()};

  for (auto node : lg->fast_class()) {
    if (type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    auto sub_io = node.get_subnode_io();
    if (!sub_io) {
      continue;
    }
    auto child_name = std::string{sub_io->get_name()};
    if (child_name == parent_name) {
      continue;
    }
    auto p = std::pair{parent_name, child_name};
    if (added.contains(p)) {
      continue;
    }
    added.insert(p);
    data += std::format(" {} -> {};\n", graphviz_legalize_name(parent_name), graphviz_legalize_name(child_name));
  }

  data += "\n}\n";
  save_graph(parent_name, "hier", data);
}

void Graphviz::create_color_map(hhds::Graph* lg) {
  absl::flat_hash_map<int, size_t> color2id;

  for (auto node : lg->fast_class()) {
    if (!has_color(node)) {
      continue;
    }
    auto c = color_of(node);
    if (color2id.contains(c)) {
      continue;
    }
    color2id.insert({c, color2id.size()});
  }

  std::string data = "digraph {\n";

  color2rgb.clear();
  for (auto e : color2id) {
    RGB color(static_cast<double>(e.second) / color2id.size());
    color2rgb[e.first] = color.to_s();
    data += std::format(" c{} [ label = <{}> , style = \"filled\" , fillcolor = \"{}\" ];\n", e.second, e.first, color.to_s());
  }

  absl::flat_hash_set<uint64_t> edges;  // hackish graph
  for (auto node : lg->fast_class()) {
    if (!has_color(node)) {
      continue;
    }
    auto c = color_of(node);
    for (auto e : node.out_edges()) {
      auto snode = e.sink.get_master_node();
      if (!has_color(snode)) {
        continue;
      }
      auto sc = color_of(snode);
      if (sc == c) {
        continue;
      }

      uint64_t edge   = static_cast<uint64_t>(c);
      edge          <<= 32;
      edge           |= static_cast<uint32_t>(sc);

      if (edges.contains(edge)) {
        continue;
      }

      data += std::format(" c{} -> c{};\n", c, sc);
      edges.insert(edge);
    }
  }

  data += "}\n";

  auto gio  = lg->get_io();
  auto name = gio ? std::string{gio->get_name()} : std::string{};
  save_graph(name, "color_map", data);
}

void Graphviz::do_from_lgraph(hhds::Graph* lg_parent, std::string_view dot_postfix) {
  create_color_map(lg_parent);

  populate_lg_data(lg_parent, dot_postfix);

  // Walk immediate sub-instances and dump their bodies too. We iterate Sub
  // nodes once and follow each subnode_graph(); HHDS's set_subnode wiring
  // gives us the body directly.
  absl::flat_hash_set<hhds::Gid> visited;
  for (auto node : lg_parent->fast_class()) {
    if (type_op_of(node) != Ntype_op::Sub) {
      continue;
    }
    auto child = node.get_subnode_graph();
    if (!child) {
      continue;
    }
    auto child_io = child->get_io();
    if (!child_io || !visited.insert(child_io->get_gid()).second) {
      continue;
    }
    populate_lg_data(child.get(), dot_postfix);
  }
}

void Graphviz::populate_lg_data(hhds::Graph* g, std::string_view dot_postfix) {
  std::string data = "digraph {\n";

  for (auto node : g->fast_class()) {
    if (node.inp_edges().empty() && node.out_edges().empty()) {
      continue;
    }
    std::string node_info;
    auto        dn = debug_name(node);
    if (!verbose) {
      auto pos  = dn.find("_lg");
      node_info = dn.substr(0, pos);
      node_info = graphviz_legalize_name(std::regex_replace(node_info, std::regex("node_"), "n"));
    } else {
      node_info = graphviz_legalize_name(dn);
    }

    auto        gv_name = graphviz_legalize_name(dn);
    std::string color;
    if (has_color(node)) {
      const auto it = color2rgb.find(color_of(node));
      if (it != color2rgb.end()) {
        color = absl::StrCat("fillcolor = \"", it->second, "\" ");
      }
    }
    if (type_op_of(node) == Ntype_op::Nconst) {
      auto v  = hydrate_const(node);
      data   += std::format(" {} [ {} label = <{}:{}> ];\n", gv_name, color, node_info, v.to_pyrope());
    } else {
      data += std::format(" {} [ {} label = <{}> ];\n", gv_name, color, node_info);
    }

    for (const auto& out : node.out_edges()) {
      populate_lg_handle_xedge(node, out, data, verbose);
    }

    if (verbose) {
      // CONST_NODE is a builtin singleton skipped by fast_class(), so const
      // edges are invisible from the driver side. Show them from the sink.
      for (const auto& inp : node.inp_edges()) {
        if (!is_const_pin(inp.driver)) {
          continue;
        }
        auto v  = hydrate_const(inp.driver);
        data   += std::format(" const_{} [ label = <{}> ];\n",
                              graphviz_legalize_name(v.to_pyrope()),
                              graphviz_legalize_name(v.to_pyrope()));
        data   += std::format(" const_{} -> {} [ label = <{}b:(,{})> ];\n",
                              graphviz_legalize_name(v.to_pyrope()),
                              gv_name,
                              bits_of(inp.driver),
                              graphviz_legalize_name(pin_name_of(inp.sink)));
      }
    }
  }

  auto gio = g->get_io();
  if (gio) {
    for (const auto& decl : gio->get_input_pin_decls()) {
      auto pin      = g->get_input_pin(decl.name);
      auto io_name  = graphviz_legalize_name(decl.name);
      data         += std::format(" {} [ label = <{}> ];\n", io_name, io_name);
      for (const auto& out : pin.out_edges()) {
        populate_lg_handle_xedge(pin.get_master_node(), out, data, verbose);
      }
    }
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto             pin      = g->get_output_pin(decl.name);
      std::string_view dst_str  = "virtual_dst_module";
      auto             dbits    = bits_of(pin);
      data                     += std::format(" {} -> {} [ label = <{}b> ];\n", graphviz_legalize_name(decl.name), dst_str, dbits);
      for (const auto& out : pin.out_edges()) {
        populate_lg_handle_xedge(pin.get_master_node(), out, data, verbose);
      }
      if (verbose) {
        for (const auto& inp : pin.inp_edges()) {
          if (!is_const_pin(inp.driver)) {
            continue;
          }
          auto v  = hydrate_const(inp.driver);
          data   += std::format(" const_{} [ label = <{}> ];\n",
                                graphviz_legalize_name(v.to_pyrope()),
                                graphviz_legalize_name(v.to_pyrope()));
          data   += std::format(" const_{} -> {} [ label = <{}b> ];\n",
                                graphviz_legalize_name(v.to_pyrope()),
                                graphviz_legalize_name(decl.name),
                                bits_of(inp.driver));
        }
      }
    }
  }

  data += "}\n";

  auto name = gio ? std::string{gio->get_name()} : std::string{};
  save_graph(name, dot_postfix, data);
}

void Graphviz::do_from_lnast(const std::shared_ptr<Lnast>& lnast, std::string_view dot_postfix) {
  std::string data = "digraph {\n";

  for (const auto& itr : lnast->depth_preorder()) {
    auto type = lnast->get_type(itr);
    auto name = lnast->get_name(itr);

    auto id  = std::to_string(level_of(itr)) + std::to_string(pos_of(itr));
    data    += std::format(" {} [ label = <{}, {}> ];\n", id, Lnast_ntype::debug_name(type), name);

    if (Lnast_ntype::is_top(type)) {
      continue;
    }

    auto p = lnast->get_parent(itr);

    auto parent_id  = std::to_string(level_of(p)) + std::to_string(pos_of(p));
    data           += std::format(" {} -> {};\n", parent_id, id);
  }

  data += "}\n";

  if (!dot_postfix.empty()) {
    save_graph(lnast->get_top_module_name(), absl::StrCat("lnast", ".", dot_postfix), data);
  } else {
    save_graph(lnast->get_top_module_name(), "lnast", data);
  }
}
