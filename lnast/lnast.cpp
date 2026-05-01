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
    hhds::register_attr_tag<lnast::attrs::loc_t>("lnast.loc");
    hhds::register_attr_tag<lnast::attrs::fname_t>("lnast.fname");
  }
};
[[maybe_unused]] const Lnast_attr_init lnast_attr_init_{};
}  // namespace

void Lnast_node::dump() const { std::print("{}, {}\n", Lnast_ntype::debug_name(get_type()), get_name()); }

Lnast::Lnast(std::string_view _module_name, std::string_view _file_name)
    : forest_(hhds::Forest::create()), top_module_name(_module_name), source_filename(_file_name) {
  // Each Lnast owns one tree body in its private forest. The TreeIO carries
  // the module name; the Tree body is created lazily here so that
  // get_root() works only after set_root() has stamped the top node.
  treeio_ = forest_->create_io(top_module_name);
  tree_   = treeio_->create_tree();
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
  treeio_->replace(std::move(new_body));
  tree_ = treeio_->get_tree();
}

// ─────────────────────────────────────────────────────────────────────────
// Mutation
// ─────────────────────────────────────────────────────────────────────────

Lnast_nid Lnast::set_root(Lnast_ntype::Lnast_ntype_int type) {
  auto root = tree_->add_root_node();
  set_data(root, type);
  return root;
}

