//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Definitions for `lean_common.hpp`.  Moved verbatim out of pass_lean.cpp's
// anonymous namespace: these needed external linkage once a direction's emitter
// became its own translation unit.

#include "lean_common.hpp"

namespace livehd::lean_ir {

uint32_t node_id(const Node& node) { return static_cast<uint32_t>(node.get_debug_nid()); }

Node pin_node(const Node_pin& pin) { return pin.get_master_node(); }

Ntype_op node_op(const Node& node) { return livehd::graph_util::type_op_of(node); }

bool node_is_flop(const Node& node) { return livehd::graph_util::is_type_flop(node); }

bool node_is_memory(const Node& node) { return node_op(node) == Ntype_op::Memory; }

bool pin_is_input(const Node_pin& pin) { return livehd::graph_util::is_graph_input_pin(pin); }

bool pin_is_const(const Node_pin& pin) { return livehd::graph_util::is_const_pin(pin); }

livehd::graph_util::Edge_vec inp_edges_ordered(const Node& node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    const auto ap = a.sink.get_port_id();
    const auto bp = b.sink.get_port_id();
    if (ap != bp) {
      return ap < bp;
    }
    return a.driver.get_class_index().value < b.driver.get_class_index().value;
  });
  return edges;
}

std::string sink_pin_name(const Edge& edge) {
  const auto sink_node = pin_node(edge.sink);
  return std::string(Ntype::get_sink_name(node_op(sink_node), edge.sink.get_port_id()));
}

uint32_t raw_pin_width(const Node_pin& pin) { return static_cast<uint32_t>(livehd::graph_util::bits_of(pin)); }

uint32_t raw_node_width(const Node& node) { return raw_pin_width(node.create_driver_pin(0)); }

Dlop pin_const_value(const Node_pin& pin) { return livehd::graph_util::hydrate_const(pin); }

Dlop node_const_value(const Node& node) { return livehd::graph_util::hydrate_const(node); }

bool node_output_is_signed(const Node& node) {
  auto n    = node;
  auto dpin = n.create_driver_pin(0);
  return !dpin.is_invalid() && !livehd::graph_util::is_unsign(dpin);
}

[[noreturn]] void fatal(const LeanCtx& /*ctx*/, const std::string& msg) { throw Emit_error("[ERROR] pass.lean: " + msg); }

Node_pin resolve_resize_chain(const Node_pin& start) {
  Node_pin cur = start;
  for (int guard = 0; guard < 32; ++guard) {
    if (cur.is_invalid() || pin_is_input(cur) || pin_is_const(cur)) {
      return cur;
    }
    auto n  = pin_node(cur);
    auto op = node_op(n);
    auto es = inp_edges_ordered(n);
    if (op == Ntype_op::Or && es.size() == 1) {
      cur = es[0].driver;  // arity-1 Or is the emitter's resize
      continue;
    }
    if (op == Ntype_op::Get_mask && es.size() == 2) {
      // Transparent only when the mask is a constant all-ones: get_mask(a,-1) is zext(a).
      const auto& mask = es[1].driver;
      if (pin_is_const(mask)) {
        auto v = pin_const_value(mask);
        if (v.is_just_i64() && v.to_just_i64() == -1) {
          cur = es[0].driver;
          continue;
        }
      }
    }
    return cur;
  }
  return cur;
}


std::string input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin) {
  // pin_name_of resolves a graph-input pin's declared port name directly (via
  // the graph's IO maps); no need to identity-match against get_input_pin.
  auto pname = std::string(livehd::graph_util::pin_name_of(pin));
  if (!pname.empty() && ctx.input_field.contains(pname)) {
    return pname;
  }
  return {};
}

// ---------------------------------------------------------------------------
// The combined certificate DAG's topological order.  See lean_common.hpp.
// ---------------------------------------------------------------------------
namespace {
std::string cert_id_name(const CertPool& pool, uint32_t id) {
  auto o = pool.origin.find(id);
  if (o != pool.origin.end()) {
    return "cert id " + std::to_string(id) + " (" + o->second + ")";
  }
  auto n = pool.by_id.find(id);
  if (n != pool.by_id.end() && !n->second.op_expr.empty()) {
    return "cert id " + std::to_string(id) + " (node n_" + std::to_string(id) + ", " + n->second.op_expr + ")";
  }
  return "cert id " + std::to_string(id);
}
}  // namespace

