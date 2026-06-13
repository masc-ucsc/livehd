//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast.hpp"

#include <charconv>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {
// Pre-register every Lnast attribute tag at static-init time. The HHDS
// attribute registry is not thread-safe on first-touch — two threads racing
// to register the same tag both find the registry empty and both try to
// insert, tripping the assert in register_tag. Pre-registering before main()
// ensures the lazy `attr()` path on Node_class only takes the early-return
// branch.
struct Lnast_attr_init {
  Lnast_attr_init() {
    hhds::register_attr_tag<hhds::attrs::name_t>("hhds::attrs::name");
    // hhds::attrs::srcid self-registers (srcid.hpp), so no entry here.
  }
};
[[maybe_unused]] const Lnast_attr_init lnast_attr_init_{};
}  // namespace

void Lnast_node::dump() const { std::print("{}, {}\n", Lnast_ntype::debug_name(get_type()), get_name()); }

Lnast::Lnast(std::string_view _module_name) : forest_(hhds::Forest::create()), top_module_name(_module_name) {
  // Each Lnast owns one tree body in its private forest. The TreeIO carries
  // the module name; the Tree body is created lazily here so that
  // get_root() works only after set_root() has stamped the top node.
  treeio_ = forest_->create_io(top_module_name);
  // Take the writable handle only to transition the slot to Public, then
  // hold the raw shared_ptr via get_tree(). Holding the writable handle
  // long-term keeps the slot in SlotState::Writing, which blocks
  // TreeIO::replace() and find_tree*/find_tree_rw queries.
  {
    auto writable = treeio_->create_tree();
  }
  tree_ = treeio_->get_tree();
}

Lnast::Lnast(std::shared_ptr<hhds::Tree> body, std::string_view _module_name)
    : tree_(std::move(body)), top_module_name(_module_name) {
  // Tree-only wrapper: no Forest, no TreeIO. replace_body() will assert.
  if (tree_ && !top_module_name.empty()) {
    tree_->set_name(top_module_name);
  }
}

Lnast::~Lnast() = default;

void Lnast::replace_body(std::shared_ptr<hhds::Tree> new_body) {
  I(treeio_, "replace_body: this Lnast was not constructed with a TreeIO");
  // Drop our raw ref so the body's only owner is the slot (otherwise
  // replace's keep_previous default-false path would still see refs).
  tree_.reset();
  treeio_->replace(std::move(new_body));
  tree_ = treeio_->get_tree();
}

void Lnast::export_into(hhds::Forest& forest) const {
  auto tio = forest.find_io(top_module_name);
  if (!tio) {
    tio           = forest.create_io(top_module_name);
    // Materialize the slot so it is Public and replace() below can swap it.
    auto writable = tio->create_tree();
  }
  // clone() deep-copies the body INCLUDING the flat-storage attribute stores
  // (name/srcid), so the exported unit is a faithful copy.
  auto body = tree_->clone();
  // Union this unit's locator into the forest-level table (Forest::save only
  // writes it — the tree-side union is the exporter's job) and rewrite the
  // clone's srcid values through the remap. Matching spans re-mint to the same
  // id, so the remap is empty except on a true hash collision.
  if (!srcloc_.empty()) {
    const auto remap = forest.source_map().merge(srcloc_);
    if (!remap.empty() && body->has_attr(hhds::attrs::srcid)) {
      auto& ids = body->attr_store(hhds::attrs::srcid);
      for (auto& [key, value] : ids) {
        if (const auto it = remap.find(value); it != remap.end()) {
          value = it->second;
        }
      }
    }
  }
  tio->replace(std::move(body));
}

std::shared_ptr<Lnast> Lnast::adopt(std::shared_ptr<hhds::Forest> forest, std::string_view module_name) {
  if (!forest) {
    return nullptr;
  }
  auto tio = forest->find_io(module_name);
  if (!tio) {
    return nullptr;
  }
  auto lnast             = std::make_shared<Lnast>(module_name);
  lnast->forest_         = std::move(forest);
  lnast->treeio_         = std::move(tio);
  lnast->tree_           = lnast->treeio_->get_tree();
  lnast->top_module_name = std::string{module_name};
  // Loaded srcids live in the Forest's table (srcmap.txt); chain to it as a
  // read-only base so span_of resolves them. forest_ is co-owned, so the base
  // outlives this Lnast.
  lnast->srcloc_.set_base(&lnast->forest_->source_map());
  return lnast;
}

