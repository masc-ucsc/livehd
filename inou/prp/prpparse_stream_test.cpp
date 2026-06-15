//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// 2f-stream: the streaming parser seam (Parser::parse_next) must hold only ONE
// top-level construct in the CST arena at a time, so resident parse-tree memory
// stays bounded regardless of file size. This is the win the prpparse front-end
// swap deferred; prp2lnast drives the LiveHD path through parse_next.

#include <algorithm>
#include <string>

#include "gtest/gtest.h"
#include "prpparse/parser.hpp"
#include "prpparse/source_buffer.hpp"

namespace {

// Build a large concatenated input: N independent top-level statements.
std::string make_input(int n) {
  std::string src;
  src.reserve(static_cast<size_t>(n) * 16);
  for (int i = 0; i < n; ++i) {
    src += "v" + std::to_string(i) + " = " + std::to_string(i) + " + 1\n";
  }
  return src;
}

}  // namespace

TEST(prpparse_stream, arena_bounded_to_one_construct) {
  constexpr int     n   = 5000;
  const std::string src = make_input(n);

  // Streaming: parse_next() recycles the arena before each construct, so the
  // live node count tracks ONE statement, never the whole file.
  prpparse::Source_buffer buf("mem_stream.prp", src);
  prpparse::Parser        p(buf);
  size_t                  max_stream = 0;
  int                     count      = 0;
  while (prpparse::Ast* a = p.parse_next()) {
    (void)a;  // the consumer would lower `a` here, before the next parse_next()
    max_stream = std::max(max_stream, p.arena_node_count());
    ++count;
  }
  EXPECT_EQ(count, n);
  // One `vK = K + 1` construct is a small, FIXED number of nodes — independent
  // of n. (Generous bound; a single such statement is ~10 nodes.)
  EXPECT_GT(max_stream, 0u);
  EXPECT_LT(max_stream, 64u);
}

TEST(prpparse_stream, whole_tree_grows_unbounded) {
  constexpr int     n   = 5000;
  const std::string src = make_input(n);

  // The whole-file builder keeps every node alive at once — O(n). This is the
  // baseline the streaming path avoids; assert streaming is dramatically smaller.
  prpparse::Source_buffer buf_s("cmp_stream.prp", src);
  prpparse::Parser        p_s(buf_s);
  size_t                  max_stream = 0;
  while (prpparse::Ast* a = p_s.parse_next()) {
    (void)a;
    max_stream = std::max(max_stream, p_s.arena_node_count());
  }

  prpparse::Source_buffer buf_w("cmp_whole.prp", src);
  prpparse::Parser        p_w(buf_w);
  p_w.parse_ast();
  const size_t whole = p_w.arena_node_count();

  EXPECT_GT(whole, static_cast<size_t>(n));            // at least one node per statement
  EXPECT_GT(whole, max_stream * 100);                  // O(n) whole-tree vs O(1) streamed
}
