// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "abc_incr.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "abc_map.hpp"  // Region_qor
#include "cell.hpp"     // Ntype_op
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/attrs/name.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "semdiff.hpp"  // structural_identical

namespace livehd::abc {

namespace {

namespace gu = livehd::graph_util;

// ABC_INCR_DEBUG: print, per region, which compare gate rejected a reuse. A
// reuse-eligible region that misses on a comment-only edit is a bug or a
// determinism gap; this makes the gate visible.
[[nodiscard]] bool incr_debug() {
  static const bool on = std::getenv("ABC_INCR_DEBUG") != nullptr;
  return on;
}

// ---------------------------------------------------------------------------
// Hash toolkit -- kept for Incr_cache::make_salt.
// ---------------------------------------------------------------------------
constexpr uint64_t mix64(uint64_t x) {
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}
constexpr uint64_t hcombine(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U))); }
uint64_t           hstr(std::string_view s) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// Incr_cache
// ---------------------------------------------------------------------------

Incr_cache::Incr_cache(std::string dir, uint64_t salt) : dir_(std::move(dir)), pre_dir_(dir_ + "_pre"), salt_(salt) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  std::filesystem::create_directories(pre_dir_, ec);

  std::ifstream in(dir_ + "/abc_cache.json", std::ios::binary);
  if (!in) {
    return;
  }
  std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  rapidjson::Document doc;
  doc.Parse(body.data(), body.size());
  // A corrupt, old-schema or wrong-salt file starts the cache cold -- it is only
  // ever a speedup, never a source of truth.
  if (doc.HasParseError() || !doc.IsObject()) {
    return;
  }
  if (auto s = doc.FindMember("schema"); s == doc.MemberEnd() || !s->value.IsInt() || s->value.GetInt() != 2) {
    return;
  }
  const std::string want = std::format("{:016x}", salt_);
  if (auto s = doc.FindMember("salt"); s == doc.MemberEnd() || !s->value.IsString() || want != s->value.GetString()) {
    return;
  }
  auto r = doc.FindMember("regions");
  if (r == doc.MemberEnd() || !r->value.IsObject()) {
    return;
  }
  for (auto it = r->value.MemberBegin(); it != r->value.MemberEnd(); ++it) {
    if (!it->value.IsObject()) {
      continue;
    }
    Row         row;
    const auto& v    = it->value;
    auto        gets = [&](const char* k) -> std::string {
      auto m = v.FindMember(k);
      return m != v.MemberEnd() && m->value.IsString() ? m->value.GetString() : std::string{};
    };
    auto arr = [&](const char* k, std::vector<std::string>& into) {
      if (auto m = v.FindMember(k); m != v.MemberEnd() && m->value.IsArray()) {
        for (const auto& e : m->value.GetArray()) {
          if (e.IsString()) {
            into.emplace_back(e.GetString());
          }
        }
      }
    };
    row.module = gets("module");
    row.pre    = gets("pre");
    row.recipe = gets("recipe");
    arr("in", row.in);
    arr("out", row.out);
    if (auto m = v.FindMember("gates"); m != v.MemberEnd() && m->value.IsInt()) {
      row.gates = m->value.GetInt();
    }
    if (auto m = v.FindMember("area"); m != v.MemberEnd() && m->value.IsNumber()) {
      row.area = m->value.GetDouble();
    }
    if (auto m = v.FindMember("delay"); m != v.MemberEnd() && m->value.IsNumber()) {
      row.delay = static_cast<float>(m->value.GetDouble());
    }
    row.crit_output = gets("crit_output");
    row.crit_src    = gets("crit_src");
    if (auto m = v.FindMember("div_blackbox"); m != v.MemberEnd() && m->value.IsInt()) {
      row.div_blackbox = m->value.GetInt();
    }
    if (!row.module.empty() && !row.pre.empty()) {
      rows_.emplace(it->name.GetString(), std::move(row));
    }
  }
}

hhds::GraphLibrary& Incr_cache::lib() { return livehd::Hhds_graph_library::instance(dir_); }
hhds::GraphLibrary& Incr_cache::cached_pre_lib() { return livehd::Hhds_graph_library::instance(pre_dir_); }