// ─────────────────────────────────────────────────────────────────────────
// Mutation
// ─────────────────────────────────────────────────────────────────────────

Lnast_nid Lnast::set_root(Lnast_ntype::Lnast_ntype_int type) {
  auto root = tree_->add_root_node();
  set_data(root, type);
  return root;
}

Lnast_nid Lnast::add_child(const Lnast_nid& parent, Lnast_ntype::Lnast_ntype_int type) {
  auto child = parent.add_child();
  set_data(child, type);
  return child;
}

Lnast_nid Lnast::add_child(const Lnast_nid& parent, const Lnast_node& n) {
  auto child = parent.add_child();
  set_data(child, n);
  return child;
}

// ─────────────────────────────────────────────────────────────────────────
// Payload accessors. The Lnast_ntype enum lives in the native HHDS Type
// slot (uint16_t). Token/text/fname ride on flat-storage attributes.
// ─────────────────────────────────────────────────────────────────────────

Lnast_ntype::Lnast_ntype_int Lnast::get_type(const Lnast_nid& nid) const {
  return static_cast<Lnast_ntype::Lnast_ntype_int>(nid.get_type());
}

void Lnast::set_type(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int t) { nid.set_type(static_cast<hhds::Type>(t)); }

std::string_view Lnast::get_name(const Lnast_nid& nid) const {
  auto ref = nid.attr(hhds::attrs::name);
  if (!ref.has()) {
    return {};
  }
  return ref.get();
}

void Lnast::set_name(const Lnast_nid& nid, std::string_view name) {
  if (name.empty()) {
    auto ref = nid.attr(hhds::attrs::name);
    if (ref.has()) {
      ref.del();
    }
    return;
  }
  nid.attr(hhds::attrs::name).set(std::string(name));
}

uint32_t Lnast::tmp_site_hash(const Lnast_nid& ref_nid, const absl::flat_hash_map<std::string, std::string>* remap) const {
  constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
  constexpr uint64_t kFnvPrime  = 0x100000001b3ULL;

  uint64_t h   = kFnvOffset;
  auto     mix = [&h](std::string_view s) {
    for (const unsigned char c : s) {
      h ^= c;
      h *= kFnvPrime;
    }
    h ^= 0xffu;  // field separator so ("ab","c") and ("a","bc") differ
    h *= kFnvPrime;
  };

  const auto parent = get_parent(ref_nid);
  if (parent.is_invalid()) {
    mix(get_name(ref_nid));
  } else {
    // The type name (not the enum value) so the hash survives ntype-table
    // reorderings; leaf kinds are tagged so ref 'x' and const 'x' differ.
    mix(Lnast_ntype::debug_name(get_type(parent)));
    for (const auto& c : children(parent)) {
      if (c == ref_nid) {
        continue;
      }
      const auto t = get_type(c);
      if (Lnast_ntype::is_ref(t) || Lnast_ntype::is_const(t)) {
        std::string_view txt = get_name(c);
        if (remap != nullptr) {
          const auto it = remap->find(txt);
          if (it != remap->end()) {
            txt = it->second;
          }
        }
        mix(Lnast_ntype::is_ref(t) ? "r" : "c");
        mix(txt);
      } else {
        mix("n");
        mix(Lnast_ntype::debug_name(t));
      }
    }
  }
  return static_cast<uint32_t>(h ^ (h >> 32));
}

hhds::SourceId Lnast::get_srcid(const Lnast_nid& nid) const {
  auto ref = nid.attr(hhds::attrs::srcid);
  if (!ref.has()) {
    return hhds::SourceId_invalid;
  }
  return ref.get();
}

void Lnast::set_srcid(const Lnast_nid& nid, hhds::SourceId id) {
  if (id == hhds::SourceId_invalid) {
    return;
  }
  nid.attr(hhds::attrs::srcid).set(id);
}

