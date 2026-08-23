#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "hlop/dlop.hpp"

template <typename Pin>
class Pin_tracker {
public:
  struct Pin_pos {
    Pin id;
    int pos = -1;
  };

  using Pin_vector = std::vector<Pin_pos>;

  Pin_tracker(Pin zero_pin) : Zero_pin(zero_pin) {}

  void add_input(Pin wname, int32_t bits) {
    auto& pv = full_map[wname];
    I(pv.empty());  // Why to double insert inputs??

    pv.resize(bits, {Zero_pin, -1});
    for (auto i = 0; i < bits; ++i) {
      pv[i].id  = wname;
      pv[i].pos = i;
    }
  }

  void add_get_mask(Pin dst_pin, Pin a_pin, int32_t a_sbits, Dlop mask) {
    auto& pv = full_map[dst_pin];
    pv.clear();

    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }

    auto pairs = mask.get_mask_range_pairs();
    for (const auto& p : pairs) {
      auto start = static_cast<size_t>(p.first);
      auto end   = static_cast<size_t>(p.first + p.second);  // [start,end)
      auto pos   = start;
      if (end > it->second.size()) {
        end = it->second.size();
      }

      while (pos < end) {
        pv.emplace_back(it->second[pos]);
        ++pos;
      }
    }
  }

  void add_set_mask(Pin dst_pin, Pin a_pin, int32_t a_sbits, Dlop mask, Pin v_pin) {
    Pin_vector pv   = get_or_create_pv(a_pin, a_sbits);
    Pin_vector v_pv = get_or_create_pv(v_pin, mask.get_bits());

    if (pv.size() < v_pv.size()) {
      pv.resize(v_pv.size(), {Zero_pin, -1});
    }

    size_t pick_v_pos = 0;
    auto   pairs      = mask.get_mask_range_pairs();
    for (const auto& p : pairs) {
      auto start = static_cast<size_t>(p.first);
      auto end   = static_cast<size_t>(p.first + p.second);  // [start,end)
      auto pos   = start;

      if (pv.size() <= end) {
        pv.resize(end, {Zero_pin, -1});
      }

      while (pos < end) {
        if (v_pv.size() <= pick_v_pos) {
          pv[pos] = v_pv.back();
        } else {
          pv[pos] = v_pv[pick_v_pos];
        }
        ++pos;
        ++pick_v_pos;
      }
    }

    clean_excess_pv(pv);
    full_map.insert_or_assign(dst_pin, pv);
  }

  void add_shl(Pin dst_pin, Pin a_pin, int32_t a_sbits, Dlop amount) {
    I(amount.is_just_i64());  // a >62-bit shift amount is structurally bogus
    const auto amount_i = amount.to_just_i64();
    I(amount_i >= 0);

    const auto amount_u  = static_cast<size_t>(amount_i);
    const auto a_sbits_u = static_cast<size_t>(a_sbits);

    auto& pv = full_map[dst_pin];
    pv.resize(a_sbits_u + amount_u, {Zero_pin, -1});

    for (size_t i = 0; i < amount_u; ++i) {
      pv[i].id  = Zero_pin;
      pv[i].pos = 0;
    }

    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }

    for (size_t i = 0; i < a_sbits_u; ++i) {
      if (i >= it->second.size()) {
        pv[amount_u + i].id  = it->second.back().id;
        pv[amount_u + i].pos = it->second.back().pos;
      } else {
        pv[amount_u + i].id  = it->second[i].id;
        pv[amount_u + i].pos = it->second[i].pos;
      }
    }
  }

  void add_sra(Pin dst_pin, Pin a_pin, int32_t a_sbits, Dlop amount) {
    I(a_sbits > 0);
    I(amount.is_just_i64());  // a >62-bit shift amount is structurally bogus
    const auto amount_i = amount.to_just_i64();
    I(amount_i >= 0);

    const auto amount_u  = static_cast<size_t>(amount_i);
    const auto a_sbits_u = static_cast<size_t>(a_sbits);

    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }
    const Pin_vector source = it->second;

    auto&      pv       = full_map[dst_pin];
    const auto out_bits = amount_u < a_sbits_u ? a_sbits_u - amount_u : size_t{1};
    pv.resize(out_bits, {Zero_pin, -1});

    for (size_t i = 0; i < out_bits; ++i) {
      // Arithmetic right shift is wiring: output bit i comes from input bit
      // i+amount.  An overshift repeats the sign bit.  Keeping one bit in that
      // case is important for the one-bit SRA nodes emitted by ABC read-back;
      // unsigned/logical overshifts are represented by zero-fill wiring, not
      // by an SRA node.
      const auto src = std::min(amount_u + i, source.size() - 1);
      pv[i]          = source[src];
    }
  }

  void add_sext(Pin dst_pin, Pin a_pin, int32_t a_sbits, Dlop amount) {
    I(a_sbits > 0);
    I(amount.is_just_i64());  // a >62-bit shift amount is structurally bogus
    const auto amount_i = amount.to_just_i64();
    I(amount_i >= 0);

    const auto amount_u = static_cast<size_t>(amount_i);

    auto& pv = full_map[dst_pin];
    pv.resize(amount_u, {Zero_pin, -1});

    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      add_input(a_pin, a_sbits);
      it = full_map.find(a_pin);
    }

    for (size_t i = 0; i < amount_u; ++i) {
      if (i >= it->second.size()) {
        pv[i].id  = it->second.back().id;
        pv[i].pos = it->second.back().pos;
      } else {
        pv[i].id  = it->second[i].id;
        pv[i].pos = it->second[i].pos;
      }
    }
  }

  // One lane of an add_concat: `pin` drives the window [offset, offset+width)
  // of the result. `sbits` is the driver's own stamped width, used only to
  // synthesize a fresh pin vector when the lane has not been tracked yet.
  struct Concat_src {
    Pin     pin;
    int32_t sbits  = 0;
    int32_t width  = 0;  // DECLARED window width
    int32_t offset = 0;  // LSB position of the window in the result
  };

  // Concat is pure WIRING: result bit (offset_i + k) IS lane i's bit k. Every
  // bit keeps its (pin, pos) identity, so the cell contributes NO delay -- it
  // mints no gate, it renames bit positions (the same reasoning that makes
  // graph_util::ge_weight charge a Concat zero gates).
  //
  // `out_bits` is the CELL CONTRACT width, sum(lane widths) + 1: bits at and
  // above sum(w) are the always-zero sign slot of a never-negative result, and
  // are left as known zero. It is never the driver pin's stamp -- the const
  // sinks carry the intended bit spacing, and a narrowed stamp must not move a
  // lane.
  void add_concat(Pin dst_pin, const std::vector<Concat_src>& lanes, int32_t out_bits) {
    I(out_bits >= 0);

    // Build into a LOCAL vector: a lane may read dst_pin's own previous entry,
    // and full_map[dst_pin] would rehash the map out from under get_or_create_pv.
    Pin_vector pv;
    pv.resize(static_cast<size_t>(out_bits), {Zero_pin, 0});

    for (const auto& l : lanes) {
      const Pin_vector src = get_or_create_pv(l.pin, l.sbits);
      if (src.empty()) {
        continue;  // untracked and unsized: leave the window known-zero
      }
      for (int32_t k = 0; k < l.width; ++k) {
        const auto dst = static_cast<size_t>(l.offset) + static_cast<size_t>(k);
        if (dst >= pv.size()) {
          break;  // window above the contract width; cannot happen for a well-formed cell
        }
        // A lane driver NARROWER than its window SIGN-EXTENDS into it: the top
        // source bit replicates, exactly as add_shl/add_sra/add_sext do. A
        // driver WIDER than its window cannot reach here (the caller rejects it
        // via graph_util::concat_lane_violation).
        const auto si = static_cast<size_t>(k) < src.size() ? static_cast<size_t>(k) : src.size() - 1;
        pv[dst]       = src[si];
      }
    }

    full_map.insert_or_assign(dst_pin, pv);
  }

  void add_or(Pin dst_pin, Pin a_pin) {
    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      return;
    }
    auto& pv = full_map[dst_pin];
    it       = full_map.find(a_pin);  // WARNING: insert could destroy iterator

    if (pv.size() < it->second.size()) {
      pv.resize(it->second.size(), {Zero_pin, 0});
    }

    for (size_t i = 0; i < it->second.size(); ++i) {
      if (it->second[i].id == Zero_pin && it->second[i].pos == 0) {
        continue;  // Nothing to do in this bit
      }
      if (pv[i].id == Zero_pin && pv[i].pos == 0) {
        pv[i] = it->second[i];
      } else {
        pv[i].pos = -1;
      }
    }
  }
  void add_and(Pin dst_pin, Pin a_pin, Dlop a_mask) {
    auto it = full_map.find(a_pin);
    if (it == full_map.end()) {
      return;
    }
    auto& pv = full_map[dst_pin];
    it       = full_map.find(a_pin);  // WARNING: insert could destroy iterator

    auto max_bits_i = a_mask.get_mask_range().second;
    I(max_bits_i >= 0);
    auto max_bits = static_cast<size_t>(max_bits_i);
    if (it->second.size() < max_bits) {
      max_bits = it->second.size();
    }

    if (pv.size() < max_bits) {
      pv.resize(max_bits, {Zero_pin, 0});
    }

    for (size_t i = 0; i < max_bits; ++i) {
      if (it->second[i].id == Zero_pin && it->second[i].pos == 0) {
        continue;  // Nothing to do in this bit
      }
      if (!a_mask.bit_test(static_cast<int32_t>(i))) {
        continue;
      }
      if (pv[i].id == Zero_pin && pv[i].pos == 0) {
        pv[i] = it->second[i];
      } else {
        pv[i].pos = -1;
      }
    }
  }

  const Pin_vector& get_pin_vector(Pin dst_pin) const {
    const auto it = full_map.find(dst_pin);
    if (it == full_map.end()) {
      static Pin_vector empty;
      return empty;
    }
    return it->second;
  }

  /* LCOV_EXCL_START */
  void dump() {
    for (const auto e : full_map) {
      std::print("name:{}\n", e.first);
      for (const auto& s : e.second) {
        std::print(" id:{} pos:{}\n", s.id, s.pos);
      }
    }
  }
  /* LCOV_EXCL_STOP */

protected:
  Pin_vector get_or_create_pv(Pin a_name, int32_t a_sbits) const {
    auto it = full_map.find(a_name);
    if (it != full_map.end()) {
      return it->second;
    }

    Pin_vector a_pv;

    a_pv.resize(a_sbits, {Zero_pin, -1});
    int32_t pos = 0;
    for (auto& e : a_pv) {
      e.id  = a_name;
      e.pos = pos;
      ++pos;
    }

    return a_pv;
  }

  static void clean_excess_pv(Pin_vector& pv) {
    auto   rit  = pv.rbegin();
    size_t ndel = 0;
    while (rit != pv.rend()) {
      if (rit->pos >= 0) {
        break;
      }

      ++ndel;
      ++rit;
    }
    if (ndel) {
      pv.resize(pv.size() - ndel);
    }
  }

  Pin                                  Zero_pin;
  // WARNING: Pin_vector MUST have pointer stability on resize of full_map
  absl::node_hash_map<Pin, Pin_vector> full_map;

private:
};
