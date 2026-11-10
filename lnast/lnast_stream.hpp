//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Minimal LNAST-to-LNAST streaming passes.
//
// A producer (lg2lnast today, any front end tomorrow) announces one function with begin() -- the
// tree holding the module name, its attributes and the `io` subtree -- and then hands over its
// top-level statements one at a time with stmt(). A pass sees a statement as soon as it exists and
// forwards zero, one or many statements downstream, so passes chain without a whole-tree barrier
// and may keep a small lookahead window (flush it in end()).
//
// Deliberately NOT upass: no symbol table, no operand bundles, no fixed-point. A pass keeps only
// the facts it needs (typically a name -> value map of its own). Because every stage speaks plain
// LNAST, any stage can be dropped and the result is still ordinary compiler input.
//
// The statement handed to stmt() is (tree, nid) owned by the caller and valid only for the call;
// copy what you keep. To emit new statements a pass builds them in a scratch tree of its own.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lnast.hpp"

namespace livehd::lnast_stream {

class Sink {
public:
  virtual ~Sink() = default;
  // `header`: root `top`, its `io` child complete, and an (initially empty) `stmts` child.
  virtual void begin(const std::shared_ptr<Lnast>& header) = 0;
  virtual void stmt(const std::shared_ptr<Lnast>& tree, const Lnast_nid& stmt) = 0;
  virtual void end() = 0;
};

// A stage: forwards everything by default; override what you rewrite.
class Pass : public Sink {
public:
  void set_next(Sink* next) { next_ = next; }

  void begin(const std::shared_ptr<Lnast>& header) override { next().begin(header); }
  void stmt(const std::shared_ptr<Lnast>& tree, const Lnast_nid& s) override { next().stmt(tree, s); }
  void end() override { next().end(); }

protected:
  Sink& next() { return *next_; }

private:
  Sink* next_ = nullptr;
};

// Final stage: materializes the stream as an ordinary tree.
class Tree_sink : public Sink {
public:
  void begin(const std::shared_ptr<Lnast>& header) override;
  void stmt(const std::shared_ptr<Lnast>& tree, const Lnast_nid& s) override;
  void end() override {}

  [[nodiscard]] std::shared_ptr<Lnast> result() const { return out_; }

private:
  std::shared_ptr<Lnast> out_;
  Lnast_nid              stmts_;
};

// Deep copy of one subtree (names and types; source ids are not carried across trees).
void copy_subtree(const std::shared_ptr<Lnast>& src, const Lnast_nid& src_nid, const std::shared_ptr<Lnast>& dst,
                  const Lnast_nid& dst_parent);

// An ordered list of passes wired head -> ... -> final sink.
class Chain {
public:
  void add(std::unique_ptr<Pass> pass) { passes_.push_back(std::move(pass)); }
  [[nodiscard]] bool empty() const { return passes_.empty(); }
  // Wire the stages in front of `final_sink`; returns the head to feed.
  Sink& link(Sink& final_sink);

private:
  std::vector<std::unique_ptr<Pass>> passes_;
};

// Registered passes, by name: "balance" (n-ary and/or/xor -> balanced binary tree).
// Returns nullptr for an unknown name.
std::unique_ptr<Pass> make_pass(std::string_view name);
// Comma separated names -> chain; `error` names the first unknown pass.
bool parse_chain(std::string_view names, Chain& chain, std::string& error);

}  // namespace livehd::lnast_stream
