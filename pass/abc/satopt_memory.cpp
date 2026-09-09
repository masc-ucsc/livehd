// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_memory.hpp"

#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <print>

#include "attr_carry.hpp"
#include "json_util.hpp"
#include "node_util.hpp"
#include "prove.hpp"
#include "rapidjson/document.h"
#include "satopt.hpp"

namespace livehd::abc {
namespace {
namespace gu         = livehd::graph_util;
using Pin            = hhds::Pin_class;
using Node           = hhds::Node_class;
using Verdict        = livehd::formal::Verdict;
constexpr int stride = 16;
constexpr int addr = 0, bits = 1, clock = 2, din = 3, enable = 4, fwd = 5, posclk = 6, type = 7, wensize = 8, size = 9, rdport = 10,
              init = 11, update = 12, reset = 14, undef = 15;
struct Port {
  Pin              a, d, en, clk;
  std::vector<int> original;
};
struct Build {
  hhds::Graph& g;
  Node         original;
  Pin          konst(int64_t v) { return gu::create_const(g, *Dlop::create_integer(v)); }
  Node         node(Ntype_op op) {
    auto n = gu::create_typed_node(g, op);
    if (gu::has_color(original)) {
      gu::set_color(n, gu::color_of(original));
    }
    return n;
  }
  Pin output(Node n, int width) {
    auto p = n.create_driver_pin(0);
    gu::set_ubits(p, width);
    return p;
  }
  Pin binary(Ntype_op op, Pin a, Pin b, int width) {
    auto n = node(op);
    a.connect_sink(n.create_sink_pin(0));
    b.connect_sink(n.create_sink_pin(0));
    return output(n, width);
  }
  Pin boolean(Pin a) {
    if (a.is_const()) {
      return konst(!gu::const_of(a).is_known_zero());
    }
    auto n = node(Ntype_op::Ror);
    a.connect_sink(n.create_sink_pin(0));
    return output(n, 1);
  }
  Pin mux(Pin s, Pin a, Pin b, int width) {
    if (a == b) {
      return a;
    }
    auto n = node(Ntype_op::Mux);
    s.connect_sink(n.create_sink_pin(0));
    a.connect_sink(n.create_sink_pin(1));
    b.connect_sink(n.create_sink_pin(2));
    return output(n, width);
  }
  static int pin_width(Pin p) { return p.is_const() ? std::max(1, gu::const_of(p).get_bits()) : std::max(1, gu::real_width(p)); }
  Pin        zero_extend(Pin p, int target) {
    const int source = pin_width(p);
    if (source >= target) {
      return p;
    }
    auto n = node(Ntype_op::Concat);
    konst(0).connect_sink(n.create_sink_pin(0));
    konst(target - source).connect_sink(n.create_sink_pin(1));
    p.connect_sink(n.create_sink_pin(2));
    konst(source).connect_sink(n.create_sink_pin(3));
    return output(n, target);
  }
  Pin address_mux(Pin s, Pin a, Pin b) {
    const int target = std::max(pin_width(a), pin_width(b));
    return mux(s, zero_extend(a, target), zero_extend(b, target), target);
  }
  Pin mask(Pin a, const Dlop& value, int width) {
    auto n = node(Ntype_op::Get_mask);
    a.connect_sink(n.create_sink_pin(0));
    gu::create_const(g, value).connect_sink(n.create_sink_pin(1));
    return output(n, width);
  }
  Pin slice(Pin a, int lo, int width) { return mask(a, *Dlop::get_mask_value(lo + width - 1, lo), width); }
  Pin pack(const std::vector<Pin>& lanes, int width) {
    if (lanes.size() == 1) {
      return lanes.front();
    }
    auto n = node(Ntype_op::Concat);
    for (int i = static_cast<int>(lanes.size()) - 1; i >= 0; --i) {
      konst(width).connect_sink(n.create_sink_pin(2 * i + 1));
      lanes[lanes.size() - 1 - i].connect_sink(n.create_sink_pin(2 * i));
    }
    return output(n, static_cast<int>(lanes.size()) * width);
  }
};
int literal(Pin p, int fallback) {
  if (!p.is_const()) {
    return fallback;
  }
  const auto& c = gu::const_of(p);
  if (c.has_unknowns() || !c.is_just_i64()) {
    return fallback;
  }
  const auto value = c.to_just_i64();
  return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max() ? static_cast<int>(value) : fallback;
}
int  width(Pin p) { return p.is_const() ? std::max(1, gu::const_of(p).get_bits()) : std::max(1, gu::bits_of(p)); }
bool same(Pin a, Pin b) { return a == b || (a.is_const() && b.is_const() && gu::const_of(a).is_known_eq(gu::const_of(b))); }

struct Memory_queries {
  std::string                 source;
  std::map<std::string, bool> answers;
  template <class Query>
  bool ask(const std::string& key, const Query& query) {
    if (auto it = answers.find(key); it != answers.end()) {
      return it->second;
    }
    const bool result = query();
    answers.emplace(key, result);
    return result;
  }
};
std::map<std::string, Memory_queries> query_cache;
std::string                           queries_path(std::string_view dir, std::string_view name) {
  if (dir.empty()) {
    return {};
  }
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : name) {
    hash = (hash ^ c) * 1099511628211ULL;
  }
  return std::format("{}/{:016x}-memory.json", dir, hash);
}
void read_queries(const std::string& path, Memory_queries& queries, const std::string& source) {
  queries = {source, {}};
  std::ifstream       input(path);
  std::string         text((std::istreambuf_iterator<char>(input)), {});
  rapidjson::Document doc;
  doc.Parse(text.c_str());
  if (!doc.IsObject() || !doc.HasMember("source") || !doc["source"].IsString() || doc["source"].GetString() != source
      || !doc.HasMember("answers") || !doc["answers"].IsObject()) {
    return;
  }
  for (const auto& member : doc["answers"].GetObject()) {
    if (member.value.IsBool()) {
      queries.answers[member.name.GetString()] = member.value.GetBool();
    }
  }
}
void write_queries(const std::string& path, const Memory_queries& queries) {
  if (path.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
  if (error) {
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
  out << std::format("{{\"source\":\"{}\",\"answers\":{{", json_util::escape(queries.source));
  bool comma = false;
  for (const auto& [key, value] : queries.answers) {
    out << std::format("{}\"{}\":{}", comma ? "," : "", json_util::escape(key), value ? "true" : "false");
    comma = true;
  }
  out << "}}\n";
  out.close();
  if (out) {
    std::filesystem::rename(temporary, path, error);
  }
  std::filesystem::remove(temporary, error);
}

// Prefilter only: an unavailable seed never rules a candidate in or proves it.
struct Proofs {
  Memory_queries&                       cache;
  absl::flat_hash_map<Pin, std::string> identities;
  std::string                           identity(Pin p) {
    if (p.is_const()) {
      return "const:" + satopt_constant_key(gu::const_of(p));
    }
    if (auto it = identities.find(p); it != identities.end()) {
      return it->second;
    }
    auto        n   = p.get_master_node();
    std::string key = std::format("{}:{}:{}:{}:{}",
                                  n.get_debug_nid(),
                                  p.get_port_id(),
                                  static_cast<int>(gu::type_op_of(n)),
                                  width(p),
                                  gu::is_unsign(p));
    // The whole source descriptor gates the original graph. Include the direct
    // inputs too so generated enable/address helpers cannot share a query key
    // merely because their allocation order gave them the same node id.
    for (const auto& e : n.inp_edges()) {
      key += std::format("|{}:{}:{}",
                         e.sink.get_port_id(),
                         e.driver.get_debug_pid(),
                         e.driver.is_const() ? satopt_constant_key(gu::const_of(e.driver)) : "");
    }
    identities[p] = key;
    return key;
  }
  int            address_bits;
  Satopt_seeds   seeds;
  formal::Prover prover;
  explicit Proofs(hhds::Graph* graph, int depth, Memory_queries& queries)
      : cache(queries)
      , address_bits(std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(depth - 1)))))
      , prover(graph, {.budget_k = 256, .cone_max = 50000, .memory_as_symbols = true, .reject_unknown_constants = true}) {}
  bool dead(Pin en) {
    if (en.is_known_false()) {
      return true;
    }
    auto values = seeds.sample(en);
    if (values) {
      for (const auto& v : *values) {
        if (!v.is_known_zero()) {
          return false;
        }
      }
    }
    return cache.ask("dead:" + identity(en), [&] { return prover.is_false(en).verdict == Verdict::Proven; });
  }
  bool exclusive(Pin a, Pin b) {
    auto va = seeds.sample(a), vb = seeds.sample(b);
    if (va && vb) {
      for (int i = 0; i < 8; ++i) {
        if (!(*va)[i].is_known_zero() && !(*vb)[i].is_known_zero()) {
          return false;
        }
      }
    }
    return cache.ask("exclusive:" + identity(a) + ":" + identity(b),
                     [&] { return prover.are_exclusive({a, b}).verdict == Verdict::Proven; });
  }
  bool relation(const Port& a, const Port& b, bool equal) {
    if (equal && same(a.a, b.a)) {
      return true;
    }
    auto aa = seeds.sample(a.a), ba = seeds.sample(b.a), ae = seeds.sample(a.en), be = seeds.sample(b.en);
    if (aa && ba && ae && be) {
      for (int i = 0; i < 8; ++i) {
        if ((*ae)[i].is_known_zero() || (*be)[i].is_known_zero()) {
          continue;
        }
        if ((*aa)[i].is_known_eq((*ba)[i]) != equal) {
          return false;
        }
      }
    }
    auto key = std::format("relation:{}:{}:{}:{}:{}:{}",
                           address_bits,
                           equal,
                           identity(a.a),
                           identity(b.a),
                           identity(a.en),
                           identity(b.en));
    return cache.ask(key, [&] {
      auto result = equal ? prover.equal_when(a.a, b.a, {a.en, b.en}, std::max(width(a.a), width(b.a)))
                          : prover.never_collide(a.a, b.a, {a.en, b.en}, address_bits);
      return result.verdict == Verdict::Proven;
    });
  }
};

