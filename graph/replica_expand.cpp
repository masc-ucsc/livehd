// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "replica_expand.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "node_util.hpp"
#include "replica_desc.hpp"

namespace livehd::graph_util {

namespace {

// One compact node's external wiring, snapshotted before anything is created.
// inp_edges()/out_edges() are LAZY views over live edge storage and this
// transform both adds edges and deletes the node it is reading.
struct Boundary {
  absl::flat_hash_map<uint32_t, hhds::Pin_class>   in_driver;  // input pid -> external driver
  std::vector<std::pair<uint32_t, hhds::Pin_class>> readers;   // (output pid, external sink)
};

Boundary snapshot_boundary(const hhds::Node_class& inst) {
  Boundary b;
  for (const auto& e : inst.inp_edges()) {
    b.in_driver[static_cast<uint32_t>(e.sink.get_port_id())] = e.driver;
  }
  for (const auto& e : inst.out_edges()) {
    b.readers.emplace_back(static_cast<uint32_t>(e.driver.get_port_id()), e.sink);
  }
  return b;
}

bool expand_one(hhds::Graph* g, const hhds::Node_class& inst, const Replica_desc& desc, std::string_view from_pass) {
  const auto  gio       = inst.get_subnode_io();
  const auto  inst_name = default_instance_name(inst);
  const auto  fail      = [&](const std::string& msg) {
    livehd::diag::err(from_pass, "replica-expand", "internal")
        .msg("cannot expand replicated instance '{}': {}", inst_name, msg)
        .emit();
    return false;
  };
  if (!gio) {
    return fail("it has no callee interface");
  }

  // Activation is NOT expandable yet. Rule 8 makes a carry
  //   carry[r+1] = active[r] ? body_out[r] : carry[r]
  // and rule 3 makes carried outputs SPECIFIED while inactive, so an honest
  // expansion needs a bypass mux per carry per ordinal. This function wires the
  // chain straight through instead, which would publish an inactive replica's
  // output as the loop result. Refuse rather than mis-expand; no front end
  // mints an activation port today (that is design M2).
  if (desc.activation_input) {
    return fail("activation is not implemented yet (the rule-8 carry bypass mux is missing)");
  }

  // A count large enough to exhaust memory is a descriptor bug, not a design.
  // The front end has its own `upass.roll_cap`, but a hand-built or loaded
  // graph reaches here without passing through it.
  constexpr uint64_t kMaxExpand = 1u << 20;
  if (desc.count > kMaxExpand) {
    return fail(std::format("count {} exceeds the {} expansion cap", desc.count, kMaxExpand));
  }

  const auto boundary = snapshot_boundary(inst);

  // The callee's own output declaration for `pid` — authoritative whether or
  // not the compact node ever materialized the pin.
  const auto output_decl = [&](hhds::Port_id pid) -> const hhds::GraphIO::DeclaredIoPin* {
    for (const auto& d : gio->get_output_pin_decls()) {
      if (d.port_id == pid) {
        return &d;
      }
    }
    return nullptr;
  };
  const auto output_is_sized = [&](hhds::Port_id pid) {
    const auto* d = output_decl(pid);
    return d != nullptr && d->bits > 0;
  };

  // PRE-FLIGHT. Every refusal below must fire BEFORE the first node is created:
  // a half-expanded graph (some occurrences wired, the compact node still
  // there) is worse than an unexpanded one, because a caller that keeps going
  // on the -1 return then emits count+1 physical instances. The checks mirror
  // the wiring loops one-for-one, so the loops' own guards stay as a backstop.
  if (desc.index_input) {
    for (uint64_t r = 0; r < desc.count; ++r) {
      if (!desc.index_at(r)) {
        return fail(std::format("index of ordinal {} overflows", r));
      }
    }
  }
  for (const auto& c : desc.carries) {
    if (!boundary.in_driver.contains(static_cast<uint32_t>(c.input_pid))) {
      return fail(std::format("carry destination pid {} has no initial value", static_cast<uint64_t>(c.input_pid)));
    }
    if (desc.count > 1 && !output_is_sized(c.output_pid)) {
      return fail(std::format("carry source pid {} has no sized output declaration", static_cast<uint64_t>(c.output_pid)));
    }
  }
  for (const auto& [out_pid, sink] : boundary.readers) {
    if (desc.count == 0) {
      if (!desc.is_carry_source(static_cast<hhds::Port_id>(out_pid))) {
        return fail(std::format("zero-count instance drives output pid {}, which is not a carry", out_pid));
      }
    } else if (!output_is_sized(static_cast<hhds::Port_id>(out_pid))) {
      return fail(std::format("output pid {} has no sized declaration on the callee", out_pid));
    }
  }

  // Occurrence names follow the same `_li<ordinal>` convention the unroller
  // stamps on the instances an unrolled body creates, so a rolled loop and the
  // same source unrolled spell their replicas identically (see header).
  std::string occ_base{inst_name};
  if (auto a = inst.attr(hhds::attrs::name); a.has()) {
    occ_base = std::string{a.get()};
  }

  // count == 0: nothing is instantiated. A carried output reads its own
  // initial value; anything else has no source at all (rejected by the
  // validator before we get here, but diagnosed rather than silently dropped).
  if (desc.count == 0) {
    for (const auto& [out_pid, sink] : boundary.readers) {
      bool tied = false;
      for (const auto& c : desc.carries) {
        if (static_cast<uint32_t>(c.output_pid) != out_pid) {
          continue;
        }
        auto it = boundary.in_driver.find(static_cast<uint32_t>(c.input_pid));
        if (it == boundary.in_driver.end()) {
          return fail(std::format("zero-count carry output pid {} has no initial value to pass through", out_pid));
        }
        it->second.connect_sink(sink);
        tied = true;
        break;
      }
      if (!tied) {
        return fail(std::format("zero-count instance drives output pid {}, which is not a carry", out_pid));
      }
    }
    inst.del_node();
    return true;
  }

  std::vector<hhds::Node_class> reps;
  reps.reserve(desc.count);
  for (uint64_t r = 0; r < desc.count; ++r) {
    auto rep = create_typed_node(*g, Ntype_op::Sub);
    rep.set_subnode(gio);
    rep.set_name(std::format("{}_li{}", occ_base, r));
    // Carry the compact node's own annotations (srcid, color, ...) so
    // provenance survives expansion; the descriptor itself must NOT ride along
    // (each occurrence is an ordinary instance now).
    if (auto s = inst.attr(hhds::attrs::srcid); s.has()) {
      rep.attr(hhds::attrs::srcid).set(s.get());
    }
    reps.emplace_back(rep);
  }

  const auto is_role_input = [&](uint32_t pid) {
    return (desc.index_input && static_cast<uint32_t>(*desc.index_input) == pid)
           || (desc.activation_input && static_cast<uint32_t>(*desc.activation_input) == pid);
  };

  // A Sub's driver pin must carry its width/sign (and, when the compact node
  // had one, its net name): cgen's per-pin declaration reads those, and an
  // unstamped pin falls through to a nameless 1-bit `wire` — which for several
  // same-named occurrences means one shared, truncated net instead of a carry
  // chain. Widths come from the callee's own output declarations, which are
  // authoritative whether or not the compact node ever materialized the pin.
  // Returns an INVALID pin when the port has no usable declaration; callers
  // must treat that as a failure. An unstamped driver pin silently becomes a
  // 1-bit net in cgen, which for a carry chain truncates every ordinal.
  const auto make_driver = [&](const hhds::Node_class& rep, hhds::Port_id pid) {
    const hhds::GraphIO::DeclaredIoPin* decl = output_decl(pid);
    if (decl == nullptr || decl->bits <= 0) {
      return hhds::Pin_class{};
    }
    auto dp = rep.create_driver_pin(pid);
    set_bits(dp, decl->bits);
    if (decl->unsign) {
      set_unsign(dp);
    } else {
      set_sign(dp);
    }
    // Deliberately NO pin_name copy. The compact node carries one only on the
    // pins the parent read, so copying it would name the LAST occurrence
    // through cgen's named-net path while the other count-1 occurrences take
    // its unnamed fallback — two declaration paths for one carry chain, and the
    // named one ends up referenced but never declared. One uniform path keeps
    // the chain consistent; cgen de-collides the repeats itself.
    return dp;
  };

  std::vector<uint32_t> invariant_pids;
  invariant_pids.reserve(boundary.in_driver.size());
  for (const auto& [pid, drv] : boundary.in_driver) {
    if (!is_role_input(pid) && !desc.is_carry_dest(static_cast<hhds::Port_id>(pid))) {
      invariant_pids.emplace_back(pid);
    }
  }
  std::ranges::sort(invariant_pids);

  for (uint64_t r = 0; r < desc.count; ++r) {
    auto rep = reps[r];

    // Invariant inputs: the same external driver on every occurrence. Wired in
    // PORT-ID order — iterating the hash map directly would let pin creation
    // order vary between runs, and emitted output should be reproducible.
    for (const auto pid : invariant_pids) {
      auto sp = rep.create_sink_pin(static_cast<hhds::Port_id>(pid));
      boundary.in_driver.at(pid).connect_sink(sp);
    }

    // Index: one fresh constant per occurrence.
    if (desc.index_input) {
      const auto v = desc.index_at(r);
      if (!v) {
        return fail(std::format("index of ordinal {} overflows", r));
      }
      auto cpin = create_const(*g, *Dlop::from_pyrope(std::to_string(*v)));
      auto sp   = rep.create_sink_pin(*desc.index_input);
      cpin.connect_sink(sp);
    }

    // Activation.
    if (desc.activation_input) {
      auto sp  = rep.create_sink_pin(*desc.activation_input);
      auto ext = boundary.in_driver.find(static_cast<uint32_t>(*desc.activation_input));
      if (r == 0 || !desc.next_active_output) {
        if (ext != boundary.in_driver.end()) {
          ext->second.connect_sink(sp);
        } else {
          // An unconnected activation input means "always called".
          create_const(*g, *Dlop::from_pyrope("1")).connect_sink(sp);
        }
      } else {
        // active[r] = active[r-1] && next_active[r-1]
        auto prev      = reps[r - 1];
        auto prev_act  = prev.get_sink_pin(*desc.activation_input);
        auto prev_next = make_driver(prev, *desc.next_active_output);
        auto andn      = create_typed_node(*g, Ntype_op::And);
        auto as        = andn.create_sink_pin(static_cast<hhds::Port_id>(0));  // multi-driver "as"
        // The previous occurrence's activation is whatever drives its sink.
        if (!prev_act.is_invalid()) {
          for (const auto& e : prev.inp_edges()) {
            if (e.sink.get_port_id() == *desc.activation_input) {
              e.driver.connect_sink(as);
              break;
            }
          }
        }
        prev_next.connect_sink(as);
        auto ao = andn.create_driver_pin(static_cast<hhds::Port_id>(0));
        set_bits(ao, 1);
        set_unsign(ao);
        ao.connect_sink(sp);
      }
    }

    // Carries: replica 0 takes the external initial value, later replicas take
    // the previous occurrence's mapped output.
    for (const auto& c : desc.carries) {
      auto sp = rep.create_sink_pin(c.input_pid);
      if (r == 0) {
        auto it = boundary.in_driver.find(static_cast<uint32_t>(c.input_pid));
        if (it == boundary.in_driver.end()) {
          return fail(std::format("carry destination pid {} has no initial value", static_cast<uint64_t>(c.input_pid)));
        }
        it->second.connect_sink(sp);
      } else {
        auto src = make_driver(reps[r - 1], c.output_pid);
        if (src.is_invalid()) {
          return fail(std::format("carry source pid {} has no sized output declaration",
                                  static_cast<uint64_t>(c.output_pid)));
        }
        src.connect_sink(sp);
      }
    }
  }

  // External readers bind to the last occurrence: a carried result is the value
  // after `count` applications, and a final-only result is the last replica's
  // output (the front end only allows that read when the last replica is
  // guaranteed to execute).
  auto last = reps.back();
  for (const auto& [out_pid, sink] : boundary.readers) {
    auto src = make_driver(last, static_cast<hhds::Port_id>(out_pid));
    if (src.is_invalid()) {
      return fail(std::format("output pid {} has no sized declaration on the callee", out_pid));
    }
    src.connect_sink(sink);
  }

  inst.del_node();
  return true;
}

}  // namespace

int expand_replicated_subs(hhds::Graph* g, std::string_view from_pass) {
  if (g == nullptr) {
    return 0;
  }
  // Snapshot the targets first: expansion creates Sub nodes, and a live walk
  // would revisit the occurrences it just made.
  std::vector<std::pair<hhds::Node_class, Replica_desc>> targets;
  for (auto n : g->fast_class()) {
    if (!is_replicated_sub(n)) {
      continue;
    }
    std::string err;
    auto        d = replica_desc_of(n, &err);
    if (!d) {
      // The node IS replicated (the attribute is there); this build just cannot
      // read the payload. Skipping it would emit/prove `count` replicas as one.
      livehd::diag::err(from_pass, "replica-desc-unreadable", "unsupported")
          .msg("instance '{}' carries a replica descriptor this build cannot read: {}", default_instance_name(n), err)
          .hint("the artifact was written by a different LiveHD version — recompile the design from source")
          .emit();
      return -1;
    }
    targets.emplace_back(n, *d);
  }

  int expanded = 0;
  for (auto& [node, desc] : targets) {
    if (!expand_one(g, node, desc, from_pass)) {
      return -1;
    }
    ++expanded;
  }
  return expanded;
}

bool expand_replicated_subs_all(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view from_pass) {
  for (const auto& g : graphs) {
    if (expand_replicated_subs(g.get(), from_pass) < 0) {
      return false;
    }
  }
  return true;
}

}  // namespace livehd::graph_util
