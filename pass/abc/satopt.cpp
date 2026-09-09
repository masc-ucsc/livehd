// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt.hpp"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

#include "abc_blast.hpp"
#include "abc_salt.hpp"
#include "absl/container/node_hash_map.h"
#include "hhds/attrs/name.hpp"
#include "json_util.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
#include "aig/hop/hop.h"
}
// clang-format on

namespace livehd::abc {
namespace {
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;
struct Unsupported {};

bool cut(const Pin& p) {
  if (gu::is_graph_input_pin(p)) {
    return true;
  }
  auto op = gu::type_op_of(p.get_master_node());
  return op == Ntype_op::Flop || op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::Fflop || op == Ntype_op::Latch;
}
int width(const Pin& p) {
  return p.is_const() ? std::max(1, static_cast<int>(gu::const_of(p).get_bits())) : std::max(1, gu::bits_of(p));
}

struct Arms {
  std::vector<Pin> controls, values;
  bool             hot = false;
};
Arms arms_of(const Node& n) {
  Arms a;
  a.hot = gu::type_op_of(n) == Ntype_op::Hotmux;
  if (a.hot) {
    auto h = gu::hotmux_inputs(n);
    for (auto [c, v] : h.arms) {
      a.controls.push_back(c);
      a.values.push_back(v);
    }
    if (!h.fallback.is_invalid()) {
      a.controls.emplace_back();
      a.values.push_back(h.fallback);
    }
  } else {
    std::map<int, Pin> inputs;
    for (const auto& e : n.inp_edges()) {
      inputs[e.sink.get_port_id()] = e.driver;
    }
    // Two-arm Mux uses a nonzero condition; larger indexed muxes are left to ABC.
    if (inputs.size() == 3 && inputs.contains(0) && inputs.contains(1) && inputs.contains(2)) {
      a.controls = {inputs.at(0), inputs.at(0)};
      a.values   = {inputs.at(1), inputs.at(2)};
    }
  }
  return a;
}

// Eight deterministic, word-level input vectors. This is only a rejection
// filter: unsupported operations can reject an opportunity, never prove one.
class Seeds {
  using Values = std::array<Dlop, 8>;
  absl::node_hash_map<Pin, Values> values_;
  absl::flat_hash_set<Pin>         visiting_;
  std::mt19937_64                  random_{0x7361746f7074ULL};
  static Dlop                      fit(const Dlop& v, const Pin& p) {
    if (p.is_const()) {
      return v;
    }
    auto low = v.and_op(Dlop::get_mask_value(width(p)));
    return gu::is_unsign(p) ? *low : *low->sext_op(Dlop::create_integer(width(p)));
  }

public:
  const Values& get(const Pin& p) {
    if (p.is_invalid()) {
      throw Unsupported{};
    }
    if (auto it = values_.find(p); it != values_.end()) {
      return it->second;
    }
    if (values_.size() > 50000 || width(p) > 65536 || !visiting_.insert(p).second) {
      throw Unsupported{};
    }
    Values out;
    if (p.is_const()) {
      if (gu::const_of(p).has_unknowns()) {
        throw Unsupported{};
      }
      out.fill(gu::const_of(p));
    } else if (cut(p)) {
      for (auto& v : out) {
        std::string bits(static_cast<size_t>(width(p)), '0');
        uint64_t    r = 0;
        for (size_t b = 0; b < bits.size(); ++b) {
          if (b % 64 == 0) {
            r = random_();
          }
          bits[b] = ((r >> (b % 64)) & 1) ? '1' : '0';
        }
        v = fit(*Dlop::from_binary(bits, true), p);
      }
    } else {
      const auto                                n  = p.get_master_node();
      const auto                                op = gu::type_op_of(n);
      std::map<int, std::vector<const Values*>> ins;
      for (const auto& e : n.inp_edges()) {
        ins[e.sink.get_port_id()].push_back(&get(e.driver));
      }
      for (int seed = 0; seed < 8; ++seed) {
        const auto arg = [&](int pid) -> const Dlop& {
          if (!ins.contains(pid) || ins.at(pid).empty()) {
            throw Unsupported{};
          }
          return (*ins.at(pid).front())[seed];
        };
        Dlop v = *Dlop::create_integer(0);
        if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
          const auto arms = arms_of(n);
          if (arms.values.empty()) {
            throw Unsupported{};
          }
          size_t selected = 0;
          if (!arms.hot) {
            selected = get(arms.controls[0])[seed].is_known_zero() ? 0 : 1;
          } else {
            selected = arms.values.size();
            for (size_t i = 0; i < arms.values.size(); ++i) {
              if (arms.controls[i].is_invalid() || !get(arms.controls[i])[seed].is_known_zero()) {
                selected = i;
                break;
              }
            }
          }
          if (selected < arms.values.size()) {
            v = get(arms.values[selected])[seed];
          }
        } else if (op == Ntype_op::Concat) {
          std::vector<Dlop::Concat_lane> lanes;
          for (const auto& l : gu::concat_lanes(n)) {
            lanes.push_back({&get(l.value)[seed], l.width});
          }
          v = *Dlop::concat_op(lanes);
        } else if (op == Ntype_op::Not) {
          v = *arg(0).not_op();
        } else if (op == Ntype_op::Get_mask) {
          v = *arg(0).get_mask_op(arg(1));
        } else if (op == Ntype_op::Set_mask) {
          v = *arg(0).set_mask_op(arg(1), arg(2));
        } else if (op == Ntype_op::Sext) {
          v = *arg(0).sext_op(arg(1));
        } else if (op == Ntype_op::SHL) {
          v = *arg(0).shl_op(arg(1));
        } else if (op == Ntype_op::SRA) {
          v = *arg(0).sra_op(arg(1));
        } else if (op == Ntype_op::LT) {
          v = *arg(0).lt_op(arg(1));
        } else if (op == Ntype_op::GT) {
          v = *arg(0).gt_op(arg(1));
        } else if (op == Ntype_op::Sum || op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor || op == Ntype_op::Mult
                   || op == Ntype_op::EQ || op == Ntype_op::Ror) {
          bool first = true;
          Dlop equal_to;
          for (const auto& [pid, operands] : ins) {
            for (const auto* operand : operands) {
              const auto& x = (*operand)[seed];
              if (op == Ntype_op::Sum) {
                v = *(pid == 1 ? v.sub_op(x) : v.add_op(x));
              } else if (op == Ntype_op::Ror) {
                v = *v.ror_op(x);
              } else if (op == Ntype_op::EQ) {
                if (first) {
                  equal_to = x;
                  v        = *Dlop::create_integer(1);
                } else {
                  v = *v.and_op(equal_to.eq_op(x));
                }
              } else if (first) {
                v = x;
              } else if (op == Ntype_op::And) {
                v = *v.and_op(x);
              } else if (op == Ntype_op::Or) {
                v = *v.or_op(x);
              } else if (op == Ntype_op::Xor) {
                v = *v.xor_op(x);
              } else {
                v = *v.mult_op(x);
              }
              first = false;
            }
          }
        } else {
          throw Unsupported{};
        }
        if (v.has_unknowns() || v.is_invalid()) {
          throw Unsupported{};
        }
        out[seed] = fit(v, p);
      }
    }
    visiting_.erase(p);
    return values_.emplace(p, std::move(out)).first->second;
  }
  bool survives(const Arms& arms, const Mux_fact& f) {
    for (int seed = 0; seed < 8; ++seed) {
      bool selected = false;
      if (!arms.hot) {
        selected = get(arms.controls[0])[seed].is_known_zero() == (f.arm == 0);
      } else if (!arms.controls[f.arm].is_invalid()) {
        selected = !get(arms.controls[f.arm])[seed].is_known_zero();
      } else {
        selected = true;
        for (const auto& c : arms.controls) {
          if (!c.is_invalid() && !get(c)[seed].is_known_zero()) {
            selected = false;
          }
        }
      }
      if (!selected) {
        continue;
      }
      const bool bit = get(arms.values[f.arm])[seed].bit_test(f.bit);
      const bool expected
          = f.kind == Mux_fact::Kind::one
            || (f.other >= 0 && (get(arms.values[f.other])[seed].bit_test(f.bit) != (f.kind == Mux_fact::Kind::complement)));
      if (bit != expected) {
        return false;
      }
    }
    return true;
  }
};

struct Gates {
  Abc_Ntk_t* ntk = Abc_NtkAlloc(ABC_NTK_NETLIST, ABC_FUNC_AIG, 1);
  Abc_Obj_t* constants[2]{};
  uint64_t   count = 0;
  Gates() { ntk->pName = Abc_UtilStrsav(const_cast<char*>("satopt")); }
  ~Gates() {
    if (ntk) {
      Abc_NtkDelete(ntk);
    }
  }
  Abc_Obj_t* net(Abc_Obj_t* node) {
    if (++count > 1000000) {
      throw Unsupported{};
    }
    auto* out = Abc_NtkCreateNet(ntk);
    Abc_ObjAddFanin(out, node);
    return out;
  }
  Abc_Obj_t* konst(bool v) {
    auto*& c = constants[v];
    if (!c) {
      auto* n  = Abc_NtkCreateNode(ntk);
      n->pData = Hop_NotCond(Hop_ManConst1(static_cast<Hop_Man_t*>(ntk->pManFunc)), !v);
      c        = net(n);
    }
    return c;
  }
  Abc_Obj_t* zero() { return konst(false); }
  Abc_Obj_t* one() { return konst(true); }
  Abc_Obj_t* inv(Abc_Obj_t* a) {
    if (a == zero()) {
      return one();
    }
    if (a == one()) {
      return zero();
    }
    auto* n  = Abc_NtkCreateNode(ntk);
    n->pData = Hop_Not(Hop_IthVar(static_cast<Hop_Man_t*>(ntk->pManFunc), 0));
    Abc_ObjAddFanin(n, a);
    return net(n);
  }
  Abc_Obj_t* bin(Abc_Obj_t* a, Abc_Obj_t* b, char op) {
    if (op == '&') {
      if (a == zero() || b == zero()) {
        return zero();
      }
      if (a == one()) {
        return b;
      }
      if (b == one() || a == b) {
        return a;
      }
    }
    if (op == '|') {
      if (a == one() || b == one()) {
        return one();
      }
      if (a == zero()) {
        return b;
      }
      if (b == zero() || a == b) {
        return a;
      }
    }
    if (op == '^') {
      if (a == b) {
        return zero();
      }
      if (a == zero()) {
        return b;
      }
      if (b == zero()) {
        return a;
      }
    }
    auto* n  = Abc_NtkCreateNode(ntk);
    auto* h  = static_cast<Hop_Man_t*>(ntk->pManFunc);
    n->pData = op == '&' ? Hop_CreateAnd(h, 2) : op == '|' ? Hop_CreateOr(h, 2) : Hop_CreateExor(h, 2);
    Abc_ObjAddFanin(n, a);
    Abc_ObjAddFanin(n, b);
    return net(n);
  }
  Abc_Obj_t* and_(Abc_Obj_t* a, Abc_Obj_t* b) { return bin(a, b, '&'); }
  Abc_Obj_t* or_(Abc_Obj_t* a, Abc_Obj_t* b) { return bin(a, b, '|'); }
  Abc_Obj_t* xor_(Abc_Obj_t* a, Abc_Obj_t* b) { return bin(a, b, '^'); }
};

class Cone {
  Wiring_blaster<Abc_Obj_t*>                          wiring_;
  Gates&                                              ops_;
  absl::node_hash_map<Pin, std::map<int, Abc_Obj_t*>> bits_;
  absl::flat_hash_set<Pin>                            visiting_;
  absl::flat_hash_set<Node>                           region_;
  partition::Region_body                              rb_;
  Map_options                                         options_;

public:
  explicit Cone(Gates& gates) : ops_(gates) {}
  Abc_Obj_t* bit(const Pin& p, int b) {
    if (p.is_invalid() || b < 0 || b > 65536) {
      throw Unsupported{};
    }
    const int stamped_width = gu::bits_of(p);
    if (stamped_width > 0 && b >= stamped_width) {
      return gu::is_unsign(p) ? ops_.zero() : bit(p, stamped_width - 1);
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
      auto* pi   = Abc_NtkCreatePi(ops_.ntk);
      auto  name = std::format("i{}", Abc_ObjId(pi));
      auto* net  = ops_.net(pi);
      Abc_ObjAssignName(net, const_cast<char*>(name.c_str()), nullptr);
      return slots[b] = net;
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
        auto  a     = arms_of(n);
        auto* value = ops_.zero();
        for (int i = static_cast<int>(a.values.size()) - 1; i >= 0; --i) {
          if (a.controls[i].is_invalid()) {
            value = bit(a.values[i], b);
          } else {
            auto* c = condition(a.controls[i]);
            value   = ops_.or_(ops_.and_(c, bit(a.values[i], b)), ops_.and_(ops_.inv(c), value));
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
        blast_comb(
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
  Abc_Obj_t* condition(const Pin& p) {
    auto* c = ops_.zero();
    for (int b = 0; b < width(p); ++b) {
      c = ops_.or_(c, bit(p, b));
    }
    return c;
  }
  Abc_Obj_t* condition(const Arms& a, int arm) {
    if (!a.hot) {
      auto* c = condition(a.controls[0]);
      return arm == 0 ? ops_.inv(c) : c;
    }
    if (!a.controls[arm].is_invalid()) {
      return condition(a.controls[arm]);
    }
    auto* c = ops_.zero();
    for (const auto& p : a.controls) {
      if (!p.is_invalid()) {
        c = ops_.or_(c, condition(p));
      }
    }
    return ops_.inv(c);
  }
};

bool crosses(const Node& n, const Arms& arms) {
  absl::flat_hash_set<Pin> seen;
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
    for (const auto& e : producer.inp_edges()) {
      pending.push_back(e.driver);
    }
  }
  return false;
}

// The exact translation input, not a digest oracle. IDs are intentionally part
// of this descriptor: a harmless renumbering misses rather than attaching a
// theorem to a different node. State and opaque outputs remain independent cuts.
std::string source_key(hhds::Graph* graph, bool colors) {
  std::vector<std::string> rows;
  for (const auto n : graph->body().nodes()) {
    auto row = std::format("{}:{}", n.get_debug_nid(), static_cast<int>(gu::type_op_of(n)));
    if (colors) {
      row += std::format(":c{}", gu::color_of(n));
    }
    std::vector<std::string> edges;
    const auto               pin = [](const Pin& p) {
      std::string value;
      if (p.is_const()) {
        value = satopt_constant_key(gu::const_of(p));
      }
      return std::format("{}:{}:{}:{}:{}:{}",
                         p.get_master_node().get_debug_nid(),
                         p.get_port_id(),
                         gu::bits_of(p),
                         gu::is_unsign(p),
                         value.size(),
                         value);
    };
    for (const auto& e : n.inp_edges()) {
      edges.push_back(std::format("{}={}", e.sink.get_port_id(), pin(e.driver)));
    }
    for (const auto& e : n.out_edges()) {
      edges.push_back("o=" + pin(e.driver));
    }
    std::sort(edges.begin(), edges.end());
    for (const auto& edge : edges) {
      row += std::format("|{}:{}", edge.size(), edge);
    }
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end());
  std::string key = std::format("{}:{}:{}", kAbcSrcSalt, graph->get_name().size(), graph->get_name());
  for (const auto& row : rows) {
    key += std::format("\n{}:{}", row.size(), row);
  }
  return key;
}
struct Cached {
  std::string                    source, colors;
  bool                           all = false;
  std::shared_ptr<Satopt_result> result;
};
std::map<std::string, Cached> saved;
std::mutex                    saved_mutex;
std::string                   cache_path(std::string_view dir, std::string_view name) {
  if (dir.empty()) {
    return {};
  }
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : name) {
    h = (h ^ c) * 1099511628211ULL;
  }
  return std::format("{}/{:016x}.json", dir, h);
}
Cached read_cache(const std::string& path) {
  Cached row;
  if (path.empty()) {
    return row;
  }
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("source") || !doc["source"].IsString() || !doc.HasMember("colors")
      || !doc["colors"].IsString() || !doc.HasMember("all") || !doc["all"].IsBool() || !doc.HasMember("facts")
      || !doc["facts"].IsArray()) {
    return row;
  }
  row.source = doc["source"].GetString();
  row.colors = doc["colors"].GetString();
  row.all    = doc["all"].GetBool();
  row.result = std::make_shared<Satopt_result>();
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
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if (ec) {
    return;
  }
  // Separate temporary files prevent concurrent invocations from interleaving
  // a definition descriptor from one proof run with another run's facts.
  auto      temporary = path + ".tmp.XXXXXX";
  const int fd        = mkstemp(temporary.data());
  if (fd < 0) {
    return;
  }
  close(fd);
  std::ofstream out(temporary);
  out << std::format("{{\"source\":\"{}\",\"colors\":\"{}\",\"all\":{},\"facts\":[",
                     json_util::escape(row.source),
                     json_util::escape(row.colors),
                     row.all ? "true" : "false");
  bool comma = false;
  for (const auto& [node, facts] : row.result->mux) {
    for (const auto& f : facts) {
      out << std::format("{}[{},{},{},{},{}]", comma ? "," : "", node, f.arm, f.bit, static_cast<int>(f.kind), f.other);
      comma = true;
    }
  }
  out << "]}\n";
  out.close();
  if (out) {
    std::filesystem::rename(temporary, path, ec);
  }
  std::filesystem::remove(temporary, ec);
}
}  // namespace

std::string satopt_constant_key(const Dlop& value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string    key;
  for (unsigned char byte : value.serialize()) {
    key += hex[byte >> 4];
    key += hex[byte & 15];
  }
  return key;
}

std::string satopt_source_key(hhds::Graph* graph) { return source_key(graph, false); }

struct Satopt_seeds::Impl {
  Seeds seeds;
};
Satopt_seeds::Satopt_seeds() : impl_(std::make_unique<Impl>()) {}
Satopt_seeds::~Satopt_seeds() = default;
std::optional<std::array<Dlop, 8>> Satopt_seeds::sample(const Pin& pin) {
  try {
    return impl_->seeds.get(pin);
  } catch (const Unsupported&) {
    return std::nullopt;
  }
}

std::shared_ptr<const Satopt_result> satopt(hhds::Graph* graph, std::string_view cache_dir, bool all_regions) {
  std::lock_guard lock(saved_mutex);
  const auto      source = source_key(graph, false);
  const auto      colors = source_key(graph, true);
  const auto      path   = cache_path(cache_dir, graph->get_name());
  auto&           row    = saved[std::string(graph->get_name())];
  if (!row.result) {
    row = read_cache(path);
  }
  if (row.result && row.source == source && (row.all || (!all_regions && row.colors == colors))) {
    auto reused    = std::make_shared<Satopt_result>(*row.result);
    reused->reused = true;
    std::print("[pass.satopt] {}: reused {} proven mux facts (exact definition match)\n", graph->get_name(), reused->proven);
    return reused;
  }
  auto                                                          result = std::make_shared<Satopt_result>();
  Seeds                                                         seeds;
  std::vector<std::pair<Mux_fact, std::shared_ptr<const Arms>>> candidates;
  for (const auto n : graph->body().nodes()) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Mux && op != Ntype_op::Hotmux) {
      continue;
    }
    const auto arms = arms_of(n);
    if (arms.values.empty() || (!all_regions && !crosses(n, arms))) {
      continue;
    }
    const auto shared_arms = std::make_shared<const Arms>(arms);
    const auto add         = [&](int arm, int bit, Mux_fact::Kind kind, int other) {
      if (result->candidates >= 1000000 || candidates.size() >= 100000) {
        return;
      }
      Mux_fact f{static_cast<uint64_t>(n.get_debug_nid()), arm, bit, kind, other};
      ++result->candidates;
      try {
        if (seeds.survives(arms, f)) {
          candidates.emplace_back(f, shared_arms);
        }
      } catch (const Unsupported&) {
      }
    };
    for (int bit = 0;
         bit < std::min(65536, width(n.create_driver_pin(0))) && result->candidates < 1000000 && candidates.size() < 100000;
         ++bit) {
      for (int arm = 0; arm < static_cast<int>(arms.values.size()) && result->candidates < 1000000 && candidates.size() < 100000;
           ++arm) {
        if (!arms.values[arm].is_const()) {
          add(arm, bit, Mux_fact::Kind::zero, -1);
          add(arm, bit, Mux_fact::Kind::one, -1);
        }
        for (int other = 0;
             other < static_cast<int>(arms.values.size()) && result->candidates < 1000000 && candidates.size() < 100000;
             ++other) {
          if (other != arm) {
            add(arm, bit, Mux_fact::Kind::equal, other);
            add(arm, bit, Mux_fact::Kind::complement, other);
          }
        }
      }
    }
  }
  result->survivors = candidates.size();
  const auto save   = [&] {
    row = {source, colors, all_regions, result};
    write_cache(path, row);
  };
  if (candidates.empty()) {
    save();
    return result;
  }
  auto* previous = Abc_FrameReadGlobalFrame();
  auto* frame    = Abc_FrameCreate();
  if (!frame) {
    return result;
  }
  Abc_FrameEnter(frame);
  {
    Gates                 gates;
    Cone                  cone(gates);
    std::vector<Mux_fact> outputs;
    for (const auto& [fact, shared_arms] : candidates) {
      const auto& arms = *shared_arms;
      try {
        auto* c        = cone.condition(arms, fact.arm);
        auto* a        = cone.bit(arms.values[fact.arm], fact.bit);
        auto* expected = fact.kind == Mux_fact::Kind::one ? gates.one() : gates.zero();
        if (fact.other >= 0) {
          expected = cone.bit(arms.values[fact.other], fact.bit);
          if (fact.kind == Mux_fact::Kind::complement) {
            expected = gates.inv(expected);
          }
        }
        auto* refute = gates.and_(c, gates.xor_(a, expected));
        auto* po     = Abc_NtkCreatePo(gates.ntk);
        Abc_ObjAddFanin(po, refute);
        auto name = std::format("f{}", outputs.size());
        Abc_ObjAssignName(po, const_cast<char*>(name.c_str()), nullptr);
        outputs.push_back(fact);
      } catch (const Unsupported&) {
      }
    }
    if (!outputs.empty()) {
      Abc_NtkAddDummyPiNames(gates.ntk);
      Abc_NtkAddDummyPoNames(gates.ntk);
      Abc_Ntk_t* logic = Abc_NtkToLogic(gates.ntk);
      if (logic) {
        Abc_FrameReplaceCurrentNetwork(frame, logic);
        if (Cmd_CommandExecute(frame, "strash; &get -n; &fraig -x -C 500; &put; strash") == 0) {
          auto* swept = Abc_FrameReadNtk(frame);
          if (Abc_NtkPoNum(swept) == static_cast<int>(outputs.size())) {
            for (size_t i = 0; i < outputs.size(); ++i) {
              auto* po = Abc_NtkPo(swept, static_cast<int>(i));
              if (Abc_ObjFanin0(po) == Abc_AigConst1(swept) && Abc_ObjFaninC0(po)) {
                result->mux[outputs[i].node].push_back(outputs[i]);
                ++result->proven;
              }
            }
          }
        }
      }
    }
  }
  Abc_FrameLeave(previous);
  Abc_FrameDestroy(frame);
  std::print("[pass.satopt] {}: {} candidates, {} seed survivors, {} proven mux facts\n",
             graph->get_name(),
             result->candidates,
             result->survivors,
             result->proven);
  save();
  return result;
}
bool                                    satopt_crosses(const Node& node) { return crosses(node, arms_of(node)); }
std::optional<std::vector<std::string>> satopt_region_facts(const Satopt_result& facts, const partition::Region_body& rb) {
  // Name each fact's subject by its expression over content-stable region port
  // names. Node IDs would attach the same cached fact to a different mux after
  // a harmless node renumbering, even when the region compare succeeds.
  absl::flat_hash_map<Pin, std::string> names;
  for (const auto& input : rb.inputs) {
    names[input.src_driver] = std::format("port{}:{}", input.name.size(), input.name);
  }
  absl::flat_hash_set<Pin>               visiting;
  std::function<std::string(const Pin&)> expression = [&](const Pin& p) -> std::string {
    if (auto it = names.find(p); it != names.end()) {
      return it->second;
    }
    if (p.is_const()) {
      return "const:" + satopt_constant_key(gu::const_of(p));
    }
    const auto  n   = p.get_master_node();
    std::string key = std::format("{}:{}:{}:{}", static_cast<int>(gu::type_op_of(n)), p.get_port_id(), width(p), gu::is_unsign(p));
    if (cut(p)) {
      const auto name = n.attr(hhds::attrs::name);
      if (!name.has() || name.get().empty()) {
        throw Unsupported{};
      }
      // Name-anchored state and opaque sources have the same identity gate as
      // the region structural comparison.
      key += std::format(":cut{}:{}", name.has() ? name.get().size() : 0, name.has() ? name.get() : "");
    } else {
      if (visiting.size() > 4096 || !visiting.insert(p).second) {
        throw Unsupported{};
      }
      std::vector<std::string> inputs;
      for (const auto& e : n.inp_edges()) {
        inputs.push_back(std::format("{}={}", e.sink.get_port_id(), expression(e.driver)));
      }
      std::sort(inputs.begin(), inputs.end());
      for (const auto& input : inputs) {
        if (key.size() + input.size() > 1048576) {
          throw Unsupported{};
        }
        key += std::format("|{}:{}", input.size(), input);
      }
      visiting.erase(p);
    }
    names[p] = key;
    return key;
  };
  std::vector<std::string> rows;
  for (const auto& n : rb.nodes) {
    auto it = facts.mux.find(static_cast<uint64_t>(n.get_debug_nid()));
    if (it == facts.mux.end() || !satopt_crosses(n)) {
      continue;
    }
    std::string subject;
    try {
      subject = expression(n.create_driver_pin(0));
    } catch (const Unsupported&) {
      return std::nullopt;
    }
    std::set<std::pair<int, int>> selected;
    for (const auto& f : it->second) {
      if (!selected.emplace(f.arm, f.bit).second) {
        continue;
      }
      rows.push_back(std::format("{}:{}:{}:{}:{}:{}", subject.size(), subject, f.arm, f.bit, static_cast<int>(f.kind), f.other));
    }
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}
}  // namespace livehd::abc