std::vector<std::string> CertPool::errors(const std::set<uint32_t>& sources) const {
  std::vector<std::string> out;
  std::set<uint32_t>       seen;
  for (const auto id : seed) {
    if (!seen.insert(id).second) {
      out.push_back("cert id " + std::to_string(id) + " was reserved twice");
    }
  }
  for (const auto id : seen) {
    if (by_id.find(id) == by_id.end()) {
      out.push_back(cert_id_name(*this, id) + " was reserved but never filled in");
      continue;
    }
    if (text.find(id) == text.end()) {
      out.push_back(cert_id_name(*this, id) + " has no emitted text");
    }
    if (by_id.at(id).nid != id) {
      out.push_back(cert_id_name(*this, id) + " is stored under a different key than its own nid ("
                    + std::to_string(by_id.at(id).nid) + ")");
    }
    // A node that is ALSO a source is skipped as a leaf by the sort, so its
    // operator would never be evaluated and its value would silently become the
    // source's.  That is a wrong model, not a slow one.
    if (sources.count(id) != 0) {
      out.push_back(cert_id_name(*this, id) + " is both a certificate node and a source");
    }
  }
  for (const auto& [id, info] : by_id) {
    (void)info;
    if (seen.count(id) == 0) {
      out.push_back(cert_id_name(*this, id) + " was filled in but never reserved, so it has no place in the order");
    }
  }
  return out;
}

Cert_dag_result cert_dag_order(const CertPool& pool, const std::set<uint32_t>& sources) {
  Cert_dag_result res;
  res.order.reserve(pool.seed.size());

  const auto bad = pool.errors(sources);
  if (!bad.empty()) {
    res.ok     = false;
    res.reason = "internal: the certificate pool is malformed (" + std::to_string(bad.size()) + " problem(s)): ";
    for (size_t i = 0; i < bad.size() && i < 5; ++i) {
      res.reason += (i != 0 ? "; " : "") + bad[i];
    }
    return res;
  }

  enum class Mark : uint8_t { none, gray, black };
  std::map<uint32_t, Mark> mark;

  // Iterative: a certificate cone is as deep as the design, and recursion here
  // would trade one unbounded-growth bug for another.
  // Each frame is (id, index of the next dep to visit).
  std::vector<std::pair<uint32_t, size_t>> stack;

  for (const auto root : pool.seed) {
    if (mark[root] == Mark::black) {
      continue;
    }
    stack.emplace_back(root, 0);
    mark[root] = Mark::gray;
    while (!stack.empty()) {
      auto& [cur, next] = stack.back();
      const auto  cur_id = cur;
      const auto  iit    = pool.by_id.find(cur_id);
      if (iit == pool.by_id.end()) {  // `errors` rules this out; never throw if it slips
        res.ok     = false;
        res.reason = "internal: " + cert_id_name(pool, cur_id) + " is in the order but has no certificate node.";
        return res;
      }
      const auto& info = iit->second;
      if (next >= info.deps.size()) {
        mark[cur_id] = Mark::black;
        res.order.push_back(cur_id);
        stack.pop_back();
        continue;
      }
      const auto dep = info.deps[next++];
      if (sources.count(dep) != 0) {
        continue;  // a leaf: carries no order
      }
      auto dit = pool.by_id.find(dep);
      if (dit == pool.by_id.end()) {
        res.ok     = false;
        res.reason = "internal: " + cert_id_name(pool, cur_id) + " depends on " + cert_id_name(pool, dep)
                     + ", which is neither a certificate node nor a source. The certificate would reference a "
                       "value that is never defined.";
        return res;
      }
      const auto m = mark[dep];
      if (m == Mark::black) {
        continue;
      }
      if (m == Mark::gray) {
        // The gray frames from `dep` to the top of the stack ARE the cycle, so
        // print them.  Naming only the back edge leaves the reader to rediscover
        // the path, which on a 19k-node cone is the whole job.
        std::string path;
        bool        on = false;
        for (const auto& fr : stack) {
          if (fr.first == dep) {
            on = true;
          }
          if (on) {
            path += "\n    " + cert_id_name(pool, fr.first) + " <- ";
          }
        }
        path += "\n    " + cert_id_name(pool, dep) + "  (closes the cycle)";
        res.ok     = false;
        res.reason = "CERTIFICATE CYCLE: " + cert_id_name(pool, cur_id) + " depends on " + cert_id_name(pool, dep)
                     + ", which is still being resolved. Unlike an LGraph-level loop this is a cycle in the MODEL: "
                       "a memory read is only ordered after the write ports FORWARDED to it (the `fwd` matrix), and "
                       "a synchronous read is a state source, so this says the value genuinely depends on itself. "
                       "There is no dependency order, and therefore no certificate to emit. Each line below depends "
                       "on the one under it:" + path;
        return res;
      }
      mark[dep] = Mark::gray;
      stack.emplace_back(dep, 0);
    }
  }
  return res;
}

}  // namespace livehd::lean_ir