Incr_cache::Compare_result Incr_cache::lookup_compare(const livehd::partition::Region_body& rb, hhds::Graph* pre_body,
                                                      std::string_view recipe) {
  Compare_result res;
  auto           dbg = [&](const char* why) {
    if (incr_debug()) {
      std::print("[abc-incr] MISS {} -- {}\n", rb.module_name, why);
    }
  };
  if (!rb.reuse_eligible) {
    dbg("reuse-ineligible (automorphic boundary)");
    return res;
  }
  if (pre_body == nullptr) {
    dbg("pre-body rebuild failed");
    return res;
  }
  auto it = rows_.find(rb.module_name);
  if (it == rows_.end()) {
    dbg("no cached row for this module name");
    return res;
  }
  const Row& row = it->second;
  if (row.recipe != recipe) {
    dbg("recipe mismatch");
    return res;  // recipe gate (verbatim)
  }
  auto pio = cached_pre_lib().find_io(row.pre);
  if (!pio) {
    dbg("cached pre-body missing (no GraphIO)");
    return res;
  }
  auto cached_pre = pio->get_graph();
  if (!cached_pre) {
    dbg("cached pre-body missing (no body)");
    return res;
  }
  // The structural compare: name-blind on internal temporaries, name-anchored on
  // IO + state (matching_names). A genuine node-set bijection with every
  // compare-point obligation discharged -- a sound YES/NO, not a hash.
  livehd::semdiff::Semdiff_options so;
  so.matching_names = true;
  // Each Sub is an opaque blackbox (its body is a SEPARATE cache entry): breaks
  // combinational loops that run THROUGH a submodule and keys on the Sub's IO
  // wiring (def name + each port's name/width/sign, never node ids), so a
  // child-body edit does not invalidate this parent while a child INTERFACE change
  // does. The pre-bodies + their Sub child decls live in cached_pre_lib(), a
  // library separate from the mapped bodies, so both compare sides resolve the same
  // body-less decls. ON by default; opt-out via ABC_INCR_NO_BLACKBOX.
  so.blackbox_subs = std::getenv("ABC_INCR_NO_BLACKBOX") == nullptr;
  if (!livehd::semdiff::structural_identical(cached_pre.get(), pre_body, so)) {
    if (incr_debug()) {
      auto m = livehd::semdiff::structural_match(cached_pre.get(), pre_body, so);
      std::print(
          "[abc-incr] MISS {} -- structural NOT equal (a_unmatched={} b_unmatched={} cut_violated={} cut_unknown={} "
          "seed_pairs={} full_pairs={})\n",
          rb.module_name, m.a_unmatched, m.b_unmatched, m.cut_violated, m.cut_unknown, m.state.seed_pairs, m.state.full_pairs);
    }
    return res;
  }
  // Every cached port name must still exist on the fresh region (a stable-name
  // stitch: a missing name is a miss, never a guess).
  absl::flat_hash_set<std::string_view> fin, fout;
  fin.reserve(rb.inputs.size());
  fout.reserve(rb.outputs.size());
  for (const auto& p : rb.inputs) {
    fin.insert(p.name);
  }
  for (const auto& p : rb.outputs) {
    fout.insert(p.name);
  }
  for (const auto& s : row.in) {
    if (!fin.contains(s)) {
      dbg("cached input port name absent on fresh region");
      return res;
    }
  }
  for (const auto& s : row.out) {
    if (!fout.contains(s)) {
      dbg("cached output port name absent on fresh region");
      return res;
    }
  }
  res.hit         = true;
  res.row         = &row;
  res.crit_output = fout.contains(row.crit_output) ? row.crit_output : std::string{};
  return res;
}

