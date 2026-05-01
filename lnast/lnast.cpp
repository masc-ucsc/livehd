//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast.hpp"

#include <format>
#include <iostream>
#include <string>

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

void Lnast::dump(const Lnast_nid& root_nid) const {
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
  tree_->print(std::cout, root_nid, opts);
}
