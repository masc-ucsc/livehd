//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast.hpp"

#include <format>
#include <iostream>
#include <string>

#include "elab_scanner.hpp"

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

void Lnast_node::dump() const { std::print("{}, {}\n", type.debug_name(), token.get_text()); }

Lnast::Lnast(std::string_view _module_name, std::string_view _file_name)
    : forest_(hhds::Forest::create()), top_module_name(_module_name), source_filename(_file_name) {
  // Each Lnast owns one tree body in its forest. The TreeIO carries the
  // module name; the Tree body is created lazily here so that get_root()
  // works only after set_root() has stamped the top node.
  auto tio = forest_->create_io(top_module_name);
  tree_    = tio->create_tree();
}

Lnast::~Lnast() = default;

// ─────────────────────────────────────────────────────────────────────────
// Mutation
// ─────────────────────────────────────────────────────────────────────────

Lnast_nid Lnast::set_root(const Lnast_node& n) {
  auto root = tree_->add_root_node();
  set_data(root, n);
  return root;
}

Lnast_nid Lnast::add_child(const Lnast_nid& parent, const Lnast_node& n) {
  auto child = parent.add_child();
  set_data(child, n);
  return child;
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

Lnast_ntype Lnast::get_type(const Lnast_nid& nid) const {
  return Lnast_ntype(static_cast<Lnast_ntype::Lnast_ntype_int>(nid.get_type()));
}

void Lnast::set_type(const Lnast_nid& nid, Lnast_ntype t) {
  nid.set_type(static_cast<hhds::Type>(t.get_raw_ntype()));
}

State_token Lnast::get_token(const Lnast_nid& nid) const {
  std::string text;
  auto        name_ref = nid.attr(hhds::attrs::name);
  if (name_ref.has()) {
    text = std::string(name_ref.get());
  }

  std::string fname_str;
  auto        fname_ref = nid.attr(lnast::attrs::fname);
  if (fname_ref.has()) {
    fname_str = std::string(fname_ref.get());
  }

  lnast::attrs::loc_t::value_type loc{};
  auto                            loc_ref = nid.attr(lnast::attrs::loc);
  if (loc_ref.has()) {
    loc = loc_ref.get();
  }

  return State_token(static_cast<Token_id>(loc.tok), loc.pos1, loc.pos2, loc.line, text, fname_str);
}

void Lnast::set_token(const Lnast_nid& nid, const State_token& tok) {
  nid.attr(hhds::attrs::name).set(std::string(tok.get_text()));
  if (!tok.fname.empty()) {
    nid.attr(lnast::attrs::fname).set(tok.fname);
  } else {
    auto fname_ref = nid.attr(lnast::attrs::fname);
    if (fname_ref.has()) {
      fname_ref.del();
    }
  }
  lnast::attrs::loc_t::value_type loc{tok.pos1, tok.pos2, tok.line, static_cast<uint8_t>(tok.tok)};
  nid.attr(lnast::attrs::loc).set(loc);
}

std::string_view Lnast::get_name(const Lnast_nid& nid) const {
  auto ref = nid.attr(hhds::attrs::name);
  if (!ref.has()) {
    return {};
  }
  return ref.get();
}

Lnast_node Lnast::get_data(const Lnast_nid& nid) const {
  if (get_type(nid).is_invalid()) {
    return Lnast_node();  // default invalid
  }
  return Lnast_node(get_type(nid), get_token(nid));
}

void Lnast::set_data(const Lnast_nid& nid, const Lnast_node& n) {
  set_type(nid, n.type);
  set_token(nid, n.token);
}

void Lnast::dump(const Lnast_nid& root_nid) const {
  hhds::Tree::PrintOptions opts;
  opts.show_types = false;
  opts.node_text  = [this](const hhds::Tree::Node_class& node) {
    const auto t = get_type(node);
    const auto n = get_name(node);
    return std::format("{}: {}", t.to_sv(), n);
  };
  opts.attributes.emplace_back("loc", [this](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
    auto tok = get_token(node);
    if (tok.pos1 == 0 && tok.pos2 == 0 && tok.line == 0) {
      return std::nullopt;
    }
    return std::format("{}-{}@{}", tok.pos1, tok.pos2, tok.fname);
  });
  tree_->print(std::cout, root_nid, opts);
}