// Copy the pre-body's body-less Sub child DECLS into the cache library alongside
// the pre-body itself. copy_from(pre_lib, pre_name) brings only the module, not
// the decls its Subs reference -- so without this the CACHED pre-body's Subs
// cannot resolve get_subnode_io(), and the compare's IO signature (folded from
// those decls) is present on the fresh side but absent on the cached side, a false
// cut_violated on every recompile. The children are IO-only decls (cheap); a Sub
// keyed by its stable subgraph NAME then resolves identically on both sides.
void Incr_cache::copy_pre_children(const livehd::partition::Region_body& rb, hhds::GraphLibrary& src_pre_lib) {
  if (rb.pre_body == nullptr) {
    return;
  }
  (void)src_pre_lib;  // recreated directly (below), not copy_from'd -- see why
  // Into cached_pre_lib(), which holds ONLY pre-bodies + their child decls (no
  // mapped bodies to shadow them), so the cached parent pre-body's Subs resolve to
  // the SAME interface the fresh side sees. RECREATE each child as an IO decl with
  // an EMPTY body rather than copy_from: copy_from calls get_graph() which ASSERTS
  // on a body-less decl (graph.hpp get_graph "unknown gid"; opt silently
  // misbehaves), and a body (even empty) is also what makes the decl survive the
  // cache save/load round-trip. create_io mints the same name-hash gid the copied
  // pre-body's Sub already references, so resolution lines up.
  auto& l = cached_pre_lib();
  for (auto n : rb.pre_body->fast_class()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    auto cio = n.get_subnode_io();
    if (!cio) {
      continue;
    }
    std::string cname{cio->get_name()};
    if (l.find_io(cname) != nullptr) {  // shared across parents; recreate once
      continue;
    }
    auto io = l.create_io(cname);
    for (const auto& p : cio->get_input_pin_decls()) {
      io->add_input(p.name, p.port_id, p.loop_break);
      if (p.bits != 0) {
        io->set_bits(p.name, p.bits);
      }
      io->set_unsign(p.name, p.unsign);
    }
    for (const auto& p : cio->get_output_pin_decls()) {
      io->add_output(p.name, p.port_id, p.loop_break);
      if (p.bits != 0) {
        io->set_bits(p.name, p.bits);
      }
      io->set_unsign(p.name, p.unsign);
    }
    io->create_graph()->commit();  // empty body: persists + no copy_from get_graph assert
  }
}

bool Incr_cache::store(const livehd::partition::Region_body& rb, hhds::GraphLibrary& pre_lib, std::string_view pre_name,
                       const Region_qor& q, std::string_view recipe, hhds::GraphLibrary* outlib) {
  // Snapshot the mapped body (rb.body, in outlib under module_name) into the mapped
  // library and the pre-abc body (in pre_lib under pre_name) into the SEPARATE
  // pre-body library, in memory. Kept apart so a mapped child body never shadows a
  // parent pre-body's Sub child decl of the same name (see cached_pre_lib).
  if (!lib().copy_from(*outlib, rb.module_name)) {
    return false;
  }
  if (!cached_pre_lib().copy_from(pre_lib, std::string{pre_name})) {
    return false;
  }
  copy_pre_children(rb, pre_lib);

  Row row;
  row.module = rb.module_name;
  row.pre    = std::string{pre_name};
  row.recipe = std::string{recipe};
  row.in.reserve(rb.inputs.size());
  row.out.reserve(rb.outputs.size());
  for (const auto& p : rb.inputs) {
    row.in.push_back(p.name);
  }
  for (const auto& p : rb.outputs) {
    row.out.push_back(p.name);
  }
  row.gates        = q.gates;
  row.area         = q.area;
  row.delay        = q.delay;
  row.crit_output  = q.crit_output;
  row.crit_src     = q.crit_src;
  row.div_blackbox = q.div_blackbox;

  rows_[rb.module_name] = std::move(row);
  dirty_                = true;
  return true;
}

bool Incr_cache::store_pre(const livehd::partition::Region_body& rb, hhds::GraphLibrary& pre_lib, std::string_view pre_name,
                           std::string_view recipe) {
  if (!cached_pre_lib().copy_from(pre_lib, std::string{pre_name})) {
    return false;
  }
  copy_pre_children(rb, pre_lib);
  Row row;
  row.module = rb.module_name;
  row.pre    = std::string{pre_name};
  row.recipe = std::string{recipe};
  row.in.reserve(rb.inputs.size());
  row.out.reserve(rb.outputs.size());
  for (const auto& p : rb.inputs) {
    row.in.push_back(p.name);
  }
  for (const auto& p : rb.outputs) {
    row.out.push_back(p.name);
  }
  rows_[rb.module_name] = std::move(row);
  dirty_                = true;
  return true;
}