void optimize(hhds::Graph& g, Node mem, Memory_satopt& stats, Memory_queries& queries) {
  std::map<int, Pin>                globals;
  std::map<int, std::map<int, Pin>> blocks;
  for (const auto& e : mem.inp_edges()) {
    int raw = e.sink.get_port_id(), off = raw % stride;
    if (off == addr || off == clock || off == din || off == enable || off == rdport) {
      blocks[raw / stride][off] = e.driver;
    } else {
      globals[off] = e.driver;
    }
  }
  const int word = literal(globals[bits], 0), depth = literal(globals[size], 0), lanes = literal(globals[wensize], 1);
  const int kind = literal(globals[type], 0);
  if (word <= 0 || word > 65536 || depth <= 0 || lanes <= 0 || word % lanes || kind == 2 || !globals[update].is_invalid()
      || !globals[reset].is_invalid() || literal(globals[posclk], 1) != 1) {
    return;
  }
  if (auto attr = mem.attr(livehd::attrs::memory_async_reset); attr.has() && attr.get() != 0) {
    return;
  }
  for (int matrix : {fwd, undef}) {
    if (!globals[matrix].is_invalid() && (!globals[matrix].is_const() || gu::const_of(globals[matrix]).has_unknowns())) {
      return;
    }
  }
  Build B{g, mem};
  Pin   shared_clock;
  for (const auto& [block, fields] : blocks) {
    (void)block;
    if (auto it = fields.find(clock); it != fields.end()) {
      shared_clock = it->second;
      break;
    }
  }
  std::vector<Port> writes, reads;
  for (auto& [block, fields] : blocks) {
    (void)block;
    if (fields[addr].is_invalid()) {
      continue;
    }
    bool read = literal(fields[rdport], fields[din].is_invalid() ? 1 : 0) == 1;
    Port p{fields[addr], fields[din], fields[enable], fields[clock], {static_cast<int>((read ? reads : writes).size())}};
    if (p.clk.is_invalid()) {
      p.clk = shared_clock;
    }
    if (p.en.is_invalid()) {
      p.en = read ? B.konst(1) : gu::create_const(g, *Dlop::get_mask_value(lanes));
    }
    if ((!p.a.is_const() && !gu::is_unsign(p.a)) || (p.a.is_const() && gu::const_of(p.a).is_negative())) {
      return;
    }
    if (!read && p.d.is_invalid()) {
      return;
    }
    (read ? reads : writes).push_back(p);
  }
  // Transform only the same-edge model that the Memory cell can represent.
  for (const auto& p : writes) {
    if (p.clk != shared_clock) {
      return;
    }
  }
  if (kind == 1) {
    for (const auto& p : reads) {
      if (p.clk != shared_clock) {
        return;
      }
    }
  }
  const auto                     original_reads = reads;
  const size_t                   old_writes     = writes.size();
  std::vector<std::vector<bool>> forward(reads.size(), std::vector<bool>(writes.size())), unknown = forward;
  for (size_t r = 0; r < reads.size(); ++r) {
    for (size_t w = 0; w < writes.size(); ++w) {
      forward[r][w] = globals[fwd].is_const() && gu::const_of(globals[fwd]).bit_test(r * writes.size() + w);
      unknown[r][w] = globals[undef].is_const() && gu::const_of(globals[undef]).bit_test(r * writes.size() + w);
    }
  }
  Proofs proof(&g, depth, queries);
  bool   changed     = false;
  // Remove a fixed address dimension only when every access has that same
  // bit, the depth is a power of two, and no whole-array view can observe the
  // discarded entries. Preserve the selected initialization words exactly.
  bool   has_readall = false;
  for (const auto& e : mem.out_edges()) {
    has_readall |= e.driver.get_port_id() == Ntype::Memory_readall_pid;
  }
  if (!has_readall && depth > 1 && (depth & (depth - 1)) == 0 && depth <= 65536 && static_cast<uint64_t>(word) * depth <= 1048576
      && (globals[init].is_invalid() || (globals[init].is_const() && !gu::const_of(globals[init]).has_unknowns()))) {
    for (int bit = 0; (uint64_t{1} << bit) < static_cast<uint64_t>(depth); ++bit) {
      std::optional<bool> fixed;
      bool                agrees = !writes.empty() || !reads.empty();
      for (const auto* ports : {&writes, &reads}) {
        for (const auto& port : *ports) {
          auto values = proof.seeds.sample(port.a);
          if (!values || (!port.a.is_const() && !gu::is_unsign(port.a))) {
            agrees = false;
            break;
          }
          bool value = (*values)[0].bit_test(bit);
          for (const auto& seed : *values) {
            agrees &= seed.bit_test(bit) == value;
          }
          if (fixed && *fixed != value) {
            agrees = false;
          }
          fixed = value;
          if (!agrees) {
            break;
          }
          if (port.a.is_const()) {
            agrees &= !gu::const_of(port.a).is_negative();
          } else if (bit >= width(port.a)) {
            agrees &= !value;
          } else {
            agrees &= queries.ask(std::format("bit:{}:{}:{}", proof.identity(port.a), bit, value),
                                  [&] { return proof.prover.constant_bit(port.a, bit, value).verdict == Verdict::Proven; });
          }
        }
      }
      if (!agrees || !fixed) {
        continue;
      }
      if (globals[init].is_const()) {
        const auto&       old = gu::const_of(globals[init]);
        std::vector<Dlop> words;
        words.reserve(depth / 2);
        for (int entry = depth / 2 - 1; entry >= 0; --entry) {
          int original = ((entry >> bit) << (bit + 1)) | (static_cast<int>(*fixed) << bit) | (entry & ((1 << bit) - 1));
          words.push_back(*old.get_mask_op(Dlop::get_mask_value((original + 1) * word - 1, original * word)));
        }
        std::vector<Dlop::Concat_lane> init_lanes;
        for (const auto& value : words) {
          init_lanes.push_back({&value, word});
        }
        globals[init] = gu::create_const(g, *Dlop::concat_op(init_lanes));
      }
      globals[size] = B.konst(depth / 2);
      for (auto* ports : {&writes, &reads}) {
        for (auto& port : *ports) {
          if (port.a.is_const()) {
            const auto& value  = gu::const_of(port.a);
            auto        select = Dlop::get_mask_value(std::max(width(port.a), bit + 1));
            select             = select->xor_op(Dlop::get_mask_value(bit, bit));
            port.a             = gu::create_const(g, *value.get_mask_op(select));
          } else if (bit < width(port.a)) {
            auto select = Dlop::get_mask_value(width(port.a));
            select      = select->xor_op(Dlop::get_mask_value(bit, bit));
            port.a      = B.mask(port.a, *select, std::max(1, width(port.a) - 1));
          }
        }
      }
      proof.address_bits = std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(depth / 2 - 1))));
      ++stats.address_bits;
      changed = true;
      break;
    }
  }
  // First remove unreachable collision windows; merging may then combine ports
  // whose original forwarding/undefined rows were different.
  for (size_t r = 0; r < reads.size(); ++r) {
    for (size_t w = 0; w < writes.size(); ++w) {
      auto observed = reads[r];
      // The RTL applies forwarding after read-enable gating. A forwarding
      // collision is observable even while the ordinary read is disabled.
      if (forward[r][w]) {
        observed.en = B.konst(1);
      }
      if ((forward[r][w] || unknown[r][w]) && proof.relation(observed, writes[w], false)) {
        stats.collision_bits += forward[r][w] + unknown[r][w];
        forward[r][w] = unknown[r][w] = false;
        changed                       = true;
      }
    }
  }
  std::erase_if(writes, [&](const Port& p) {
    if (!proof.dead(p.en)) {
      return false;
    }
    ++stats.dead_ports;
    changed = true;
    return true;
  });
  std::erase_if(reads, [&](const Port& p) {
    for (int rr : p.original) {
      if (std::any_of(forward[rr].begin(), forward[rr].end(), [](bool bit) { return bit; })) {
        return false;
      }
    }
    if (!proof.dead(p.en)) {
      return false;
    }
    ++stats.dead_ports;
    changed = true;
    return true;
  });
  // A bounded pair worklist keeps a pathological multi-port memory from
  // monopolizing synthesis. Budget-outs preserve the original behavior.
  size_t pairs = 0;
  for (size_t a = 0; a < writes.size(); ++a) {
    for (size_t b = a + 1; b < writes.size() && pairs < 4096;) {
      ++pairs;
      auto& x          = writes[a];
      auto& y          = writes[b];
      bool  compatible = true;
      for (const auto& r : reads) {
        for (int rr : r.original) {
          for (int xx : x.original) {
            for (int yy : y.original) {
              compatible &= forward[rr][xx] == forward[rr][yy] && unknown[rr][xx] == unknown[rr][yy];
            }
          }
        }
      }
      // Only adjacent write groups may be merged: moving a lower-priority write
      // past an intervening conflicting write would change last-writer-wins.
      bool adjacent   = b == a + 1;
      bool structural = same(x.a, y.a) && x.en.is_const() && y.en.is_const() && !gu::const_of(x.en).has_unknowns()
                        && !gu::const_of(y.en).has_unknowns();
      if (!compatible || !adjacent || (!structural && !proof.exclusive(x.en, y.en))) {
        ++b;
        continue;
      }
      auto select = B.boolean(y.en);
      x.a         = B.address_mux(select, x.a, y.a);
      if (structural && lanes > 1) {
        std::vector<Pin> values;
        for (int lane = 0; lane < lanes; ++lane) {
          values.push_back(B.slice(gu::const_of(y.en).bit_test(lane) ? y.d : x.d, lane * (word / lanes), word / lanes));
        }
        x.d = B.pack(values, word);
      } else {
        x.d = B.mux(select, x.d, y.d, word);
      }
      x.en = B.binary(Ntype_op::Or, x.en, y.en, lanes);
      x.original.insert(x.original.end(), y.original.begin(), y.original.end());
      writes.erase(writes.begin() + static_cast<std::ptrdiff_t>(b));
      ++stats.merged_writes;
      changed = true;
    }
  }
  pairs = 0;
  for (size_t a = 0; a < reads.size(); ++a) {
    for (size_t b = a + 1; b < reads.size() && pairs < 4096;) {
      ++pairs;
      auto& x          = reads[a];
      auto& y          = reads[b];
      bool  compatible = x.clk == y.clk;
      for (const auto& w : writes) {
        for (int ww : w.original) {
          for (int xx : x.original) {
            for (int yy : y.original) {
              compatible &= forward[xx][ww] == forward[yy][ww] && unknown[xx][ww] == unknown[yy][ww];
            }
          }
        }
      }
      // Disabled read values are don't-cares except for forwarded lanes.
      // Preserve those independently observable bypasses on separate ports.
      if (!x.en.is_known_true() || !y.en.is_known_true()) {
        for (int rr : x.original) {
          compatible &= std::none_of(forward[rr].begin(), forward[rr].end(), [](bool bit) { return bit; });
        }
        for (int rr : y.original) {
          compatible &= std::none_of(forward[rr].begin(), forward[rr].end(), [](bool bit) { return bit; });
        }
      }
      if (!compatible || (!proof.relation(x, y, true) && !proof.exclusive(x.en, y.en))) {
        ++b;
        continue;
      }
      x.a  = B.address_mux(B.boolean(y.en), x.a, y.a);
      x.en = B.binary(Ntype_op::Or, B.boolean(x.en), B.boolean(y.en), 1);
      x.original.insert(x.original.end(), y.original.begin(), y.original.end());
      reads.erase(reads.begin() + static_cast<std::ptrdiff_t>(b));
      ++stats.merged_reads;
      changed = true;
    }
  }
  Node result = mem;
  if (changed) {
    result = B.node(Ntype_op::Memory);
    gu::carry_node_attrs(mem, result);
    gu::carry_srcid(mem, result);
    for (const auto& [off, p] : globals) {
      if (!p.is_invalid() && off != fwd && off != undef) {
        p.connect_sink(result.create_sink_pin(off));
      }
    }
    const auto connect = [&](const Port& p, int block, bool read) {
      p.a.connect_sink(result.create_sink_pin(block * stride + addr));
      if (!p.clk.is_invalid()) {
        p.clk.connect_sink(result.create_sink_pin(block * stride + clock));
      }
      p.en.connect_sink(result.create_sink_pin(block * stride + enable));
      if (!read) {
        p.d.connect_sink(result.create_sink_pin(block * stride + din));
      }
      B.konst(read).connect_sink(result.create_sink_pin(block * stride + rdport));
    };
    for (size_t w = 0; w < writes.size(); ++w) {
      connect(writes[w], static_cast<int>(w), false);
    }
    for (size_t r = 0; r < reads.size(); ++r) {
      connect(reads[r], static_cast<int>(writes.size() + r), true);
    }
    for (int matrix : {fwd, undef}) {
      std::string value(std::max<size_t>(1, reads.size() * writes.size()), '0');
      for (size_t r = 0; r < reads.size(); ++r) {
        for (size_t w = 0; w < writes.size(); ++w) {
          bool set = (matrix == fwd ? forward : unknown)[reads[r].original.front()][writes[w].original.front()];
          value[value.size() - 1 - (r * writes.size() + w)] = set ? '1' : '0';
        }
      }
      gu::create_const(g, *Dlop::from_binary(value, true)).connect_sink(result.create_sink_pin(matrix));
    }
    absl::flat_hash_map<Pin, Pin> rewired;
    for (const auto& e : mem.out_edges()) {
      int pid = e.driver.get_port_id();
      Pin replacement;
      if (pid == Ntype::Memory_readall_pid) {
        replacement = result.create_driver_pin(pid);
      } else {
        if (pid < static_cast<int>(old_writes) || pid >= static_cast<int>(old_writes + original_reads.size())) {
          continue;
        }
        int ordinal = pid - static_cast<int>(old_writes);
        for (size_t r = 0; r < reads.size(); ++r) {
          if (std::find(reads[r].original.begin(), reads[r].original.end(), ordinal) != reads[r].original.end()) {
            replacement = result.create_driver_pin(static_cast<hhds::Port_id>(writes.size() + r));
            break;
          }
        }
        if (replacement.is_invalid()) {
          replacement = B.konst(0);
        }
      }
      if (!replacement.is_const()) {
        gu::carry_pin_attrs(e.driver, replacement);
      }
      rewired[e.driver] = replacement;
      replacement.connect_sink(e.sink);
    }
    for (auto* ports : {&writes, &reads}) {
      for (auto& port : *ports) {
        for (auto* pin : {&port.a, &port.d, &port.en, &port.clk}) {
          if (auto found = rewired.find(*pin); found != rewired.end()) {
            *pin = found->second;
          }
        }
      }
    }
    mem.del_node();
  }
  // Rebuild the prover after port rewrites: its memo must never outlive a
  // mutation of a memory output it previously cut to a free symbol.
  Proofs priority(&g, literal(globals[size], depth), queries);
  auto&  independent = stats.independent[{std::string(g.get_name()), static_cast<uint64_t>(result.get_debug_nid())}];
  pairs              = 0;
  for (size_t a = 0; a < writes.size(); ++a) {
    for (size_t b = a + 1; b < writes.size() && pairs < 4096; ++b) {
      ++pairs;
      if (priority.relation(writes[a], writes[b], false)) {
        independent.emplace(static_cast<int>(a), static_cast<int>(b));
      }
    }
  }
}
}  // namespace
Memory_satopt optimize_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view cache_dir) {
  Memory_satopt result;
  for (const auto& graph : graphs) {
    if (!graph) {
      continue;
    }
    std::vector<Node> memories;
    for (const auto node : graph->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Memory) {
        memories.push_back(node);
      }
    }
    if (memories.empty()) {
      continue;
    }
    const auto source  = satopt_source_key(graph.get());
    const auto path    = queries_path(cache_dir, graph->get_name());
    auto&      queries = query_cache[std::string(graph->get_name())];
    if (queries.source != source) {
      read_queries(path, queries, source);
    }
    for (const auto& node : memories) {
      optimize(*graph, node, result, queries);
    }
    write_queries(path, queries);
  }
  std::print("[pass.satopt] memory: {} write merges, {} read merges, {} dead ports, {} unreachable collision bits\n",
             result.merged_writes,
             result.merged_reads,
             result.dead_ports,
             result.collision_bits);
  return result;
}
}  // namespace livehd::abc
