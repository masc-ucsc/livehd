// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_mux.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "attr_carry.hpp"
#include "blast.hpp"
#include "cprop.hpp"
#include "json_util.hpp"
#include "lnet_ops.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "satopt_detail.hpp"

namespace livehd::satopt {
namespace {
using namespace detail;
using livehd::synth::Blast_options;
using livehd::synth::Lid;
using livehd::synth::Lnet;
using livehd::synth::Lnet_ops;
using livehd::synth::Wiring_blaster;

// A proof cone's bits, built into a RAW Lnet with lazy constants and a node
// limit (the network satopt used to build straight into ABC).
constexpr uint64_t kMaxProofNodes = 1000000;

class Cone {
  Wiring_blaster<Lid>                          wiring_;
  Lnet_ops&                                    ops_;
  absl::node_hash_map<Pin, std::map<int, Lid>> bits_;
  absl::flat_hash_set<Pin>                     visiting_;
  absl::flat_hash_set<Node>                    region_;
  partition::Region_body                       rb_;
  Blast_options                                options_;

  bool                                         reject_unstamped_ = false;

public:
  explicit Cone(Lnet_ops& ops, bool reject_unstamped = false) : ops_(ops), reject_unstamped_(reject_unstamped) {}
  Lid bit(const Pin& p, int b) {
    if (p.is_invalid() || b < 0 || b > 65536) {
      throw Unsupported{};
    }
    const int stamped_width = gu::bits_of(p);
    if (stamped_width > 0 && b >= stamped_width) {
      return gu::is_unsign(p) ? ops_.zero() : bit(p, stamped_width - 1);
    }
    // An unstamped width is read as one bit: past bit 0 there is nothing to
    // blast, and the shared profile proves nothing about a guessed width.
    if (stamped_width <= 0 && !p.is_const() && !cut(p) && (b > 0 || reject_unstamped_)) {
      throw Unsupported{};
    }
    auto& slots = bits_[p];
    if (auto it = slots.find(b); it != slots.end()) {
      return it->second;
    }
    if (p.is_const()) {
      if (gu::const_of(p).has_unknowns()) {
        throw Unsupported{};
      }
      return slots[b] = ops_.konst(gu::const_of(p).bit_test(b));
    }
    if (cut(p)) {
      return slots[b] = ops_.input({});
    }
    if (bits_.size() > 50000 || width(p) > 65536 || !visiting_.insert(p).second) {
      throw Unsupported{};
    }
    auto n  = p.get_master_node();
    auto op = gu::type_op_of(n);
    if (op == Ntype_op::Concat || op == Ntype_op::Set_mask) {
      // Wiring detects cycles per bit, so disjoint slices of a packed state
      // bus remain usable even when the source node has another active bit.
      visiting_.erase(p);
      slots[b] = wiring_.bit(
          p,
          b,
          [&](const Pin& q, int i) { return bit(q, i); },
          [&] { return ops_.zero(); },
          [](std::string_view) { throw Unsupported{}; });
    } else {
      // The proof uses priority Hotmux semantics. It never assumes exclusivity
      // of unconstrained state, so every resulting theorem is whole-design safe.
      if (op == Ntype_op::Hotmux) {
        auto a     = arms_of(n);
        Lid  value = ops_.zero();
        for (int i = static_cast<int>(a.values.size()) - 1; i >= 0; --i) {
          if (a.controls[i].is_invalid()) {
            value = bit(a.values[i], b);
          } else {
            const Lid c = condition(a.controls[i]);
            value       = ops_.or_(ops_.and_(c, bit(a.values[i], b)), ops_.and_(ops_.inv(c), value));
          }
        }
        slots[b] = value;
      } else {
        const auto fail       = [](const Node&,
                                   std::string_view,
                                   std::string_view,
                                   std::string_view,
                                   std::string_view = {},
                                   const Pin&       = Pin{},
                                   std::string_view = {}) { throw Unsupported{}; };
        const auto fail_shift = [](const Node&, std::string_view, const Dlop&, const Pin&) { throw Unsupported{}; };
        livehd::synth::blast_comb(
            n,
            width(p),
            slots,
            ops_,
            [&](const Pin& q, int i) { return bit(q, i); },
            options_,
            rb_,
            region_,
            fail,
            fail_shift);
      }
    }
    visiting_.erase(p);
    return slots.at(b);
  }
  Lid condition(const Pin& p) {
    Lid c = ops_.zero();
    for (int b = 0; b < width(p); ++b) {
      c = ops_.or_(c, bit(p, b));
    }
    return c;
  }
  Lid condition(const Arms& a, int arm) {
    if (!a.hot) {
      const Lid c = condition(a.controls[0]);
      return arm == 0 ? ops_.inv(c) : c;
    }
    if (!a.controls[arm].is_invalid()) {
      return condition(a.controls[arm]);
    }
    Lid c = ops_.zero();
    for (const auto& p : a.controls) {
      if (!p.is_invalid()) {
        c = ops_.or_(c, condition(p));
      }
    }
    return ops_.inv(c);
  }
};

// One proof over every candidate. `build` returns one refutation per
// candidate (nonzero exactly when its claim fails), or Lnet::kNone for an
// unsupported one; the prover proves which are constant zero. nullopt when
// the prover attempted nothing.
std::optional<std::vector<bool>> prove_zero(const Prove_const0& prove, const std::function<std::vector<Lid>(Lnet_ops&, Cone&)>& build,
                                            bool reject_unstamped, uint64_t& nodes) {
  Lnet                net;
  Lnet_ops            ops(net, Lnet_ops::Constants::lazy, kMaxProofNodes);
  Cone                cone(ops, reject_unstamped);
  const auto          refutes = build(ops, cone);
  nodes                       = net.size();
  std::vector<size_t> outputs;
  std::vector<bool>   proven(refutes.size(), false);
  for (size_t i = 0; i < refutes.size(); ++i) {
    if (refutes[i] == Lnet::kNone) {
      continue;
    }
    net.add_output(refutes[i], std::format("f{}", outputs.size()));
    outputs.push_back(i);
  }
  if (outputs.empty()) {
    return proven;
  }
  const auto zero = prove(net);
  if (!zero) {
    return std::nullopt;
  }
  if (zero->size() == outputs.size()) {
    for (size_t i = 0; i < outputs.size(); ++i) {
      proven[outputs[i]] = (*zero)[i];
    }
  }
  return proven;
}

bool crosses_walk(const Node& n, const Arms& arms, absl::flat_hash_set<Pin>& seen) {
  std::vector<Pin>         pending = arms.values;
  for (const auto& c : arms.controls) {
    if (!c.is_invalid()) {
      pending.push_back(c);
    }
  }
  while (!pending.empty()) {
    const auto p = pending.back();
    pending.pop_back();
    if (p.is_const() || !seen.insert(p).second || gu::is_graph_input_pin(p)) {
      continue;
    }
    const auto producer = p.get_master_node();
    if (gu::color_of(producer) != gu::color_of(n)) {
      return true;
    }
    if (cut(p)) {
      continue;
    }
    if (seen.size() > 50000) {
      return false;
    }
    for (const auto& in_pin : producer.inp_sorted_pins()) {
      const auto in_drv = in_pin.get_driver_pin();
      pending.push_back(in_drv);
    }
  }
  return false;
}
// Does an arm or control of `n` come from another color? `steps` (optional)
// counts the pins walked.
bool crosses(const Node& n, const Arms& arms, uint64_t* steps = nullptr) {
  absl::flat_hash_set<Pin> seen;
  const bool               result = crosses_walk(n, arms, seen);
  if (steps) {
    *steps += seen.size();
  }
  return result;
}
// `options` is the proof-relevant budget (Budget::proof_key); a partial
// search (`complete=false`) never stands in for a full one.
constexpr int kMuxCacheVersion = 3;
struct Cached {
  std::string                    source, colors, options;
  bool                           all      = false;
  bool                           complete = false;
  uint64_t                       work = 0, queries = 0;  // what the search cost (Meter::replay)
  std::shared_ptr<Satopt_result> result;
};
std::map<std::string, Cached> saved;
std::mutex                    saved_mutex;
Cached read_cache(const std::string& path) {
  Cached row;
  if (path.empty()) {
    return row;
  }
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt() != kMuxCacheVersion
      || !doc.HasMember("source") || !doc["source"].IsString() || !doc.HasMember("colors") || !doc["colors"].IsString()
      || !doc.HasMember("options") || !doc["options"].IsString() || !doc.HasMember("all") || !doc["all"].IsBool()
      || !doc.HasMember("complete") || !doc["complete"].IsBool() || !doc.HasMember("work") || !doc["work"].IsUint64()
      || !doc.HasMember("queries") || !doc["queries"].IsUint64() || !doc.HasMember("facts") || !doc["facts"].IsArray()) {
    return row;
  }
  row.work             = doc["work"].GetUint64();
  row.queries          = doc["queries"].GetUint64();
  row.source           = doc["source"].GetString();
  row.colors           = doc["colors"].GetString();
  row.options          = doc["options"].GetString();
  row.all              = doc["all"].GetBool();
  row.complete         = doc["complete"].GetBool();
  row.result           = std::make_shared<Satopt_result>();
  row.result->complete = row.complete;
  for (const auto& f : doc["facts"].GetArray()) {
    if (!f.IsArray() || f.Size() != 5 || !f[0].IsUint64() || !f[1].IsInt() || !f[2].IsInt() || !f[3].IsInt() || !f[4].IsInt()
        || f[1].GetInt() < 0 || f[2].GetInt() < 0 || f[3].GetInt() < 0 || f[3].GetInt() > 3) {
      return {};
    }
    Mux_fact fact{f[0].GetUint64(), f[1].GetInt(), f[2].GetInt(), static_cast<Mux_fact::Kind>(f[3].GetInt()), f[4].GetInt()};
    row.result->mux[fact.node].push_back(fact);
    ++row.result->proven;
  }
  return row;
}
void write_cache(const std::string& path, const Cached& row) {
  if (path.empty()) {
    return;
  }
  auto text  = std::format(
      "{{\"version\":{},\"source\":\"{}\",\"colors\":\"{}\",\"options\":\"{}\",\"all\":{},\"complete\":{},\"work\":{},"
      "\"queries\":{},\"facts\":[",
      kMuxCacheVersion,
      json_util::escape(row.source),
      json_util::escape(row.colors),
      json_util::escape(row.options),
      row.all ? "true" : "false",
      row.complete ? "true" : "false",
      row.work,
      row.queries);
  bool comma = false;
  for (const auto& [node, facts] : row.result->mux) {
    for (const auto& f : facts) {
      text  += std::format("{}[{},{},{},{},{}]", comma ? "," : "", node, f.arm, f.bit, static_cast<int>(f.kind), f.other);
      comma  = true;
    }
  }
  write_atomic(path, text + "]}\n");
}
// Does every simulated column agree with `f` (vacuously where its arm is not
// selected)? Throws Unsupported when a value it needs is unavailable; such a
// candidate is dropped (its cone would not blast either, and the batch stays
// small).
bool survives(Word_sim& sim, const Arms& arms, const Mux_fact& f) {
  const auto get = [&](const Pin& p) -> const std::vector<Dlop>& {
    const auto* v = sim.values(p);
    if (v == nullptr) {
      throw Unsupported{};
    }
    return *v;
  };
  for (uint32_t j = 0; j < sim.columns(); ++j) {
    bool selected = false;
    if (!arms.hot) {
      selected = get(arms.controls[0])[j].is_known_zero() == (f.arm == 0);
    } else if (!arms.controls[f.arm].is_invalid()) {
      selected = !get(arms.controls[f.arm])[j].is_known_zero();
    } else {
      selected = true;
      for (const auto& c : arms.controls) {
        if (!c.is_invalid() && !get(c)[j].is_known_zero()) {
          selected = false;
        }
      }
    }
    if (!selected) {
      continue;
    }
    const bool bit      = get(arms.values[f.arm])[j].bit_test(f.bit);
    const bool expected = f.kind == Mux_fact::Kind::one
                          || (f.other >= 0 && (get(arms.values[f.other])[j].bit_test(f.bit) != (f.kind == Mux_fact::Kind::complement)));
    if (bit != expected) {
      return false;
    }
  }
  return true;
}


// What bit `bit` of `pin` reads inside the mux's own region, through the
// wiring a rewrite builds (Get_mask windows, Concat lanes, Xor with a
// constant, Not, constants): a constant, or a (driver, bit, complemented)
// triple. A fact whose arm bit already reads its target this way was applied
// before. The proof cone sees across regions, so a relation that is
// structural there (an arm computed as ~x in another region) is still new to
// the mux's region, which sees two unrelated inputs.
struct Local_bit {
  Pin                 pin;
  int                 bit      = 0;
  bool                inverted = false;
  std::optional<bool> value;
};
// `uncolored`: the shared profile's graphs carry no colors, so an uncolored
// mux looks through uncolored wiring too (a second run then sees its own
// rewrite and is idempotent).
Local_bit local_bit(Pin pin, int bit, const Node& mux, bool uncolored) {
  bool inverted = false;
  for (int guard = 0; guard < 256 && !pin.is_invalid(); ++guard) {
    if (pin.is_const()) {
      const auto& v = gu::const_of(pin);
      if (v.has_unknowns()) {
        break;
      }
      return {.value = v.bit_test(bit) != inverted};
    }
    const int w = gu::bits_of(pin);
    const auto n = pin.get_master_node();
    const bool same_region = uncolored && !gu::has_color(mux) ? !gu::has_color(n)
                                                              : gu::has_color(mux) && gu::has_color(n) && gu::color_of(n) == gu::color_of(mux);
    if ((w > 0 && bit >= w) || !same_region) {
      break;
    }
    const auto op = gu::type_op_of(n);
    if (op == Ntype_op::Get_mask) {
      const auto mask   = gu::get_driver_of_sink_name(n, "mask");
      const auto window = mask.is_const() ? gu::mask_window_of(gu::const_of(mask)) : std::nullopt;
      if (!window) {
        break;
      }
      pin = gu::get_driver_of_sink_name(n, "a");
      bit += window->first;
      continue;
    }
    if (op == Ntype_op::Concat) {
      const auto lanes = gu::concat_lanes(n);
      const auto lane  = std::ranges::find_if(lanes, [&](const auto& l) { return bit >= l.offset && bit < l.offset + l.width; });
      if (lane == lanes.end()) {
        break;
      }
      pin = lane->value;
      bit -= lane->offset;
      continue;
    }
    std::vector<Pin> ins;
    for (const auto& sink : n.inp_sorted_pins()) {
      ins.push_back(sink.get_driver_pin());
    }
    if (op == Ntype_op::Not && ins.size() == 1) {
      pin      = ins.front();
      inverted = !inverted;
      continue;
    }
    if (op == Ntype_op::Xor && ins.size() == 2 && ins[0].is_const() != ins[1].is_const()) {
      const auto& k = gu::const_of(ins[0].is_const() ? ins[0] : ins[1]);
      if (k.has_unknowns()) {
        break;
      }
      inverted ^= k.bit_test(bit);
      pin = ins[0].is_const() ? ins[1] : ins[0];
      continue;
    }
    break;
  }
  return {pin, bit, inverted, std::nullopt};
}

// True when arm `f.arm` bit `f.bit` already reads what the fact states.
bool applied(const Mux_fact& f, const Arms& arms, const Node& mux, bool uncolored) {
  const auto self = local_bit(arms.values[f.arm], f.bit, mux, uncolored);
  if (f.other < 0) {
    return self.value && *self.value == (f.kind == Mux_fact::Kind::one);
  }
  const auto other      = local_bit(arms.values[f.other], f.bit, mux, uncolored);
  const bool complement = f.kind == Mux_fact::Kind::complement;
  if (self.value || other.value) {
    return self.value && other.value && (*self.value != *other.value) == complement;
  }
  return self.pin == other.pin && self.bit == other.bit && (self.inverted != other.inverted) == complement;
}

// A per-bit arm fact holds only while its arm is selected, so it may rewrite
// that arm's INPUT and nothing else: never the arm's driver (it has other
// consumers) and never the mux output. equal/complement read the OTHER arm's
// ORIGINAL driver, captured before any sink of this mux is rewired.
//
// Each arm is rebuilt from maximal runs of bits with the same choice
// (unchanged, 0, 1, = other arm, ~other arm). One run over the whole width is
// a single pin (a constant, the other arm's driver, or its complement); mixed
// runs become a Concat of slices, most significant lane first.
//
// Shared-profile guards (satopt_stages.hpp): `exclusive(n)` answers whether a
// Hotmux's controls are proven exclusive, which a rewrite leaving every data
// arm identical needs -- cprop then folds the Hotmux, and its obligation with
// it.
void apply_mux_facts(Select_rewrite& rw, const Node& n, const std::vector<Mux_fact>& facts, Mux_satopt& stats, Profile profile,
                     const std::function<bool(const Node&)>& exclusive) {
  const bool shared = profile == Profile::shared;
  const auto arms   = arms_of(n);
  const int  count  = static_cast<int>(arms.values.size());
  const auto out    = n.create_driver_pin(0);
  const int  width  = gu::bits_of(out);
  if (count == 0 || width <= 0) {
    return;  // an unstamped output has no bit range to rewrite
  }
  // The latch contract keeps a latch Q a DIRECT data arm of its hold mux: no
  // arm reading a latch output is rebuilt, and no arm is rebuilt to read one.
  std::vector<bool> latch(count, false);
  for (int a = 0; a < count; ++a) {
    const auto& v = arms.values[a];
    latch[a]      = shared && !v.is_const() && gu::type_op_of(v.get_master_node()) == Ntype_op::Latch;
  }
  // First fact per (arm, bit) wins, as the blaster applied them.
  std::vector<std::vector<int>> choice(count, std::vector<int>(width, -1));
  for (size_t i = 0; i < facts.size(); ++i) {
    const auto& f = facts[i];
    if (f.arm < 0 || f.arm >= count || f.bit < 0 || f.bit >= width || f.other >= count) {
      continue;
    }
    if (latch[f.arm] || (f.other >= 0 && latch[f.other])) {
      continue;
    }
    if (choice[f.arm][f.bit] < 0 && applied(f, arms, n, shared)) {
      choice[f.arm][f.bit] = -2;  // already read that way: nothing to rewrite
    }
    if (choice[f.arm][f.bit] == -1) {
      choice[f.arm][f.bit] = static_cast<int>(i);
    }
  }
  // An arm bit may read another arm's bit only while that bit stays as is:
  // the rewrite reads the ORIGINAL arm drivers, so reading a bit that is
  // itself rewritten would no longer match the two arms. Scanning in arm
  // order drops the lower arm of a mutually equal pair (the higher arm then
  // reads the lower) and any read of a bit proven constant.
  for (int a = 0; a < count; ++a) {
    for (int b = 0; b < width; ++b) {
      const int c = choice[a][b];
      if (c >= 0 && facts[c].other >= 0 && choice[facts[c].other][b] >= 0) {
        choice[a][b] = -1;
      }
    }
  }
  const int   explicit_arms = arms.hot && !arms.controls.empty() && arms.controls.back().is_invalid() ? count - 1 : count;
  Arm_builder build{rw.g, n};
  // The replacement of each rewritten arm, applied once every arm is planned.
  std::vector<std::pair<int, Pin>> plan;
  uint64_t                         planned_bits = 0;
  for (int a = 0; a < count; ++a) {
    // A constant arm is rewritten too: reading the other arm's bit makes that
    // mux bit independent of the select.
    // Maximal runs of the same (kind, other); kind -1 = unchanged.
    struct Run {
      int lo, hi, kind, other;
    };
    std::vector<Run> runs;
    int              rewritten = 0;
    for (int b = 0; b < width; ++b) {
      const int c     = choice[a][b];
      const int kind  = c < 0 ? -1 : static_cast<int>(facts[c].kind);
      const int other = c < 0 ? -1 : facts[c].other;
      rewritten += c >= 0;
      if (!runs.empty() && runs.back().kind == kind && runs.back().other == other && runs.back().hi == b) {
        runs.back().hi = b + 1;
      } else {
        runs.push_back({b, b + 1, kind, other});
      }
    }
    if (rewritten == 0) {
      continue;
    }
    const auto run_pin = [&](const Run& r) -> Pin {
      switch (static_cast<Mux_fact::Kind>(r.kind)) {
        case Mux_fact::Kind::zero: return gu::create_const(rw.g, *Dlop::create_integer(0));
        case Mux_fact::Kind::one: return build.ones(r.hi - r.lo);
        case Mux_fact::Kind::equal:
          return r.lo == 0 && r.hi == width ? arms.values[r.other] : build.slice(arms.values[r.other], r.lo, r.hi);
        case Mux_fact::Kind::complement: return build.complement(arms.values[r.other], r.lo, r.hi);
      }
      return {};
    };
    Pin replacement;
    if (shared) {
      // The arm is rebuilt as the mux output's `width`-bit pattern read with
      // the output's own sign: LGraph values are integers, and a signed mux
      // extends its arms -- an unsigned rebuild of a negative arm would change
      // the value every wider reader (and bitwidth) sees.
      const bool signed_out = !gu::is_unsign(out);
      if (runs.size() == 1 && runs.front().kind == static_cast<int>(Mux_fact::Kind::zero)) {
        replacement = gu::create_const(rw.g, *Dlop::create_integer(0));
      } else if (runs.size() == 1 && runs.front().kind == static_cast<int>(Mux_fact::Kind::one)) {
        replacement = signed_out ? gu::create_const(rw.g, *Dlop::create_integer(-1)) : build.ones(width);
      } else {
        const auto& r0 = runs.front();
        if (runs.size() == 1 && r0.kind == static_cast<int>(Mux_fact::Kind::equal) && gu::bits_of(arms.values[r0.other]) == width
            && gu::is_unsign(arms.values[r0.other]) == !signed_out) {
          replacement = arms.values[r0.other];
        } else {
          std::vector<std::pair<Pin, int>> lanes;
          for (const auto& r : runs) {
            Pin lane;
            if (r.kind < 0) {
              lane = build.slice(arms.values[a], r.lo, r.hi);
            } else if (r.kind == static_cast<int>(Mux_fact::Kind::equal)) {
              lane = build.slice(arms.values[r.other], r.lo, r.hi);
            } else if (r.kind == static_cast<int>(Mux_fact::Kind::one)) {
              lane = build.ones(r.hi - r.lo);
            } else {
              lane = run_pin(r);
            }
            lanes.emplace_back(lane, r.hi - r.lo);
          }
          replacement = lanes.size() == 1 ? lanes.front().first : build.concat(lanes, width);
          if (signed_out) {
            replacement = build.sext(replacement, width);
          }
        }
      }
    } else if (runs.size() == 1) {
      replacement = run_pin(runs.front());
    } else {
      std::vector<std::pair<Pin, int>> lanes;
      for (const auto& r : runs) {
        lanes.emplace_back(r.kind < 0 ? build.slice(arms.values[a], r.lo, r.hi) : run_pin(r), r.hi - r.lo);
      }
      replacement = build.concat(lanes, width);
    }
    plan.emplace_back(a, replacement);
    planned_bits += static_cast<uint64_t>(rewritten);
  }
  if (plan.empty()) {
    return;
  }
  if (shared && arms.hot) {
    // Would every data value (the implicit 0 fallback included) end up the
    // same? Then cprop folds the Hotmux, so its controls must be proven
    // exclusive first -- otherwise the whole mux keeps its original arms.
    std::vector<Pin> final_values = arms.values;
    for (const auto& [a, pin] : plan) {
      final_values[a] = pin;
    }
    const auto same = [](const Pin& x, const Pin& y) {
      return x == y || (x.is_const() && y.is_const() && gu::const_of(x).is_known_eq(gu::const_of(y)));
    };
    bool identical = true;
    for (const auto& v : final_values) {
      identical = identical && same(v, final_values.front());
    }
    if (explicit_arms == count) {  // no fallback: an unselected Hotmux is 0
      identical = identical && final_values.front().is_const() && gu::const_of(final_values.front()).is_known_zero();
    }
    if (identical && !exclusive(n)) {
      return;
    }
  }
  for (const auto& [a, replacement] : plan) {
    const auto pid = !arms.hot ? static_cast<hhds::Port_id>(a + 1)
                     : a < explicit_arms ? static_cast<hhds::Port_id>(2 * a + 1)
                                         : static_cast<hhds::Port_id>(2 * explicit_arms);
    rw.replace(n, pid, replacement);
    ++stats.arms;
  }
  stats.bits += planned_bits;
  ++stats.muxes;
}
}  // namespace