livehd::diag::Span Lnast::span_of(const Lnast_nid& nid) const {
  if (nid.is_invalid()) {
    return {};
  }
  return srcloc_.resolve_span(get_srcid(nid));
}

std::vector<livehd::diag::Note> Lnast::notes_of(const Lnast_nid& nid, std::string_view message) const {
  if (nid.is_invalid()) {
    return {};
  }
  return livehd::diag::notes_from(srcloc_.resolve_spans(get_srcid(nid)), message);
}

void Lnast::set_data(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int type) {
  set_type(nid, type);
  set_name(nid, {});
  if (pending_srcid_ != hhds::SourceId_invalid && srcid_carries(type)) {
    set_srcid(nid, pending_srcid_);
  }
}

void Lnast::set_data(const Lnast_nid& nid, const Lnast_node& n) {
  set_type(nid, n.get_type());
  set_name(nid, n.get_name());
  if (pending_srcid_ != hhds::SourceId_invalid && srcid_carries(n.get_type())) {
    set_srcid(nid, pending_srcid_);
  }
}

// ─────────────────────────────────────────────────────────────────────────
// print / dump
//
// hhds split: print() is pretty-only (box-drawing, not round-trippable);
// dump() goes through write_dump and is round-trippable by hhds
// Tree::read_dump (with lnast_type_table()); the lnast-side attributes
// (name, src) ride in the dump as `'<name>' @(loc=..,fname=..)`.
// ─────────────────────────────────────────────────────────────────────────

namespace {

// Build a Type_entry table indexed by Lnast_ntype enum value. The Type
// stored on each tree node is Lnast_ntype's uint8_t, so type_table[i].name
// must equal Lnast_ntype::to_sv(i) for read_dump to round-trip.
std::span<const hhds::Type_entry> lnast_type_table() {
  static const auto table = []() {
    std::vector<hhds::Type_entry> t;
    t.reserve(Lnast_ntype::Lnast_ntype_last_invalid);
    for (uint8_t i = 0; i < Lnast_ntype::Lnast_ntype_last_invalid; ++i) {
      t.push_back({Lnast_ntype::to_sv(static_cast<Lnast_ntype::Lnast_ntype_int>(i)), hhds::Statement_class::Node});
    }
    return t;
  }();
  return table;
}

}  // namespace

void Lnast::print(std::ostream& os, const Lnast_nid& root_nid) const {
  hhds::Tree::PrintOptions opts;
  opts.show_types = false;
  opts.node_text  = [this](const hhds::Tree::Node_class& node) {
    const auto t = get_type(node);
    const auto n = get_name(node);
    return std::format("{}: {}", Lnast_ntype::to_sv(t), n);
  };
  opts.attributes.emplace_back("loc", [this](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
    const auto span = span_of(node);
    if (span.is_null()) {
      return std::nullopt;
    }
    return std::format("{}-{}@{}", span.start_byte.value_or(0), span.end_byte.value_or(0), span.file);
  });
  tree_->print(os, root_nid, opts);
}

void Lnast::dump(std::ostream& os) const {
  hhds::Tree::PrintOptions opts;
  opts.type_table = lnast_type_table();
  opts.show_types = true;
  // node_text is just the variable name (the type column already carries
  // the lnast type). hhds write_dump suppresses node_text only when it
  // equals the type name, so we echo the type name back for unnamed nodes
  // — that prints as bare "assign" instead of "assign ''".
  opts.node_text  = [this](const hhds::Tree::Node_class& node) -> std::string {
    auto n = get_name(node);
    if (n.empty()) {
      return std::string(Lnast_ntype::to_sv(get_type(node)));
    }
    return std::string(n);
  };
  opts.attributes.emplace_back("src", [this](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
    const auto id = get_srcid(node);
    if (id == hhds::SourceId_invalid) {
      return std::nullopt;
    }
    return std::to_string(id);
  });
  tree_->write_dump(os, opts);
}

void Lnast::dump(const std::string& filename) const {
  std::ofstream ofs(filename);
  if (!ofs.is_open()) {
    // iassert's I() has no formatted 3-arg form; report like every other
    // recoverable error path (a std::runtime_error the caller classifies).
    throw std::runtime_error(std::format("lnast.dump: cannot open {} for writing", filename));
  }
  dump(ofs);
}
