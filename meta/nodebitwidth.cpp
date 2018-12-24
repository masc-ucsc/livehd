//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "nodebitwidth.hpp"
#include "lgraph.hpp"

void Node_bitwidth::Explicit_range::dump() const {
  fmt::print("max{}:{} min{}:{} sign{}:{} {}"
      ,max_set?"_set":"",max
      ,min_set?"_set":"",min
      ,sign_set?"_set":"",sign
      ,overflow?"overflow":"");
}

bool Node_bitwidth::Explicit_range::is_unsigned() const {
  return !sign_set || (sign_set && !sign);
}

void Node_bitwidth::Explicit_range::set_sbits(uint16_t size) {
  sign_set = true;
  sign     = true;

  if (size==0) {
    overflow = false;
    max_set  = false;
    min_set  = false;
    return;
  }
  assert(size);

  max_set = true;
  min_set = true;

  if (size>63) {
    overflow = true;
    max = size-1;  // Use bits in overflow mode
    min = -(size-1); // Use bits
  }else{
    overflow = false;
    max = pow(2,size-1) - 1;
    min = -pow(2,size-1);
  }
}

void Node_bitwidth::Explicit_range::set_ubits(uint16_t size) {
  sign_set = false;
  sign     = false;

  if (size==0) {
    overflow = false;
    max_set  = false;
    min_set  = false;
    return;
  }
  assert(size);

  max_set = true;
  min_set = true;
  min     = 0;

  if (size>63) {
    overflow = true;
    max = size; // Use bits in overflow mode
  }else{
    overflow = false;
    max = pow(2,size) - 1;
  }
}
void Node_bitwidth::Explicit_range::set_uconst(uint32_t val) {
  sign_set = true;
  sign     = false;

  max_set = true;
  min_set = true;
  max     = val;
  min     = val;
}

void Node_bitwidth::Explicit_range::set_sconst(uint32_t val) {
  sign_set = true;
  sign     = true;

  max_set = true;
  min_set = true;
  max     = static_cast<int32_t>(val); //calculate 2's complement, ex. B = 1011 = -5
  min     = static_cast<int32_t>(val);
}


void Node_bitwidth::Implicit_range::dump() const {
  fmt::print("max:{} min:{} sign:{} {}",max,min,sign,overflow?"overflow":"");
}

int64_t Node_bitwidth::Implicit_range::round_power2(int64_t x) const {
  uint64_t ux = abs(x);

  if (x == 0) {
    return 0;
  }

  // This finds the number of bits in "ux" minus the number of leading 0s.
  uint64_t ux_r = 1ULL<<(sizeof(uint64_t) * 8 - __builtin_clzll(ux));

  if (ux==static_cast<uint64_t>(x)) {
    return ux_r-1;//I need to subtract one since 4 bits gives us 0 to 15, not 0 to 2^4.
  }

  //FIXME: Above I subtract one. Do I need to do this for negatives? I'd assume not.
  return -ux_r;
}

bool Node_bitwidth::Implicit_range::expand(const Implicit_range &i, bool round2) {

  bool updated = false;

  if (sign & !i.sign)
    updated = true;

  sign = sign & i.sign;

  if (!i.overflow && overflow) {
    // Nothing to do (!overflow is always smaller than overflow)
  }else if (i.overflow && !overflow) {
    overflow = true;
    updated = true;
    max = i.max;
    min = i.min;
  }else{
    auto tmp = std::max(max,i.max);
    if (round2 && !overflow) {
      tmp = round_power2(tmp);
      if (sign)
        tmp--;
    }
    updated |= (tmp!=max);
    max = tmp;

    if (sign) {
      tmp = -max-1;
    }else{
      tmp = 0;
    }
    if (round2 && !overflow)
      tmp = round_power2(tmp);
    updated |= (tmp!=min);
    min = tmp;
  }

  return updated;
}

void Node_bitwidth::Implicit_range::pick(const Explicit_range &e) {

  if (!e.overflow && !overflow) {
    if (e.sign && sign) {
    }else if (e.sign && !sign) {
      max = max/2;
      min = -min/2;
    }else if (!e.sign && sign) {
      //max = max;
      min = 0;
    }

    if (max>e.max)
      max = e.max;

    if (min<e.min)
      min = e.min;
  }else if (e.overflow && overflow) {
    if (e.sign && sign) {
    }else if (e.sign && !sign) {
      max = max-1;
      min = min+1;
    }else if (!e.sign && sign) {
      //max = max;
      min = 0;
    }

    if (max>e.max)
      max = e.max;

    if (min<e.min)
      min = e.min;
  }else if (e.overflow && !overflow) {
  }else if (!e.overflow && overflow) {
    overflow = e.overflow;
    max = e.max;
    min = e.min;
  }
}

LGraph_Node_bitwidth::LGraph_Node_bitwidth(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid)
    , LGraph_Base(path, name, lgid)
    , node_bitwidth(path + "/lgraph_" + name + "_bitwidth") {
}

void LGraph_Node_bitwidth::clear() {
  node_bitwidth.clear();
}

void LGraph_Node_bitwidth::reload() {
  uint64_t sz = library->get_nentries(lgraph_id);
  node_bitwidth.reload(sz);
}

void LGraph_Node_bitwidth::sync() {
  node_bitwidth.sync();
}

void LGraph_Node_bitwidth::emplace_back() {
  node_bitwidth.emplace_back();
  //node_bitwidth[node_bitwidth.size() - 1] = Node_bitwidth();
}

void LGraph_Node_bitwidth::node_bitwidth_set(Index_ID nid, const Node_bitwidth &t) {
  assert(nid < node_bitwidth.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());

  node_bitwidth[nid] = t;
}

Node_bitwidth &LGraph_Node_bitwidth::node_bitwidth_get(Index_ID nid) const {
  assert(nid < node_bitwidth.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());

  return node_bitwidth[nid];
}

void LGraph_Node_bitwidth::node_bitwidth_emplace_back() {
  node_bitwidth.emplace_back();
}