Lnast_nid Lnast::set_root(const Lnast_node& n) {
  auto root = tree_->add_root_node();
  set_data(root, n);
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

Lnast_nid Lnast::append_sibling(const Lnast_nid& sibling, Lnast_ntype::Lnast_ntype_int type) {
  auto next = sibling.append_sibling();
  set_data(next, type);
  return next;
}

Lnast_nid Lnast::append_sibling(const Lnast_nid& sibling, const Lnast_node& n) {
  auto next = sibling.append_sibling();
  set_data(next, n);
  return next;
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

Lnast::Loc Lnast::get_loc(const Lnast_nid& nid) const {
  auto ref = nid.attr(lnast::attrs::loc);
  if (!ref.has()) {
    return {};
  }
  auto v = ref.get();
  return Loc{v.pos1, v.pos2, v.line, v.tok};
}

void Lnast::set_loc(const Lnast_nid& nid, const Loc& loc) {
  if (loc.pos1 == 0 && loc.pos2 == 0 && loc.line == 0 && loc.tok == 0) {
    auto ref = nid.attr(lnast::attrs::loc);
    if (ref.has()) {
      ref.del();
    }
    return;
  }
  lnast::attrs::loc_t::value_type v{loc.pos1, loc.pos2, loc.line, loc.tok};
  nid.attr(lnast::attrs::loc).set(v);
}

std::string_view Lnast::get_fname(const Lnast_nid& nid) const {
  auto ref = nid.attr(lnast::attrs::fname);
  if (!ref.has()) {
    return {};
  }
  return ref.get();
}

void Lnast::set_fname(const Lnast_nid& nid, std::string_view fname) {
  if (fname.empty()) {
    auto ref = nid.attr(lnast::attrs::fname);
    if (ref.has()) {
      ref.del();
    }
    return;
  }
  nid.attr(lnast::attrs::fname).set(std::string(fname));
}

void Lnast::set_data(const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int type) {
  set_type(nid, type);
  set_name(nid, {});
}

void Lnast::set_data(const Lnast_nid& nid, const Lnast_node& n) {
  set_type(nid, n.get_type());
  set_name(nid, n.get_name());
}

// ─────────────────────────────────────────────────────────────────────────
// print / dump / read
//
// hhds split: print() is pretty-only (box-drawing, not round-trippable);
// dump() goes through write_dump (parseable by read_dump). Lnast::read uses
// hhds Tree::read_dump and re-applies the lnast-side attributes (name,
// loc, fname) that ride in the dump as `'<name>' @(loc=..,fname=..)`.
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

// Encode/decode loc as "pos1:pos2:line:tok" so it survives a single
// comma-separated attribute slot in the dump.
std::string encode_loc(const Lnast::Loc& loc) {
  return std::format("{}:{}:{}:{}", loc.pos1, loc.pos2, loc.line, static_cast<uint32_t>(loc.tok));
}

bool decode_loc(std::string_view s, Lnast::Loc& out) {
  std::array<uint64_t, 4> v{0, 0, 0, 0};
  size_t                  field = 0;
  size_t                  pos   = 0;
  while (pos <= s.size() && field < 4) {
    size_t end = s.find(':', pos);
    if (end == std::string_view::npos) {
      end = s.size();
    }
    auto piece = s.substr(pos, end - pos);
    auto first = piece.data();
    auto last  = piece.data() + piece.size();
    auto [ptr, ec] = std::from_chars(first, last, v[field]);
    if (ec != std::errc{} || ptr != last) {
      return false;
    }
    ++field;
    if (end == s.size()) {
      break;
    }
    pos = end + 1;
  }
  if (field != 4) {
    return false;
  }
  out.pos1 = v[0];
  out.pos2 = v[1];
  out.line = static_cast<uint32_t>(v[2]);
  out.tok  = static_cast<uint8_t>(v[3]);
  return true;
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
    auto loc = get_loc(node);
    if (loc.pos1 == 0 && loc.pos2 == 0 && loc.line == 0) {
      return std::nullopt;
    }
    return std::format("{}-{}@{}", loc.pos1, loc.pos2, get_fname(node));
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
  opts.node_text = [this](const hhds::Tree::Node_class& node) -> std::string {
    auto n = get_name(node);
    if (n.empty()) {
      return std::string(Lnast_ntype::to_sv(get_type(node)));
    }
    return std::string(n);
  };
  opts.attributes.emplace_back("loc", [this](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
    auto loc = get_loc(node);
    if (loc.pos1 == 0 && loc.pos2 == 0 && loc.line == 0 && loc.tok == 0) {
      return std::nullopt;
    }
    return encode_loc(loc);
  });
  opts.attributes.emplace_back("fname", [this](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
    auto f = get_fname(node);
    if (f.empty()) {
      return std::nullopt;
    }
    return std::string(f);
  });
  tree_->write_dump(os, opts);
}

void Lnast::dump(const std::string& filename) const {
  std::ofstream ofs(filename);
  I(ofs.is_open(), "lnast.dump: cannot open {} for writing", filename);
  dump(ofs);
}

namespace {

// Build an Lnast from one already-decoded hhds read_dump segment. Factored
// out so single- and multi-tree readers share the post-processing.
std::shared_ptr<Lnast> finish_read(hhds::Tree::ReadDumpResult result) {
  auto top_name = std::string(result.tree->get_name());
  auto lnast    = std::make_shared<Lnast>(top_name, "");
  // The loaded tree is unattached (forest=null); replace_body wires it
  // through TreeIO so forest_/treeio_ owns it from here on.
  lnast->replace_body(result.tree);

  // Re-apply per-node lnast attributes (name, loc, fname). NodeData entries
  // are recorded in pre-order; iterating tree.pre_order() yields the same
  // order, so we match them index-by-index.
  size_t i = 0;
  for (auto node : lnast->tree().pre_order()) {
    if (i >= result.nodes.size()) {
      break;
    }
    const auto& nd = result.nodes[i++];

    // NodeData::node_text falls back to type_name when the dump line had no
    // quoted text. Suppress that fallback so an unset name stays unset.
    auto type     = lnast->get_type(node);
    auto type_str = Lnast_ntype::to_sv(type);
    if (!nd.node_text.empty() && nd.node_text != type_str) {
      lnast->set_name(node, nd.node_text);
    }

    for (const auto& [k, v] : nd.attributes) {
      if (k == "loc") {
        Lnast::Loc loc{};
        if (decode_loc(v, loc)) {
          lnast->set_loc(node, loc);
        }
      } else if (k == "fname") {
        lnast->set_fname(node, v);
      }
    }
  }

  return lnast;
}

// A "name line" in a multi-tree dump is the header that starts a new tree:
// it carries no tree-drawing prefix (every node line begins either with the
// 0xe2 byte of ├/└/│ or with the 4-space prefix that hhds emits under a
// last-child parent).
bool is_name_line(const std::string& line) {
  if (line.empty()) {
    return false;
  }
  unsigned char c = static_cast<unsigned char>(line.front());
  return c != 0xe2 && c != ' ';
}

}  // namespace

std::shared_ptr<Lnast> Lnast::read(std::istream& is) {
  auto trees = read_all(is);
  return trees.empty() ? nullptr : trees.front();
}

std::shared_ptr<Lnast> Lnast::read(const std::string& filename) {
  std::ifstream ifs(filename);
  I(ifs.is_open(), "lnast.read: cannot open {} for reading", filename);
  return read(ifs);
}

std::vector<std::shared_ptr<Lnast>> Lnast::read_all(std::istream& is) {
  std::vector<std::shared_ptr<Lnast>> out;

  // Split the stream into one-tree segments at every "name line" (header
  // with no tree-drawing prefix), then hand each segment to hhds::read_dump.
  std::string current;
  bool        have_segment = false;
  std::string line;
  while (std::getline(is, line)) {
    if (is_name_line(line) && have_segment) {
      std::istringstream segment(current);
      out.push_back(finish_read(hhds::Tree::read_dump(segment, lnast_type_table())));
      current.clear();
    }
    current.append(line);
    current.push_back('\n');
    have_segment = true;
  }
  if (have_segment) {
    std::istringstream segment(current);
    out.push_back(finish_read(hhds::Tree::read_dump(segment, lnast_type_table())));
  }
  return out;
}

std::vector<std::shared_ptr<Lnast>> Lnast::read_all(const std::string& filename) {
  std::ifstream ifs(filename);
  I(ifs.is_open(), "lnast.read: cannot open {} for reading", filename);
  return read_all(ifs);
}