bool Incr_cache::reuse_hit(const livehd::partition::Region_body& rb, const Compare_result& res, hhds::GraphLibrary* outlib) {
  if (!res.hit || res.row == nullptr) {
    return false;
  }
  auto mio = lib().find_io(res.row->module);
  if (!mio) {
    return false;
  }
  auto mapped = mio->get_graph();
  if (!mapped) {
    return false;
  }
  // Fill the freshly-partitioned region shell IN PLACE from the cached mapped
  // body (no clone, no port stitch; the name-hash gid and the writer handle stay
  // valid, so the partitioner commits it normally).
  if (!outlib->replace_body_from(rb.module_name, *mapped)) {
    return false;
  }
  ++hits_;
  return true;
}

void Incr_cache::save() {
  if (!dirty_) {
    return;
  }
  std::vector<const std::string*> keys;
  keys.reserve(rows_.size());
  for (const auto& [k, v] : rows_) {
    (void)v;
    keys.emplace_back(&k);
  }
  std::sort(keys.begin(), keys.end(), [](const auto* a, const auto* b) { return *a < *b; });

  auto jesc = [](std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
      switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default: o += c; break;
      }
    }
    return o;
  };

  std::string out   = std::format("{{\"schema\":2,\"salt\":\"{:016x}\",\"regions\":{{", salt_);
  bool        first = true;
  for (const auto* k : keys) {
    const auto& r = rows_.at(*k);
    if (!first) {
      out += ",";
    }
    first = false;
    out += std::format("\"{}\":{{\"module\":\"{}\",\"pre\":\"{}\",\"recipe\":\"{}\",\"in\":[",
                       jesc(*k),
                       jesc(r.module),
                       jesc(r.pre),
                       jesc(r.recipe));
    for (size_t i = 0; i < r.in.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", jesc(r.in[i]));
    }
    out += "],\"out\":[";
    for (size_t i = 0; i < r.out.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", jesc(r.out[i]));
    }
    out += std::format("],\"gates\":{},\"area\":{},\"delay\":{},\"crit_output\":\"{}\",\"crit_src\":\"{}\",\"div_blackbox\":{}}}",
                       r.gates,
                       r.area,
                       r.delay,
                       jesc(r.crit_output),
                       jesc(r.crit_src),
                       r.div_blackbox);
  }
  out += "}}";

  const std::string path = dir_ + "/abc_cache.json";
  const std::string tmp  = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      return;
    }
    f << out;
  }
  std::rename(tmp.c_str(), path.c_str());

  livehd::Hhds_graph_library::save(dir_);      // mapped bodies
  livehd::Hhds_graph_library::save(pre_dir_);  // pre-bodies + their Sub child decls
}

uint64_t Incr_cache::make_salt(std::string_view library_path, bool map_register, bool map_memory, std::string_view dff_cell,
                               bool use_proven_assume, bool use_all_assume) {
  // Bump the tag whenever the mapper's read-back or the cache shape changes:
  // stale bodies must never survive a semantic change.
  // v3: lgraph-compare cache -- keyed by module name, stores pre+mapped bodies,
  // structural_identical + verbatim recipe instead of a 128-bit digest.
  uint64_t      h = hstr("abc-incr-v3");
  std::ifstream f{std::string{library_path}, std::ios::binary};
  if (f) {
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    h = hcombine(h, hstr(bytes));
  } else {
    h = hcombine(h, hstr(library_path));
  }
  h = hcombine(h, static_cast<uint64_t>(map_register) << 1U | static_cast<uint64_t>(map_memory));
  h = hcombine(h, hstr(dff_cell));
  h = hcombine(h, static_cast<uint64_t>(use_proven_assume) << 1U | static_cast<uint64_t>(use_all_assume));
  return h;
}

}  // namespace livehd::abc