std::shared_ptr<const Satopt_result> mux_satopt(hhds::Graph* graph, const Mux_prover& prover, std::string_view cache_dir,
                                                bool all_regions, Profile profile, Meter* meter) {
  std::lock_guard lock(saved_mutex);
  Meter           unlimited;
  auto&           m       = meter ? *meter : unlimited;
  const bool      shared  = profile == Profile::shared;
  const auto      source  = source_key(graph, false, prover.salt);
  const auto      colors  = source_key(graph, true, prover.salt);
  const auto      options = m.budget().proof_key();
  // A shared proof also rejects unstamped widths: its own row.
  const auto      path    = cache_path(cache_dir, graph->get_name(), shared ? "-shared" : "");
  auto&           row     = saved[std::string(graph->get_name()) + (shared ? "\x01shared" : "")];
  const auto      usable  = [&] {
    return row.result && row.complete && row.source == source && row.options == options
           && (row.all || (!all_regions && row.colors == colors));
  };
  if (!usable()) {
    row = read_cache(path);
  }
  const bool had_complete = usable();
  if (had_complete && m.replay(row.work, row.queries)) {
    auto reused    = std::make_shared<Satopt_result>(*row.result);
    reused->reused = true;
    std::print("[pass.satopt] {}: reused {} proven mux facts (exact definition match)\n", graph->get_name(), reused->proven);
    return reused;
  }
  // A complete row this budget cannot replay stays for a later, larger run:
  // a partial search never replaces it.
  const auto                                                    prior  = had_complete ? row : Cached{};
  const auto                                                    work0  = m.work_done();
  const auto                                                    query0 = m.queries_done();
  auto                                                          result = std::make_shared<Satopt_result>();
  Word_sim                                                      sim(sim_options(m.budget()));
  std::vector<std::pair<Mux_fact, std::shared_ptr<const Arms>>> candidates;
  // Work: the walks, one unit per candidate and eight word-level samples per
  // evaluated value. The candidate caps and the budget make the search
  // partial, never wrong.
  bool       afford = true;
  const auto capped = [&] {
    if (result->candidates >= 1000000 || candidates.size() >= 100000) {
      result->complete = false;
      return true;
    }
    if (!afford) {
      result->complete = false;
    }
    return !afford;
  };
  for (const auto n : graph->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
      continue;
    }
    if (capped()) {
      break;
    }
    const auto arms  = arms_of(n);
    uint64_t   walk  = 0;
    const bool cross = !arms.values.empty() && (all_regions || crosses(n, arms, &walk));
    afford           = m.work(1 + walk);
    if (!cross) {
      continue;
    }
    if (!afford) {
      result->complete = false;  // this mux was never searched
      ++result->budget_skips;
      break;
    }
    const auto shared_arms = std::make_shared<const Arms>(arms);
    const auto add         = [&](int arm, int bit, Mux_fact::Kind kind, int other) {
      if (capped()) {
        return;
      }
      Mux_fact f{static_cast<uint64_t>(n.get_debug_nid()), arm, bit, kind, other};
      ++result->candidates;
      const auto evaluated = sim.work();
      try {
        if (survives(sim, arms, f)) {
          candidates.emplace_back(f, shared_arms);
        }
      } catch (const Unsupported&) {
      }
      afford = m.work(1 + (sim.work() - evaluated));
    };
    for (int bit = 0; bit < std::min(65536, width(n.create_driver_pin(0))) && !capped(); ++bit) {
      for (int arm = 0; arm < static_cast<int>(arms.values.size()) && !capped(); ++arm) {
        if (!arms.values[arm].is_const()) {
          add(arm, bit, Mux_fact::Kind::zero, -1);
          add(arm, bit, Mux_fact::Kind::one, -1);
        }
        for (int other = 0; other < static_cast<int>(arms.values.size()) && !capped(); ++other) {
          if (other != arm) {
            add(arm, bit, Mux_fact::Kind::equal, other);
            add(arm, bit, Mux_fact::Kind::complement, other);
          }
        }
      }
    }
  }
  if (!afford) {
    result->complete = false;  // the last charge failed: whatever it paid for is unsearched
  }
  result->survivors = candidates.size();
  const auto save   = [&] {
    if (m.exhausted()) {
      result->complete = false;  // some charge failed: whatever it paid for went unsearched
    }
    if (!result->complete && had_complete) {
      row = prior;
      return;
    }
    row = {source, colors, options, all_regions, result->complete, m.work_done() - work0, m.queries_done() - query0, result};
    write_cache(path, row);
  };
  if (candidates.empty()) {
    save();
    return result;
  }
  // One solver query proves the whole batch.
  if (!m.query()) {
    result->budget_skips += candidates.size();
    result->complete      = false;
    save();
    return result;
  }
  ++result->queries;
  uint64_t   nodes  = 0;
  const auto proven = prove_zero(
      prover.prove_const0,
      [&](Lnet_ops& ops, Cone& cone) {
        std::vector<Lid> refutes;
        for (const auto& [fact, shared_arms] : candidates) {
          const auto& arms = *shared_arms;
          try {
            const Lid c        = cone.condition(arms, fact.arm);
            const Lid a        = cone.bit(arms.values[fact.arm], fact.bit);
            Lid       expected = fact.kind == Mux_fact::Kind::one ? ops.one() : ops.zero();
            if (fact.other >= 0) {
              expected = cone.bit(arms.values[fact.other], fact.bit);
              if (fact.kind == Mux_fact::Kind::complement) {
                expected = ops.inv(expected);
              }
            }
            refutes.push_back(ops.and_(c, ops.xor_(a, expected)));
          } catch (const Unsupported&) {
            refutes.push_back(Lnet::kNone);
          } catch (const Lnet_ops::Too_large&) {
            // Past the proof network's size cap: not refuted, not searched.
            ++result->budget_skips;
            result->complete = false;
            refutes.push_back(Lnet::kNone);
          }
        }
        return refutes;
      },
      shared,
      nodes);
  m.work(nodes);
  if (!proven) {
    return result;
  }
  for (size_t i = 0; i < proven->size(); ++i) {
    if ((*proven)[i]) {
      result->mux[candidates[i].first.node].push_back(candidates[i].first);
      ++result->proven;
    }
  }
  std::print("[pass.satopt] {}: {} candidates, {} seed survivors, {} proven mux facts{}\n",
             graph->get_name(),
             result->candidates,
             result->survivors,
             result->proven,
             result->complete ? "" : " (partial search)");
  save();
  return result;
}
Mux_satopt optimize_muxes(hhds::Graph* graph, const Mux_prover& prover, std::string_view cache_dir, bool all_regions, Profile profile,
                          Meter* meter) {
  Meter      unlimited;
  auto&      m = meter ? *meter : unlimited;
  Mux_satopt stats;
  const auto result  = mux_satopt(graph, prover, cache_dir, all_regions, profile, &m);
  stats.candidates   = result->candidates;
  stats.survivors    = result->survivors;
  stats.proven       = result->proven;
  stats.reused       = result->reused;
  stats.queries      = result->queries;
  stats.budget_skips = result->budget_skips;
  stats.complete     = result->complete;
  if (result->mux.empty()) {
    return stats;
  }
  // Resolve every subject before mutating: the rewrite adds nodes. Facts are
  // keyed by node id in a std::map, so the application order (and the ids of
  // the new nodes) is deterministic.
  // A reused all-regions proof (explicit pass.satopt) still applies only to
  // the muxes this call analyzes: inside one region a fact does not change
  // the region's function, so it is left to the mapper.
  absl::flat_hash_map<uint64_t, Node> subjects;
  for (const auto n : graph->body().nodes()) {
    if (result->mux.contains(static_cast<uint64_t>(n.get_debug_nid())) && (all_regions || crosses(n, arms_of(n)))) {
      subjects.emplace(static_cast<uint64_t>(n.get_debug_nid()), n);
    }
  }
  const bool                      shared = profile == Profile::shared;
  Select_rewrite                  rw{*graph, {}, shared};
  std::optional<formal::Prover>   onehot;  // built on first use, before any rewrite of this graph
  const auto                      exclusive = [&](const Node& n) {
    if (gu::proven_of(n) == gu::kFormalOnehot && !gu::has_runtime_check(n)) {
      return true;
    }
    // Out of budget, the Hotmux keeps its arms (the fact is not applied).
    if (!m.query()) {
      ++stats.budget_skips;
      stats.complete = false;
      return false;
    }
    ++stats.queries;
    if (!onehot) {
      onehot.emplace(graph, prove_options(m.budget(), true, true));
    }
    std::vector<Pin> controls;
    for (const auto& [control, value] : gu::hotmux_inputs(n).arms) {
      (void)value;
      controls.push_back(control);
    }
    const auto before = onehot->work();
    const bool proven = onehot->are_exclusive(controls).verdict == formal::Verdict::Proven;
    m.work(onehot->work() - before);
    return proven;
  };
  // The prover's memo stays valid across the rewrites below: each keeps its
  // mux output's function, and the swept nodes go only at the end.
  for (const auto& [nid, facts] : result->mux) {
    if (auto it = subjects.find(nid); it != subjects.end()) {
      apply_mux_facts(rw, it->second, facts, stats, profile, exclusive);
    }
  }
  rw.sweep();
  stats.nodes_removed = rw.removed;
  std::print("[pass.satopt] {}: rewrote {} arm(s) of {} mux(es), {} bit(s) from proven mux facts\n",
             graph->get_name(),
             stats.arms,
             stats.muxes,
             stats.bits);
  return stats;
}
Collapse_satopt collapse_hotmuxes(hhds::Graph* graph, Profile profile, Meter* meter) {
  Meter           unlimited;
  auto&           m = meter ? *meter : unlimited;
  Collapse_satopt stats;
  if (graph == nullptr) {
    return stats;
  }
  std::optional<formal::Prover> prover;  // built on first use
  uint64_t                      charged = 0;
  for (const auto n : graph->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Hotmux || !n.has_out_edges() || gu::proven_of(n) == gu::kFormalOnehot
        || gu::has_runtime_check(n) || gu::has_color(n)) {
      continue;
    }
    std::vector<Pin> controls;
    for (const auto& [control, value] : gu::hotmux_inputs(n).arms) {
      (void)value;
      controls.push_back(control);
    }
    if (controls.size() < 2) {
      continue;  // one control: nothing to overlap, and cprop's decode covers it
    }
    ++stats.candidates;
    if (!m.query()) {
      ++stats.budget_skips;
      break;
    }
    ++stats.queries;
    if (!prover) {
      // Global exclusivity: no parent context, state and memory free.
      prover.emplace(graph, prove_options(m.budget(), true, profile == Profile::shared));
    }
    const auto verdict = prover->are_exclusive(controls).verdict;
    m.work(prover->work() - charged);
    charged = prover->work();
    if (verdict == formal::Verdict::Proven) {
      gu::set_proven(n, gu::kFormalOnehot);
      ++stats.proven;
    } else if (verdict == formal::Verdict::Refuted) {
      ++stats.refuted;
    } else {
      ++stats.unknown;
    }
  }
  if (stats.proven != 0) {
    uint64_t before = 0, after = 0;
    for (const auto n : graph->body().nodes()) {
      (void)n;
      ++before;
    }
    livehd::share_mux_regions(*graph, false);
    for (const auto n : graph->body().nodes()) {
      (void)n;
      ++after;
    }
    stats.nodes_removed = before > after ? before - after : 0;
  }
  std::print("[pass.satopt] {}: {} Hotmux(es) proven exclusive of {} candidate(s)\n", graph->get_name(), stats.proven, stats.candidates);
  return stats;
}

namespace {
std::optional<Mux_prover>& mux_prover_slot() {
  static std::optional<Mux_prover> slot;
  return slot;
}
}  // namespace
void              register_mux_prover(const Mux_prover& prover) { mux_prover_slot() = prover; }
const Mux_prover* registered_mux_prover() { return mux_prover_slot() ? &*mux_prover_slot() : nullptr; }
}  // namespace livehd::satopt
