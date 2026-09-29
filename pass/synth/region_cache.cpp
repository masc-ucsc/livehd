// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "region_cache.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "region_qor.hpp"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"  // Ntype_op
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "hash_util.hpp"
#include "hhds/attrs/name.hpp"
#include "json_util.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"
#include "semdiff.hpp"  // structural_identical

namespace livehd::synth {

namespace {

namespace gu = livehd::graph_util;

// ABC_INCR_DEBUG: print, per region, which compare gate rejected a reuse. A
// reuse-eligible region that misses on a comment-only edit is a bug or a
// determinism gap; this makes the gate visible.
[[nodiscard]] bool incr_debug() {
  static const bool on = std::getenv("ABC_INCR_DEBUG") != nullptr;
  return on;
}

using hash_util::combine64;
using hash_util::fnv1a64;

std::string digest_key(uint64_t h0, uint64_t h1, std::string_view recipe) {
  return std::format("{:016x}{:016x}|{}", h0, h1, recipe);
}

bool parse_hex64(std::string_view text, uint64_t& value) {
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  return ec == std::errc{} && ptr == text.data() + text.size();
}

// Re-declare a Sub's child def (`cio`) into `dst` if absent, cloning its IO
// (names/widths/signs/port-ids/loop_break) -- never a copy_from (which asserts on
// a body-less decl). `with_body` mints an empty body: REQUIRED in a cache library
// so the decl survives the save/load round-trip, but OMITTED for an output netlist
// library where a body-less blackbox is what the fresh mapping (blackbox_io)
// produces -- so a reused cell decl is byte-for-byte what a cold map would emit.
void recreate_child_decl(hhds::GraphLibrary& dst, const hhds::GraphIO& cio, bool with_body) {
  std::string cname{cio.get_name()};
  if (dst.find_io(cname) != nullptr) {
    return;  // already present (shared across parents, or a real bodied def) -- never clobber
  }
  auto io = dst.create_io(cname);
  for (const auto& p : cio.get_input_pin_decls()) {
    io->add_input(p.name, p.port_id, p.loop_break);
    if (p.bits != 0) {
      io->set_bits(p.name, p.bits);
    }
    io->set_unsign(p.name, p.unsign);
  }
  for (const auto& p : cio.get_output_pin_decls()) {
    io->add_output(p.name, p.port_id, p.loop_break);
    if (p.bits != 0) {
      io->set_bits(p.name, p.bits);
    }
    io->set_unsign(p.name, p.unsign);
  }
  if (with_body) {
    io->create_graph()->commit();  // empty body: persists across the cache save/load
  }
}

// True when `cio` owns REAL logic, not just an IO shell. A Liberty/DFF cell decl
// (and the empty placeholder recreate_child_decl leaves behind) has no body
// node; abc_map's shared input-bit splitter def does. The two must be carried
// through the cache differently: a cell is re-declared, a bodied helper def has
// to be COPIED with its body or the reused region reads undriven bits.
bool has_body_logic(const hhds::GraphIO& cio) {
  auto g = const_cast<hhds::GraphIO&>(cio).get_graph();
  if (!g) {
    return false;
  }
  for ([[maybe_unused]] auto n : g->body().nodes()) {
    return true;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Region_cache
// ---------------------------------------------------------------------------

Region_cache::Region_cache(std::string dir, uint64_t salt, bool scoped_libraries)
    : dir_(std::move(dir)), pre_dir_(dir_ + "_pre"), salt_(salt), scoped_libraries_(scoped_libraries) {
  if (!scoped_libraries_) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::filesystem::create_directories(pre_dir_, ec);
  }

  std::ifstream in(dir_ + "/abc_cache.json", std::ios::binary);
  if (!in) {
    return;
  }
  std::string         body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  rapidjson::Document doc;
  doc.Parse(body.data(), body.size());
  // A corrupt, old-schema or wrong-salt file starts the cache cold -- it is only
  // ever a speedup, never a source of truth.
  if (doc.HasParseError() || !doc.IsObject()) {
    return;
  }
  if (auto s = doc.FindMember("schema"); s == doc.MemberEnd() || !s->value.IsInt() || s->value.GetInt() != 4) {
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
    row.module        = gets("module");
    row.pre           = gets("pre");
    row.recipe        = gets("recipe");
    row.evidence_file = gets("evidence_file");
    if (auto m = v.FindMember("evidence_bytes"); m != v.MemberEnd() && m->value.IsUint64()) {
      row.evidence_bytes = m->value.GetUint64();
    }
    if (!parse_hex64(gets("evidence_hash"), row.evidence_hash)) {
      row.evidence_file.clear();
    }
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
    if (auto m = v.FindMember("logic_depth"); m != v.MemberEnd() && m->value.IsInt()) {
      row.logic_depth = m->value.GetInt();
    }
    row.crit_output = gets("crit_output");
    row.crit_src    = gets("crit_src");
    if (auto m = v.FindMember("div_blackbox"); m != v.MemberEnd() && m->value.IsInt()) {
      row.div_blackbox = m->value.GetInt();
    }
    const auto digest = gets("digest");
    if (digest.size() == 32 && parse_hex64(std::string_view{digest}.substr(0, 16), row.digest0)
        && parse_hex64(std::string_view{digest}.substr(16), row.digest1) && (row.digest0 != 0 || row.digest1 != 0)) {
      row.digest_valid = true;
    }
    if (!row.module.empty() && !row.pre.empty()) {
      rows_.emplace(it->name.GetString(), std::move(row));
    }
  }
  for (const auto& [name, row] : rows_) {
    if (row.digest_valid) {
      digest_index_[digest_key(row.digest0, row.digest1, row.recipe)].push_back(name);
    }
  }
}

namespace {
hhds::GraphLibrary& scoped_library(std::unique_ptr<hhds::GraphLibrary>& holder, const std::string& path) {
  if (!holder) {
    holder = std::make_unique<hhds::GraphLibrary>();
    if (std::filesystem::is_regular_file(std::filesystem::path(path) / "library.txt")) {
      holder->load(path);
    }
  }
  return *holder;
}
}  // namespace

hhds::GraphLibrary& Region_cache::lib() {
  return scoped_libraries_ ? scoped_library(scoped_mapped_, dir_) : livehd::Hhds_graph_library::instance(dir_);
}
hhds::GraphLibrary& Region_cache::cached_pre_lib() {
  return scoped_libraries_ ? scoped_library(scoped_pre_, pre_dir_) : livehd::Hhds_graph_library::instance(pre_dir_);
}

Region_cache::Compare_result Region_cache::lookup_compare(const livehd::partition::Region_body& rb, hhds::Graph* pre_body,
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
  const auto digest
      = livehd::semdiff::canonical_digest(pre_body, {}, livehd::semdiff::Sub_fold::interface, /*matching_io_names=*/false);
  std::vector<const Row*> candidates;
  if (auto it = rows_.find(rb.module_name); it != rows_.end()) {
    candidates.push_back(&it->second);
  }
  if (digest.valid) {
    if (auto it = digest_index_.find(digest_key(digest.h0, digest.h1, recipe)); it != digest_index_.end()) {
      for (const auto& name : it->second) {
        if (name == rb.module_name) {
          continue;
        }
        if (auto row = rows_.find(name); row != rows_.end()) {
          candidates.push_back(&row->second);
        }
      }
    }
  }
  if (candidates.empty()) {
    dbg("no same-name or same-digest cached row");
    return loaded_snapshot_ ? loaded_snapshot_->lookup_compare(rb, pre_body, recipe) : res;
  }

  for (const Row* candidate : candidates) {
    const Row& row        = *candidate;
    const bool cross_name = row.module != rb.module_name;
    if (row.recipe != recipe) {
      continue;
    }
    // Same-name reuse predates the digest and remains governed by the exact
    // comparison. A digest is a cross-name discovery index, never a rejection
    // oracle for the established path.
    if (cross_name && (!digest.valid || !row.digest_valid || digest.h0 != row.digest0 || digest.h1 != row.digest1)) {
      continue;
    }
    auto pio = cached_pre_lib().find_io(row.pre);
    if (!pio) {
      continue;
    }
    auto cached_pre = pio->get_graph();
    if (!cached_pre) {
      continue;
    }
    if (!cross_name) {
      // Exact comparison below matches named IO, while replace_body_from
      // installs the pin table by numeric port id. Equal names/logic alone
      // do not authorize that copy when partitioning reorders the boundary.
      // Refuse this candidate until reuse can explicitly transport a port
      // permutation (including IO attributes and direct feedthroughs).
      auto fresh_io    = pre_body->get_io();
      auto same_layout = [](const auto& cached, const auto& fresh) {
        if (cached.size() != fresh.size()) {
          return false;
        }
        absl::flat_hash_map<std::string_view, hhds::Port_id> ports;
        for (const auto& port : cached) {
          ports.emplace(port.name, port.port_id);
        }
        for (const auto& port : fresh) {
          auto it = ports.find(port.name);
          if (it == ports.end() || it->second != port.port_id) {
            return false;
          }
        }
        return true;
      };
      if (!fresh_io || !same_layout(pio->get_input_pin_decls(), fresh_io->get_input_pin_decls())
          || !same_layout(pio->get_output_pin_decls(), fresh_io->get_output_pin_decls())) {
        dbg("same-name boundary changed numeric port layout");
        continue;
      }
    }
    // The structural compare: name-blind on internal temporaries, name-anchored
    // on IO + state. The digest only found this candidate; this exact proof is
    // what authorizes reuse.
    livehd::semdiff::Semdiff_options so;
    so.matching_names    = true;
    so.matching_io_names = !cross_name;
    so.blackbox_subs     = std::getenv("ABC_INCR_NO_BLACKBOX") == nullptr;
    if (!livehd::semdiff::structural_identical(cached_pre.get(), pre_body, so)) {
      bool rescued = std::getenv("ABC_INCR_NO_TRAVERSAL") == nullptr
                     && livehd::semdiff::structural_equivalent_traversal(cached_pre.get(), pre_body, so);
      if (!rescued) {
        if (incr_debug()) {
          auto m = livehd::semdiff::structural_match(cached_pre.get(), pre_body, so);
          std::print(
              "[abc-incr] MISS {} -- structural NOT equal (a_unmatched={} b_unmatched={} cut_violated={} cut_unknown={} "
              "seed_pairs={} full_pairs={})\n",
              rb.module_name,
              m.a_unmatched,
              m.b_unmatched,
              m.cut_violated,
              m.cut_unknown,
              m.state.seed_pairs,
              m.state.full_pairs);
        }
        continue;
      }
      if (incr_debug()) {
        std::print("[abc-incr] RESCUE {} -- exact traversal proved equivalent past a stalled signature\n", rb.module_name);
      }
    }

    // A stable-name stitch: a missing port is a miss, never a guess.
    absl::flat_hash_set<std::string_view> fin, fout;
    fin.reserve(rb.inputs.size());
    fout.reserve(rb.outputs.size());
    for (const auto& p : rb.inputs) {
      fin.insert(p.name);
    }
    for (const auto& p : rb.outputs) {
      fout.insert(p.name);
    }
    bool ports_match = row.in.size() == fin.size() && row.out.size() == fout.size();
    if (!cross_name) {
      for (const auto& s : row.in) {
        ports_match &= fin.contains(s);
      }
      for (const auto& s : row.out) {
        ports_match &= fout.contains(s);
      }
    }
    if (!ports_match) {
      continue;
    }

    res.hit = true;
    res.row = &row;
    if (!cross_name) {
      res.crit_output = fout.contains(row.crit_output) ? row.crit_output : std::string{};
    } else if (!row.crit_output.empty()) {
      auto cached_io = cached_pre->get_io();
      auto fresh_io  = pre_body->get_io();
      if (cached_io && fresh_io) {
        // Port_invalid must stay unmatched: without the guard an unresolvable
        // cached name falls through to whatever fresh decl happens to carry an
        // invalid port id, which is a GUESS at the critical output -- the same
        // "a missing port is a miss, never a guess" rule the stitch above keeps.
        hhds::Port_id crit_pid = hhds::Port_invalid;
        for (const auto& decl : cached_io->get_output_pin_decls()) {
          if (decl.name == row.crit_output) {
            crit_pid = decl.port_id;
            break;
          }
        }
        if (crit_pid != hhds::Port_invalid) {
          for (const auto& decl : fresh_io->get_output_pin_decls()) {
            if (decl.port_id == crit_pid) {
              res.crit_output = decl.name;
              break;
            }
          }
        }
      }
    }
    return res;
  }
  dbg("cached candidates failed exact comparison");
  return loaded_snapshot_ ? loaded_snapshot_->lookup_compare(rb, pre_body, recipe) : res;
}

// Copy the pre-body's body-less Sub child DECLS into the cache library alongside
// the pre-body itself. copy_from(pre_lib, pre_name) brings only the module, not
// the decls its Subs reference -- so without this the CACHED pre-body's Subs
// cannot resolve get_subnode_io(), and the compare's IO signature (folded from
// those decls) is present on the fresh side but absent on the cached side, a false
// cut_violated on every recompile. The children are IO-only decls (cheap); a Sub
// keyed by its stable subgraph NAME then resolves identically on both sides.
void Region_cache::copy_pre_children(const livehd::partition::Region_body& rb, hhds::GraphLibrary& src_pre_lib) {
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
  for (auto n : rb.pre_body->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (auto cio = n.get_subnode_io()) {
      recreate_child_decl(l, *cio, /*with_body=*/true);  // empty body: survives cache save/load
    }
  }
}

// Copy the MAPPED body's leaf-cell Sub child decls (liberty cells, DFF cells --
// declared into `outlib` only lazily when a region MAPS, via abc_map's
// blackbox_io) into lib() next to the mapped body, so the cached body is
// self-contained. Without this, an all-HIT recompile never maps any region, so
// the cells are never declared and the reused netlist drops them at emission /
// cannot be encoded for LEC (they resolve get_subnode_io to nothing). A bodied
// child region already sits in lib() under its own name (stored children-first),
// so `recreate_child_decl`'s find-or-skip leaves it untouched -- only the leaf
// blackbox cells are added.
void Region_cache::copy_mapped_children(std::string_view module_name, hhds::GraphLibrary& outlib) {
  auto mio = outlib.find_io(module_name);
  if (!mio) {
    return;
  }
  auto mapped = mio->get_graph();
  if (!mapped) {
    return;
  }
  auto& l = lib();
  for (auto n : mapped->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (auto cio = n.get_subnode_io()) {  // resolves in outlib (cells declared during this map)
      const std::string cname{cio->get_name()};
      if (l.find_io(cname) == nullptr && has_body_logic(*cio)) {
        if (l.copy_from(outlib, cname)) {
          continue;  // bodied helper def (input-bit splitter): keep its LOGIC
        }
      }
      recreate_child_decl(l, *cio, /*with_body=*/true);
    }
  }
}

bool Region_cache::store(const livehd::partition::Region_body& rb, hhds::GraphLibrary& pre_lib, std::string_view pre_name,
                       const Region_qor& q, std::string_view recipe, hhds::GraphLibrary* outlib) {
  // The pre-abc body (in pre_lib under pre_name) is copied NOW, into the
  // SEPARATE pre-body library: `pre_lib` is a per-region throwaway the
  // partitioner destroys the moment this callback returns. The two cache
  // libraries are kept apart so a mapped child body never shadows a parent
  // pre-body's Sub child decl of the same name (see cached_pre_lib).
  //
  // The MAPPED body is NOT copied: it stays in `outlib` for the rest of the run
  // and save() flushes it from there, so the mapping phase never holds two
  // copies of every netlist (see the header, and Row::in_outlib for how same-run
  // reuse finds it in the meantime).
  if (outlib == nullptr || outlib->find_io(rb.module_name) == nullptr) {
    return false;  // nothing to defer: keep store()'s false-on-failure contract
  }
  if (saved_generation_readable_ && !loaded_snapshot_) {
    auto previous = rows_.find(rb.module_name);
    if (previous != rows_.end() && !previous->second.in_outlib) {
      // The on-disk generation is still unchanged until save/freeze. Its
      // private pre library avoids the mutable cached_pre_lib() that store
      // overwrites below. Bodies stay lazy; do not duplicate every netlist.
      loaded_snapshot_ = std::make_unique<Region_cache>(dir_, salt_, true);
    }
  }
  if (!cached_pre_lib().copy_from(pre_lib, std::string{pre_name})) {
    return false;
  }
  copy_pre_children(rb, pre_lib);
  outlib_ = outlib;

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
  row.logic_depth  = q.logic_depth;
  row.crit_output  = q.crit_output;
  row.crit_src     = q.crit_src;
  row.div_blackbox = q.div_blackbox;
  if (q.hook_evidence && q.hook_evidence->size() <= max_evidence_bytes) {
    row.evidence = q.hook_evidence;
  }
  const auto digest
      = livehd::semdiff::canonical_digest(rb.pre_body, {}, livehd::semdiff::Sub_fold::interface, /*matching_io_names=*/false);
  row.digest0      = digest.h0;
  row.digest1      = digest.h1;
  row.digest_valid = digest.valid;
  row.in_outlib    = true;  // body still only in `outlib`; save() flushes it

  rows_[rb.module_name] = std::move(row);
  const auto& stored    = rows_.at(rb.module_name);
  if (stored.digest_valid) {
    digest_index_[digest_key(stored.digest0, stored.digest1, stored.recipe)].push_back(rb.module_name);
  }
  dirty_ = true;
  return true;
}

bool Region_cache::store_pre(const livehd::partition::Region_body& rb, hhds::GraphLibrary& pre_lib, std::string_view pre_name,
                           std::string_view recipe) {
  if (!cached_pre_lib().copy_from(pre_lib, std::string{pre_name})) {
    return false;
  }
  copy_pre_children(rb, pre_lib);
  Row row;
  row.module = rb.module_name;
  row.pre    = std::string{pre_name};
  row.recipe = std::string{recipe};
  const auto digest
      = livehd::semdiff::canonical_digest(rb.pre_body, {}, livehd::semdiff::Sub_fold::interface, /*matching_io_names=*/false);
  row.digest0      = digest.h0;
  row.digest1      = digest.h1;
  row.digest_valid = digest.valid;
  row.in.reserve(rb.inputs.size());
  row.out.reserve(rb.outputs.size());
  for (const auto& p : rb.inputs) {
    row.in.push_back(p.name);
  }
  for (const auto& p : rb.outputs) {
    row.out.push_back(p.name);
  }
  rows_[rb.module_name] = std::move(row);
  const auto& stored    = rows_.at(rb.module_name);
  if (stored.digest_valid) {
    digest_index_[digest_key(stored.digest0, stored.digest1, stored.recipe)].push_back(rb.module_name);
  }
  dirty_ = true;
  return true;
}

bool Region_cache::reuse_hit(const livehd::partition::Region_body& rb, const Compare_result& res, hhds::GraphLibrary* outlib) {
  if (!res.hit || res.row == nullptr || outlib == nullptr) {
    return false;  // no output library -> nowhere to install the reused body (deref'd below)
  }
  // A row stored EARLIER IN THIS RUN still has its mapped body only in `outlib`
  // (store() defers the cache-library copy to save()); a row loaded from disk
  // has it in lib(). Same-run reuse therefore reads the netlist straight out of
  // the output library -- no second copy has to exist for it to work.
  hhds::GraphLibrary& src = res.row->in_outlib ? *outlib : lib();
  auto                mio = src.find_io(res.row->module);
  if (!mio) {
    return false;
  }
  auto mapped = mio->get_graph();
  if (!mapped) {
    return false;
  }
  if (incr_debug()) {
    std::print("[abc-incr] HIT {} <- {} ({})\n",
               rb.module_name,
               res.row->module,
               res.row->in_outlib ? "current output" : "saved cache");
  }
  // Re-declare the reused body's leaf-cell Sub defs (liberty cells, DFF cells)
  // into `outlib` FIRST. On an all-HIT recompile no region maps, so abc_map's
  // blackbox_io never runs and these decls would be absent -- the reused netlist
  // would then drop the cells at Verilog emission and be un-encodable for LEC
  // (issue: dino_synth_lec_synth / lhd_abc_incr_test). Body-less, exactly as a
  // cold map's blackbox_io produces (their behavior comes from the Liberty
  // models at LEC / the library at synthesis). A bodied child region already sits
  // in `outlib` (stored children-first), so find-or-skip leaves it untouched.
  for (auto n : mapped->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (auto cio = n.get_subnode_io()) {  // resolves in `src` (copy_mapped_children stored it)
      const std::string cname{cio->get_name()};
      if (outlib->find_io(cname) == nullptr && has_body_logic(*cio)) {
        // Bodied helper def (input-bit splitter): a decl-only clone would emit
        // an EMPTY module and the reused region's inputs would read undriven.
        // Unreachable when src IS outlib -- the child already resolves there.
        if (outlib->copy_from(src, cname)) {
          continue;
        }
      }
      recreate_child_decl(*outlib, *cio, /*with_body=*/false);
    }
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

void Region_cache::refresh_qor(std::string_view module, const Region_qor& q) {
  auto it = rows_.find(std::string{module});
  if (it == rows_.end() || !it->second.in_outlib) {
    return;
  }
  it->second.area  = q.area;
  it->second.delay = q.delay;
  it->second.gates = q.gates;
  dirty_           = true;
}

void Region_cache::freeze_pending() {
  // After freezing, lib() may contain the new owner of an old name. A previous
  // row must no longer direct reuse_hit to that name in the changed library.
  loaded_snapshot_.reset();
  saved_generation_readable_ = false;
  if (!dirty_) {
    return;
  }
  // Flush the bodies store() deferred. Doing it here, once, is what keeps the
  // mapping phase down to ONE copy of each mapped netlist: `outlib` owns them
  // while ABC is live, and the duplicate only exists after the last region is
  // done. A body that has gone missing drops its row rather than persisting a
  // metadata entry the next run would compare against and then fail to reuse.
  if (outlib_ != nullptr) {
    std::vector<std::string> lost;
    for (auto& [name, row] : rows_) {
      if (!row.in_outlib) {
        continue;
      }
      if (!lib().copy_from(*outlib_, row.module)) {
        lost.push_back(name);
        continue;
      }
      copy_mapped_children(row.module, *outlib_);  // self-contain the leaf-cell decls
      row.in_outlib = false;
    }
    for (const auto& name : lost) {
      rows_.erase(name);
    }
  }
}

void Region_cache::save() { save_to(dir_, false); }

bool Region_cache::stage_snapshot(const std::string& directory) {
  if (!dirty_) {
    return false;
  }
  // Materialize old lazy bodies before saving elsewhere. This avoids relying
  // on the library's best-effort pending-body directory copy for a transaction.
  for (auto* library : {&lib(), &cached_pre_lib()}) {
    for (auto gid : library->all_gids()) {
      [[maybe_unused]] auto graph = library->get_graph(gid);
    }
  }
  save_to(directory, true);
  return true;
}

std::shared_ptr<const std::string> Region_cache::read_evidence(const Row& row) const {
  if (row.evidence) {
    return row.evidence->size() <= max_evidence_bytes ? row.evidence : nullptr;
  }
  const std::filesystem::path relative(row.evidence_file);
  // Only our generated two-component paths are admitted, never traversal or
  // absolute paths from a damaged manifest.
  if (relative.empty() || relative.is_absolute() || relative.parent_path().parent_path() != ""
      || !relative.parent_path().string().starts_with("evidence-") || relative.filename().extension() != ".json"
      || row.evidence_bytes == 0 || row.evidence_bytes > max_evidence_bytes) {
    return nullptr;
  }
  std::ifstream input(std::filesystem::path(dir_) / relative, std::ios::binary | std::ios::ate);
  if (!input || input.tellg() != static_cast<std::streamoff>(row.evidence_bytes)) {
    return nullptr;
  }
  input.seekg(0);
  auto data = std::make_shared<std::string>(row.evidence_bytes, '\0');
  input.read(data->data(), static_cast<std::streamsize>(data->size()));
  if (!input || input.peek() != std::char_traits<char>::eof() || fnv1a64(*data) != row.evidence_hash) {
    return nullptr;
  }
  return data;
}

void Region_cache::save_to(const std::string& directory, bool strict) {
  if (!dirty_) {
    return;
  }
  freeze_pending();
  std::vector<const std::string*> keys;
  keys.reserve(rows_.size());
  for (const auto& [k, v] : rows_) {
    (void)v;
    keys.emplace_back(&k);
  }
  std::sort(keys.begin(), keys.end(), [](const auto* a, const auto* b) { return *a < *b; });

  std::filesystem::create_directories(directory);
  std::string evidence_directory;
  size_t      evidence_index = 0;

  std::string out   = std::format("{{\"schema\":4,\"salt\":\"{:016x}\",\"regions\":{{", salt_);
  bool        first = true;
  for (const auto* k : keys) {
    const auto& r = rows_.at(*k);
    std::string evidence_file;
    auto        evidence = read_evidence(r);
    if (evidence && !evidence->empty()) {
      if (evidence_directory.empty()) {
        evidence_directory = directory + "/evidence-XXXXXX";
        if (mkdtemp(evidence_directory.data()) == nullptr) {
          throw std::runtime_error("cannot stage synthesis cache evidence directory");
        }
      }
      evidence_file
          = (std::filesystem::path(evidence_directory).filename() / (std::to_string(evidence_index++) + ".json")).string();
      std::ofstream file(std::filesystem::path(directory) / evidence_file, std::ios::binary | std::ios::trunc);
      file.write(evidence->data(), static_cast<std::streamsize>(evidence->size()));
      file.close();
      if (!file) {
        throw std::runtime_error("cannot stage synthesis cache evidence");
      }
    }
    if (!first) {
      out += ",";
    }
    first  = false;
    out   += std::format("\"{}\":{{\"module\":\"{}\",\"pre\":\"{}\",\"recipe\":\"{}\",\"in\":[",
                         json_util::escape(*k),
                         json_util::escape(r.module),
                         json_util::escape(r.pre),
                         json_util::escape(r.recipe));
    for (size_t i = 0; i < r.in.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", json_util::escape(r.in[i]));
    }
    out += "],\"out\":[";
    for (size_t i = 0; i < r.out.size(); ++i) {
      out += std::format("{}\"{}\"", i != 0 ? "," : "", json_util::escape(r.out[i]));
    }
    out += std::format(
        "],\"gates\":{},\"area\":{},\"delay\":{},\"logic_depth\":{},\"crit_output\":\"{}\",\"crit_src\":\"{}\",\"div_blackbox\":{},"
        "\"digest\":\"{:"
        "016x}{:016x}\",\"evidence_file\":\"{}\",\"evidence_bytes\":{},\"evidence_hash\":\"{:016x}\"}}",
        r.gates,
        r.area,
        r.delay,
        r.logic_depth,
        json_util::escape(r.crit_output),
        json_util::escape(r.crit_src),
        r.div_blackbox,
        r.digest0,
        r.digest1,
        json_util::escape(evidence_file),
        evidence ? evidence->size() : 0,
        evidence ? fnv1a64(*evidence) : 0);
  }
  out += "}}";

  const std::string path = directory + "/abc_cache.json";
  const std::string tmp  = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      if (strict) {
        throw std::runtime_error("cannot create staged synthesis cache metadata");
      }
      return;
    }
    f << out;
    f.close();
    if (strict && !f) {
      throw std::runtime_error("cannot serialize staged synthesis cache metadata");
    }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0 && strict) {
    throw std::runtime_error("cannot install staged synthesis cache metadata");
  }

  if (scoped_libraries_ || strict) {
    lib().save(directory);
    cached_pre_lib().save(directory + "_pre");
  } else {
    livehd::Hhds_graph_library::save(dir_);      // mapped bodies
    livehd::Hhds_graph_library::save(pre_dir_);  // pre-bodies + their Sub child decls
  }
}

uint64_t Region_cache::make_salt(uint64_t engine_salt, std::string_view library_path, bool map_register, Memory_fold memory_fold,
                                 uint64_t memory_max_bits, std::string_view dff_desc) {
  // The caller's engine salt (a backend's generated source salt, which folds
  // pass/synth's) automatically covers mapper/read-back revision changes. Keep the schema tag for persistent on-disk shape changes
  // that older readers cannot parse; stale bodies must never survive either.
  // v3: lgraph-compare cache -- keyed by module name, stores pre+mapped bodies,
  // structural_identical + verbatim recipe instead of a 128-bit digest.
  // v4: mapped body now self-contains its leaf-cell Sub decls (copy_mapped_children)
  // so an all-HIT reuse can re-declare them into outlib -- a v3 cache lacks them.
  // v5: partition boundary INPUT naming is now bidirectional (producer + consumer
  // cone, Proposal 2), so a v4 cache's port names no longer match.
  // v6: the formal-assume don't-care inputs left the salt with the EXDC item
  // -- a v5 salt mixed in two bools that no longer exist.
  // v7: rows carry canonical digests and same-run cross-name reuse is enabled.
  // v9: hashing moved to core/hash_util (canonical FNV offset basis; the local
  // copy used the truncated one) -- every salt/digest value shifted, so the
  // tag is bumped to keep this history honest (v8 values never coexist).
  // v10: the DFF item is the RESOLVED cell descriptor (name:d:clk:q:inverted),
  // and a mapped body may hold QN-cell Subs whose latch carried ~D; a v9 cache
  // keyed on the empty raw option could serve DFFHQx4 bodies under the QN pick.
  // v11: mapped gate instances are named by the neutral cell index
  // (g<index>_<cell>, region_writer.cpp), no longer by ABC object ids.
  uint64_t h = fnv1a64("abc-incr-v11");
  h          = combine64(h, engine_salt);
  std::ifstream f{std::string{library_path}, std::ios::binary};
  if (f) {
    // Size-then-read, never istreambuf_iterator: the iterator form goes through
    // the streambuf one character at a time, which on the 46 MB merged ASAP7
    // Liberty costs 143 ms against 9 ms here. Measured A/B end to end, that is
    // 175 ms off the fixed floor of EVERY pass.abc run (0.753 s -> 0.578 s of
    // non-mapping time on a 1-cell design). Same bytes, so the salt is unchanged
    // and existing caches stay valid.
    f.seekg(0, std::ios::end);
    const auto len = f.tellg();
    f.seekg(0, std::ios::beg);
    std::string bytes;
    if (len > 0) {
      bytes.resize(static_cast<size_t>(len));
      f.read(bytes.data(), len);
      bytes.resize(static_cast<size_t>(f.gcount()));
    }
    h = combine64(h, fnv1a64(bytes));
  } else {
    h = combine64(h, fnv1a64(library_path));
  }
  h = combine64(h, static_cast<uint64_t>(map_register) << 1U | static_cast<uint64_t>(memory_fold == Memory_fold::Always));
  if (memory_fold == Memory_fold::Auto) {
    // Only `auto` reads the threshold, and only `auto` adds an item: the
    // explicit true/false salts stay exactly what they were before the mode
    // existed, so a cache written under them survives.
    h = combine64(h, memory_max_bits);
  }
  h = combine64(h, fnv1a64(dff_desc));
  return h;
}

}  // namespace livehd::synth
