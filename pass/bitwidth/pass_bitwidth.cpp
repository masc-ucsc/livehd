//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgbench.hpp"
#include "lgraph.hpp"

#include "pass_bitwidth.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <math.h>

void Pass_bitwidth_options_pack::set(const std::string &label, const std::string &value) {

  max_iterations = 10; // FIXME: pass argument from cmd
}

Pass_bitwidth::Pass_bitwidth() {
}


void Pass_bitwidth::Node_properties::Explicit_range::dump() const {
  fmt::print("max{}:{} min{}:{} sign{}:{} {}"
      ,max_set?"_set":"",max
      ,min_set?"_set":"",min
      ,sign_set?"_set":"",sign
      ,overflow?"overflow":"");
}

bool Pass_bitwidth::Node_properties::Explicit_range::is_unsigned() const {
  return !sign_set || (sign_set && !sign);
}

void Pass_bitwidth::Node_properties::Explicit_range::set_sbits(uint16_t size) {
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

void Pass_bitwidth::Node_properties::Explicit_range::set_uconst(uint32_t val) {
  sign_set = true;
  sign     = false;

  max_set = true;
  min_set = true;
  max     = val;
  min     = val;
}

void Pass_bitwidth::Node_properties::Explicit_range::set_ubits(uint16_t size) {
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

void Pass_bitwidth::Node_properties::Implicit_range::dump() const {
  fmt::print("max:{} min:{} sign:{} {}",max,min,sign,overflow?"overflow":"");
}

int64_t Pass_bitwidth::Node_properties::Implicit_range::round_power2(int64_t x) const {
  uint64_t ux = abs(x);
  uint64_t ux_r = 1ULL<<(sizeof(uint64_t) * 8 - __builtin_clzll(ux));
  if (ux==static_cast<uint64_t>(x))
    return ux_r;

  return -ux_r;
}

bool Pass_bitwidth::Node_properties::Implicit_range::expand(const Implicit_range &i, bool round2) {

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

void Pass_bitwidth::Node_properties::Implicit_range::pick(const Explicit_range &e) {

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

void Pass_bitwidth::mark_all_outputs(const LGraph *lg, Index_ID idx) {

  for(const auto &out : lg->out_edges(idx)) {
    Index_ID dest_idx = out.get_idx();
    assert(lg->is_root(dest_idx));

    next_pending.set_bit(dest_idx);
  }

}

void Pass_bitwidth::iterate_graphio(const LGraph *lg, Index_ID idx) {

  // This should be called only if we do cross module propagation

  mark_all_outputs(lg,idx);
}

void Pass_bitwidth::iterate_logic(const LGraph *lg, Index_ID idx) {

  bool updated = false;
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();

    updated |= bw[idx].i.expand(bw[inp_idx].i, true);
  }

  if(updated) {
    mark_all_outputs(lg,idx);
  }
}

void Pass_bitwidth::iterate_arith(const LGraph *lg, Index_ID idx) {

  bool updated = false;
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();

    updated |= bw[idx].i.expand(bw[inp_idx].i, false);
  }

  if(updated) {
    mark_all_outputs(lg,idx);
  }
}

void Pass_bitwidth::iterate_pick(const LGraph *lg, Index_ID idx) {

  // At most like inputs, but constrain on output (pick can drop bits)

  Pass_bitwidth::Node_properties::Implicit_range imp;

  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();

    imp.expand(bw[inp_idx].i,true);
  }

  imp.pick(bw[idx].e);

  if (imp.max != bw[idx].i.max ||
      imp.min != bw[idx].i.min ||
      imp.sign != bw[idx].i.sign) {
    bw[idx].i = imp;
    mark_all_outputs(lg,idx);
  }

}

//------------------------------------------------------------------
//MIT Algorithm

void Pass_bitwidth::bw_pass_setup(LGraph *lg) {

  lg->each_output_root_fast([this,lg](Index_ID idx, Port_ID pid) {

    if (lg->get_bits(idx)==0)
      return;

    const auto &node = lg->node_type_get(idx);
    bool sign = false;
    if (node.op == U32Const_Op) {
      uint32_t val = lg->node_value_get(idx);
      bw[idx].e.set_uconst(val);
    }else {
      if (node.has_may_gen_sign()) {
        bool all_signed = true;
        for(const auto &inp : lg->inp_edges(idx)) {
          if (node.is_input_signed(inp.get_inp_pin().get_pid()))
            continue;

          all_signed = false;
          break;
        }
        sign = all_signed;
      }

      if (sign)
        bw[idx].e.set_sbits(lg->get_bits(idx));
      else
        bw[idx].e.set_ubits(lg->get_bits(idx));
    }

    bw[idx].set_implicit();
    assert(idx);
    pending.set_bit(idx);
  });
}

void Pass_bitwidth::iterate_node(LGraph *lg, Index_ID idx) {

  const auto &op = lg->node_type_get(idx);

  switch(op.op) {
    case GraphIO_Op:
      iterate_graphio(lg, idx);
      break;
    case And_Op:
    case Or_Op:
    case Xor_Op:
    case Not_Op:
      iterate_logic(lg, idx);
      break;
    case Sum_Op:
    case Div_Op:
    case Mult_Op:
    case Mod_Op:
      iterate_arith(lg, idx);
      break;
    case Pick_Op:
      iterate_pick(lg, idx);
      break;
    case U32Const_Op:
      // No need to iterate
      break;
    case LessThan_Op:
      fmt::print("I found a L.T.");
      break;
    case GreaterThan_Op:
      fmt::print("I found a G.T.");
      break;
    case LessEqualThan_Op:
      fmt::print("I found a L.E.T.");
      break;
    case GreaterEqualThan_Op:
      fmt::print("I found a G.E.T.");
      break;
    case Equals_Op:
      fmt::print("I found an EQUALS");
      break;
    case ShiftLeft_Op:
      fmt::print("I found a SHIFT_LEFT");
      break;
    case ShiftRight_Op:
      fmt::print("I found a SHIFT_RIGHT");
      break;
    case Mux_Op:
      fmt::print("I found a MUX");
      break;
    case Join_Op:
      fmt::print("I found a JOIN");
      break;
    case Flop_Op:
      fmt::print("I found a FLOP");
      break;
    case SubGraph_Op:
      fmt::print("I found a SUBGRAPH");
      break;
    case Latch_Op:
      fmt::print("I found a LATCH");
      break;
    case StrConst_Op:
      fmt::print("I found a STRING");
      break;
    default:
      fmt::print("FIXME! op not found");
  }
}

void Pass_bitwidth::bw_pass_dump(LGraph *lg) {

  lg->each_input_root_fast([this,lg](Index_ID idx, Port_ID pid) {

    const auto &node = lg->node_type_get(idx);

    fmt::print("inp {}:{} {} ", idx, pid, node.get_name());
    bw[idx].i.dump();
    fmt::print("\n  ");
    bw[idx].e.dump();
    fmt::print("\n");

  });

  lg->each_output_root_fast([this,lg](Index_ID idx, Port_ID pid) {

    const auto &node = lg->node_type_get(idx);

    fmt::print("out {}:{} {} ", idx, pid, node.get_name());
    bw[idx].i.dump();
    fmt::print("\n  ");
    bw[idx].e.dump();
    fmt::print(" ({})\n",bw[idx].niters);

  });

}

bool Pass_bitwidth::bw_pass_iterate(LGraph *lg) {

  int iterations = 0;

  do{
    assert(next_pending.none());

    Index_ID idx = pending.get_first();
    pending.clear_bit(idx);
    bw[idx].niters++;
    if (bw[idx].niters>opack.max_iterations) {
      fmt::print("bw_pass_iterate abort {}\n",iterations);
      return false;
    }

    do{
      iterate_node(lg, idx);
      idx = pending.extract_next(idx);
    }while(idx);

    assert(pending.none());
    if (next_pending.none()) {
      fmt::print("bw_pass_iterate pass {}\n",iterations);
      return true;
    }

    pending.swap(next_pending);

    iterations++;

  }while(true);

  assert(false);

  return false;
}

void Pass_bitwidth::trans(LGraph *lg) {
  {
    LGBench b("pass.bitwidth");

    bw.resize(lg->size());  // FIXME: this should be moved to table like nodedelay.cpp

    bw_pass_setup(lg);

    bool done = bw_pass_iterate(lg);
    if (!done) {
      console->error("could not converge in the iterations FIXME: dump nice message on why\n");
    }
  }

  bw_pass_dump(lg);
}

