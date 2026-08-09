// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "occurrence_materialize.hpp"

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

namespace livehd::graph_util {

namespace {

// One compact node's external wiring, snapshotted before anything is created.
// inp_edges()/out_edges() are LAZY views over live edge storage and this
// transform both adds edges and deletes the node it is reading.
struct Boundary {
  absl::flat_hash_map<uint32_t, hhds::Pin_class>    in_driver;  // input pid -> external driver
  std::vector<std::pair<uint32_t, hhds::Pin_class>> readers;    // (output pid, external sink)
};

Boundary snapshot_boundary(const hhds::Node_class& inst) {
  Boundary b;
  for (const auto& e : inst.inp_edges()) {
    if (e.driver.get_master_node() == inst) {
      continue;  // native carry self-edge, not an external initial driver
    }
    b.in_driver[static_cast<uint32_t>(e.sink.get_port_id())] = e.driver;
  }
  for (const auto& e : inst.out_edges()) {
    if (e.sink.get_master_node() == inst) {
      continue;  // native carry self-edge, not a parent reader
    }
    b.readers.emplace_back(static_cast<uint32_t>(e.driver.get_port_id()), e.sink);
  }
  return b;
}

bool expand_one(hhds::Graph* g, const hhds::Node_class& inst, const hhds::Subnode_loop& desc, std::string_view from_pass) {
  const auto gio       = inst.get_subnode_io();
  const auto inst_name = default_instance_name(inst);
  const auto fail      = [&](const std::string& msg) {
    livehd::diag::err(from_pass, "replica-expand", "internal")
        .msg("cannot expand replicated instance '{}': {}", inst_name, msg)
        .emit();
    return false;
  };
  if (!gio) {
    return fail("it has no callee interface");
  }
  const auto carries         = inst.subnode_group().carries();
  const auto is_carry_source = [&](hhds::Port_id pid) {
    return std::ranges::any_of(carries, [pid](const auto& carry) { return carry.output_port() == pid; });
  };
  const auto is_carry_dest = [&](hhds::Port_id pid) {
    return std::ranges::any_of(carries, [pid](const auto& carry) { return carry.input_port() == pid; });
  };

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
      (void)desc.index_at(r);  // the native descriptor validated the complete domain at attach/load
    }
  }
  for (const auto& c : carries) {
    if (!boundary.in_driver.contains(static_cast<uint32_t>(c.input_port()))) {
      return fail(std::format("carry destination pid {} has no initial value", static_cast<uint64_t>(c.input_port())));
    }
    if (desc.count > 1 && !output_is_sized(c.output_port())) {
      return fail(std::format("carry source pid {} has no sized output declaration", static_cast<uint64_t>(c.output_port())));
    }
  }
  for (const auto& [out_pid, sink] : boundary.readers) {
    if (desc.count == 0) {
      if (!is_carry_source(static_cast<hhds::Port_id>(out_pid))) {
        return fail(std::format("zero-count instance drives output pid {}, which is not a carry", out_pid));
      }
    } else if (!output_is_sized(static_cast<hhds::Port_id>(out_pid))) {
      return fail(std::format("output pid {} has no sized declaration on the callee", out_pid));
    }
  }

