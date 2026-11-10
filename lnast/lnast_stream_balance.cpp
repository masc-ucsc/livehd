//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <format>
#include <vector>

#include "lnast_stream.hpp"

namespace livehd::lnast_stream {
namespace {

// `bit_and|bit_or|bit_xor(dst, a, b, c, ...)` with more than two operands becomes pairwise
// temporaries and one final binary statement: a log-depth tree instead of an N-deep chain for
// every consumer (the LLVM and slop emitters fold n-ary operands left to right). The operator is
// associative and bitwise, so the partial results need no declared range: they are as wide as the
// widest operand underneath, and `dst` keeps the type_spec the producer gave it.
class Balance : public Pass {
public:
  void begin(const std::shared_ptr<Lnast>& header) override {
    count_ = 0;
    next().begin(header);
  }

  void stmt(const std::shared_ptr<Lnast>& tree, const Lnast_nid& s) override {
    const auto type = tree->get_type(s);
    if (!Lnast_ntype::is_bit_and(type) && !Lnast_ntype::is_bit_or(type) && !Lnast_ntype::is_bit_xor(type)) {
      next().stmt(tree, s);
      return;
    }
    std::vector<Lnast_nid> kids;
    for (auto c : tree->children(s)) {
      kids.push_back(c);
    }
    if (kids.size() < 4) {  // dst + at most two operands
      next().stmt(tree, s);
      return;
    }
    if (!scratch_) {
      scratch_ = std::make_shared<Lnast>("lnast_stream_balance");
      root_    = scratch_->set_root(Lnast_ntype::create_stmts());
    }

    // Operands as (tree, nid) leaves from the source, then temporaries by name.
    struct Leaf {
      bool       is_tmp = false;
      std::string name;
      Lnast_nid  nid;
    };
    std::vector<Leaf> level;
    for (size_t i = 1; i < kids.size(); ++i) {
      level.push_back({false, {}, kids[i]});
    }
    const auto put = [&](const Lnast_nid& parent, const Leaf& l) {
      if (l.is_tmp) {
        scratch_->add_child(parent, Lnast_node::create_ref(l.name));
      } else {
        copy_subtree(tree, l.nid, scratch_, parent);
      }
    };
    const auto emit = [&](const Lnast_nid& dst_src, const std::string& dst_name, const Leaf& a, const Leaf& b) {
      const auto n = scratch_->add_child(root_, type);
      if (dst_name.empty()) {
        copy_subtree(tree, dst_src, scratch_, n);
      } else {
        scratch_->add_child(n, Lnast_node::create_ref(dst_name));
      }
      put(n, a);
      put(n, b);
      next().stmt(scratch_, n);
    };
    while (level.size() > 2) {
      std::vector<Leaf> upper;
      for (size_t i = 0; i + 1 < level.size(); i += 2) {
        const auto name = std::format("%bal{}", count_++);
        emit({}, name, level[i], level[i + 1]);
        upper.push_back({true, name, {}});
      }
      if (level.size() & 1) {
        upper.push_back(level.back());
      }
      level.swap(upper);
    }
    emit(kids[0], {}, level[0], level[1]);
  }

private:
  std::shared_ptr<Lnast> scratch_;
  Lnast_nid              root_;
  size_t                 count_ = 0;
};

}  // namespace

std::unique_ptr<Pass> make_pass(std::string_view name) {
  if (name == "balance") {
    return std::make_unique<Balance>();
  }
  return nullptr;
}

}  // namespace livehd::lnast_stream
