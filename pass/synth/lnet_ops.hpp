// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The blaster's bit algebra (arith.hpp's Ops) over a RAW Lnet: 2-input gates
// and explicit inverters, folding only constants and `x op x`, with lazily
// created constants. Every node is recorded where it is created, so a
// backend replays the network object for object (abc_cleanup.md section 4).
//
// Two constant styles reproduce the two builders this replaced: `eager` (the
// region blaster: every binary gate materializes constant 0 then constant 1
// first, an inverter asks for constant 1 before constant 0) and `lazy`
// (satopt's proof cones: a constant exists only once a test reads it). They
// fold alike; they differ only in WHEN a constant node appears, which a
// replay must keep.
#include <cstdint>
#include <stdexcept>

#include "lnet.hpp"

namespace livehd::synth {

class Lnet_ops {
public:
  using Bit = Lid;
  enum class Constants : uint8_t { eager, lazy };
  // Thrown before a node past `max_nodes` (0: no limit) would be created.
  struct Too_large : std::runtime_error {
    Too_large() : std::runtime_error("lnet node limit") {}
  };

  explicit Lnet_ops(Lnet& net, Constants style = Constants::eager, uint64_t max_nodes = 0)
      : net_(net), style_(style), max_nodes_(max_nodes) {}

  [[nodiscard]] Lnet& net() { return net_; }

  Bit zero() {
    if (const0_ == Lnet::kNone) {
      admit();
      const0_ = net_.add_constant(false);
    }
    return const0_;
  }
  Bit one() {
    if (const1_ == Lnet::kNone) {
      admit();
      const1_ = net_.add_constant(true);
    }
    return const1_;
  }
  Bit konst(bool v) { return v ? one() : zero(); }

  Bit inv(Bit a) {
    if (style_ == Constants::eager) {
      if (a == one()) {
        return zero();
      }
      if (a == zero()) {
        return one();
      }
    } else {
      if (a == zero()) {
        return one();
      }
      if (a == one()) {
        return zero();
      }
    }
    return gate({a}, Lnet::kNot);
  }
  Bit and_(Bit a, Bit b) {
    if (style_ == Constants::eager) {
      materialize();
    }
    if (a == zero() || b == zero()) {
      return zero();
    }
    if (a == one()) {
      return b;
    }
    if (b == one() || a == b) {
      return a;
    }
    return gate({a, b}, Lnet::kAnd2);
  }
  Bit or_(Bit a, Bit b) {
    if (style_ == Constants::eager) {
      materialize();
    }
    if (a == one() || b == one()) {
      return one();
    }
    if (a == zero()) {
      return b;
    }
    if (b == zero() || a == b) {
      return a;
    }
    return gate({a, b}, Lnet::kOr2);
  }
  Bit xor_(Bit a, Bit b) {
    if (style_ == Constants::eager) {
      materialize();
      if (a == zero()) {
        return b;
      }
      if (b == zero()) {
        return a;
      }
      if (a == b) {
        return zero();
      }
    } else {
      if (a == b) {
        return zero();
      }
      if (a == zero()) {
        return b;
      }
      if (b == zero()) {
        return a;
      }
    }
    return gate({a, b}, Lnet::kXor2);
  }
  // 2:1 mux: sel ? t : f == (sel & t) | (~sel & f).
  Bit mux(Bit sel, Bit t, Bit f) { return or_(and_(sel, t), and_(inv(sel), f)); }

  // A primary input, counted against the node limit.
  Bit input(std::string name) {
    admit();
    return net_.add_input(std::move(name));
  }

private:
  Lnet&     net_;
  Constants style_;
  uint64_t  max_nodes_;
  Lid       const0_ = Lnet::kNone;
  Lid       const1_ = Lnet::kNone;

  void materialize() {
    (void)zero();
    (void)one();
  }
  void admit() const {
    if (max_nodes_ != 0 && net_.size() - 1 >= max_nodes_) {
      throw Too_large{};
    }
  }
  Bit gate(std::initializer_list<Lid> fanins, uint64_t fn) {
    admit();
    return net_.add_lut(fanins, fn);
  }
};

}  // namespace livehd::synth