  // Use HHDS's one structural formatter so multiple loop sites in the same
  // parent continue the module-scoped __li ordinal space instead of each
  // restarting at zero.
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
      for (const auto& c : carries) {
        if (static_cast<uint32_t>(c.output_port()) != out_pid) {
          continue;
        }
        auto it = boundary.in_driver.find(static_cast<uint32_t>(c.input_port()));
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
  const auto* lib = g->get_io() != nullptr ? g->get_io()->get_library() : nullptr;
  for (const auto occurrence : inst.subnode_group().occurrences()) {
    const uint64_t r   = occurrence.ordinal();
    auto           rep = create_typed_node(*g, Ntype_op::Sub);
    rep.set_subnode(gio);
    auto occurrence_name = lib != nullptr ? hhds::format_occurrence_path(*lib, occurrence.path()) : std::string{};
    if (occurrence_name.empty()) {
      occurrence_name = std::format("{}__li{}", occ_base, r);
    }
    rep.set_name(occurrence_name);
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

  // An ordinary Sub occurrence must expose every declared callee output, even
  // when that particular occurrence has no parent-side reader. HHDS's
  // hierarchy view reaches a callee output from the body first and then asks
  // for the matching site driver; leaving an unused output pin uncreated makes
  // that read-only traversal assert instead of correctly resolving to an empty
  // consumer set. Activation makes this visible on the last occurrence's
  // `__next_active`, but the invariant applies to every output.
  for (const auto& rep : reps) {
    for (const auto& decl : gio->get_output_pin_decls()) {
      (void)make_driver(rep, decl.port_id);
    }
  }

  std::vector<uint32_t> invariant_pids;
  invariant_pids.reserve(boundary.in_driver.size());
  for (const auto& [pid, drv] : boundary.in_driver) {
    if (!is_role_input(pid) && !is_carry_dest(static_cast<hhds::Port_id>(pid))) {
      invariant_pids.emplace_back(pid);
    }
  }
  std::ranges::sort(invariant_pids);

  std::vector<hhds::Pin_class>                                active_values(desc.count);
  absl::flat_hash_map<uint32_t, std::vector<hhds::Pin_class>> carry_input_values;
  for (const auto& carry : carries) {
    carry_input_values[static_cast<uint32_t>(carry.input_port())].reserve(desc.count);
  }

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
      const auto v    = desc.index_at(r);
      auto       cpin = create_const(*g, *Dlop::from_pyrope(std::to_string(v)));
      auto       sp   = rep.create_sink_pin(*desc.index_input);
      cpin.connect_sink(sp);
    }

    // Activation.
    if (desc.activation_input) {
      auto            sp  = rep.create_sink_pin(*desc.activation_input);
      auto            ext = boundary.in_driver.find(static_cast<uint32_t>(*desc.activation_input));
      hhds::Pin_class active_value;
      if (r == 0 || !desc.next_active_output) {
        if (ext != boundary.in_driver.end()) {
          active_value = ext->second;
        } else {
          // An unconnected activation input means "always called".
          active_value = create_const(*g, *Dlop::from_pyrope("1"));
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
        active_value = ao;
      }
      active_value.connect_sink(sp);
      active_values[r] = active_value;
    }

    // Carries: replica 0 takes the external initial value. Later replicas use
    // active[r-1] ? output[r-1] : input[r-1], so an inactive occurrence
    // preserves the carry instead of publishing an arbitrary body result.
    for (const auto& c : carries) {
      auto            sp = rep.create_sink_pin(c.input_port());
      hhds::Pin_class carry_value;
      if (r == 0) {
        auto it = boundary.in_driver.find(static_cast<uint32_t>(c.input_port()));
        if (it == boundary.in_driver.end()) {
          return fail(std::format("carry destination pid {} has no initial value", static_cast<uint64_t>(c.input_port())));
        }
        carry_value = it->second;
      } else {
        auto previous_output = make_driver(reps[r - 1], c.output_port());
        if (previous_output.is_invalid()) {
          return fail(std::format("carry source pid {} has no sized output declaration", static_cast<uint64_t>(c.output_port())));
        }
        if (!desc.activation_input) {
          carry_value = previous_output;
        } else {
          const auto* decl = output_decl(c.output_port());
          if (decl == nullptr || decl->bits == 0 || active_values[r - 1].is_invalid()) {
            return fail(
                std::format("carry source pid {} cannot build an activation bypass", static_cast<uint64_t>(c.output_port())));
          }
          auto& prior_values = carry_input_values.at(static_cast<uint32_t>(c.input_port()));
          if (prior_values.size() != r || prior_values.back().is_invalid()) {
            return fail(std::format("carry destination pid {} has no previous input value", static_cast<uint64_t>(c.input_port())));
          }
          auto mux = create_typed_node(*g, Ntype_op::Mux, static_cast<int32_t>(decl->bits));
          active_values[r - 1].connect_sink(mux.create_sink_pin(0));
          prior_values.back().connect_sink(mux.create_sink_pin(1));
          previous_output.connect_sink(mux.create_sink_pin(2));
          carry_value = mux.create_driver_pin(0);
          if (decl->unsign) {
            set_unsign(carry_value);
          } else {
            set_sign(carry_value);
          }
        }
      }
      carry_value.connect_sink(sp);
      carry_input_values.at(static_cast<uint32_t>(c.input_port())).push_back(carry_value);
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

int materialize_occurrences(hhds::Graph* g, std::string_view from_pass) {
  if (g == nullptr) {
    return 0;
  }
  // Snapshot the targets first: expansion creates Sub nodes, and a live walk
  // would revisit the occurrences it just made.
  std::vector<std::pair<hhds::Node_class, hhds::Subnode_loop>> targets;
  for (auto n : g->body().nodes()) {
    if (!n.is_loop_subnode()) {
      continue;
    }
    auto d = n.subnode_loop();
    if (!d) {
      livehd::diag::err(from_pass, "replica-desc-unreadable", "unsupported")
          .msg("instance '{}' is marked as a loop Sub but has no native descriptor", default_instance_name(n))
          .emit();
      return -1;
    }
    targets.emplace_back(n, *d);
  }

  int expanded = 0;
  // Expand in reverse storage order. The shared occurrence formatter derives
  // a site's module-scoped ordinal base from earlier compact loop sites; those
  // descriptors must still be present when a later site is formatted.
  for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
    if (!expand_one(g, it->first, it->second, from_pass)) {
      return -1;
    }
    ++expanded;
  }
  return expanded;
}

bool materialize_occurrences_all(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view from_pass) {
  for (const auto& g : graphs) {
    if (materialize_occurrences(g.get(), from_pass) < 0) {
      return false;
    }
  }
  return true;
}

}  // namespace livehd::graph_util
