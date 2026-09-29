// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_ssa.hpp"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_ntype.hpp"

namespace {

using N = Lnast_ntype;

Lnast_nid add_scalar_port(const std::shared_ptr<Lnast>& ln, const Lnast_nid& tuple, std::string_view name) {
  auto store = ln->add_child(tuple, N::create_store());
  ln->add_child(store, Lnast_node::create_ref(name));
  ln->add_child(store, Lnast_node::create_const("nil"));
  auto type = ln->add_child(store, N::create_prim_type_int());
  ln->add_child(type, Lnast_node::create_const("255"));
  ln->add_child(type, Lnast_node::create_const("0"));
  return store;
}

struct Function_lnast {
  std::shared_ptr<Lnast> ln;
  Lnast_nid              stmts;
};

Function_lnast make_function() {
  auto ln   = std::make_shared<Lnast>("unit.f");
  auto root = ln->set_root(N::create_top());
  auto io   = ln->add_child(root, N::create_io());
  auto in   = ln->add_child(io, N::create_tuple_add());
  auto out  = ln->add_child(io, N::create_tuple_add());
  add_scalar_port(ln, in, "a");
  add_scalar_port(ln, out, "z");
  auto stmts = ln->add_child(root, N::create_stmts());
  return {std::move(ln), stmts};
}

Function_lnast make_tuple_input_function() {
  auto ln   = std::make_shared<Lnast>("unit.tuple_f");
  auto root = ln->set_root(N::create_top());
  auto io   = ln->add_child(root, N::create_io());
  auto in   = ln->add_child(io, N::create_tuple_add());
  auto out  = ln->add_child(io, N::create_tuple_add());

  auto ar = ln->add_child(in, N::create_store());
  ln->add_child(ar, Lnast_node::create_ref("ar"));
  ln->add_child(ar, Lnast_node::create_const("nil"));
  auto fields = ln->add_child(ar, N::create_tuple_add());
  add_scalar_port(ln, fields, "x");
  add_scalar_port(ln, out, "z");

  auto stmts = ln->add_child(root, N::create_stmts());
  return {std::move(ln), stmts};
}

void add_store(const Function_lnast& fn, std::string_view dst, std::string_view value) {
  auto st = fn.ln->add_child(fn.stmts, N::create_store());
  fn.ln->add_child(st, Lnast_node::create_ref(dst));
  fn.ln->add_child(st, Lnast_node::create_const(value));
}

TEST(UpassSsa, SingleDefinitionBodyKeepsSourceTree) {
  auto fn = make_function();
  add_store(fn, "z", "7");

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_EQ(fn.ln->tree_ptr(), before);
  ASSERT_EQ(fn.ln->io_meta().inputs.size(), 1);
  ASSERT_EQ(fn.ln->io_meta().outputs.size(), 1);
  EXPECT_EQ(fn.ln->io_meta().inputs.front().name, "a");
  EXPECT_EQ(fn.ln->io_meta().outputs.front().name, "z");
  EXPECT_EQ(fn.ln->io_meta().inputs.front().bits, 8);
}

TEST(UpassSsa, RepeatedDefinitionUsesVersioningRebuild) {
  auto fn = make_function();
  add_store(fn, "x", "1");
  add_store(fn, "x", "2");
  add_store(fn, "z", "3");

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_NE(fn.ln->tree_ptr(), before);
  bool saw_version = false;
  for (const auto& nid : fn.ln->depth_preorder(fn.ln->get_root())) {
    if (!nid.is_invalid() && N::is_ref(fn.ln->get_type(nid)) && fn.ln->get_name(nid) == "x___ssa_1") {
      saw_version = true;
      break;
    }
  }
  EXPECT_TRUE(saw_version);
}

TEST(UpassSsa, VerilogOriginRepeatedDefinitionDefersVersioningToRunner) {
  auto fn = make_function();
  fn.ln->set_verilog_origin(true);
  add_store(fn, "x", "1");
  add_store(fn, "x", "2");
  add_store(fn, "z", "3");

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_EQ(fn.ln->tree_ptr(), before);
  EXPECT_TRUE(fn.ln->needs_stream_ssa());
  EXPECT_TRUE(fn.ln->stream_ssa_names().contains("x"));
  EXPECT_FALSE(fn.ln->stream_ssa_names().contains("z"));
}

TEST(UpassSsa, VerilogOriginRepeatedOutputKeepsLegacyFinalPortCommit) {
  auto fn = make_function();
  fn.ln->set_verilog_origin(true);
  add_store(fn, "z", "0sb?");
  add_store(fn, "z", "7");

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_NE(fn.ln->tree_ptr(), before);
  EXPECT_FALSE(fn.ln->needs_stream_ssa());
}

TEST(UpassSsa, PreflattenedTuplePortBodyKeepsSourceTree) {
  auto fn = make_tuple_input_function();
  auto st = fn.ln->add_child(fn.stmts, N::create_store());
  fn.ln->add_child(st, Lnast_node::create_ref("z"));
  fn.ln->add_child(st, Lnast_node::create_ref("ar.x"));

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_EQ(fn.ln->tree_ptr(), before);
  ASSERT_EQ(fn.ln->io_meta().inputs.size(), 1);
  EXPECT_EQ(fn.ln->io_meta().inputs.front().name, "ar.x");
}

TEST(UpassSsa, StaticTuplePortFieldUseStreamsWithoutRebuild) {
  auto fn    = make_tuple_input_function();
  auto alias = fn.ln->add_child(fn.stmts, N::create_store());
  fn.ln->add_child(alias, Lnast_node::create_ref("%port"));
  fn.ln->add_child(alias, Lnast_node::create_ref("ar"));
  auto get = fn.ln->add_child(fn.stmts, N::create_tuple_get());
  fn.ln->add_child(get, Lnast_node::create_ref("%field"));
  fn.ln->add_child(get, Lnast_node::create_ref("%port"));
  fn.ln->add_child(get, Lnast_node::create_const("x"));
  auto st = fn.ln->add_child(fn.stmts, N::create_store());
  fn.ln->add_child(st, Lnast_node::create_ref("z"));
  fn.ln->add_child(st, Lnast_node::create_ref("%field"));

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_EQ(fn.ln->tree_ptr(), before);
}

TEST(UpassSsa, WholeTuplePortUseStillRebuilds) {
  auto fn = make_tuple_input_function();
  auto st = fn.ln->add_child(fn.stmts, N::create_store());
  fn.ln->add_child(st, Lnast_node::create_ref("z"));
  fn.ln->add_child(st, Lnast_node::create_ref("ar"));

  const auto before = fn.ln->tree_ptr();
  uPass_ssa::run(fn.ln);

  EXPECT_NE(fn.ln->tree_ptr(), before);
}

}  // namespace
