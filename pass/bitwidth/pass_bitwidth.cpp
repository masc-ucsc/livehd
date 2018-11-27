//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <math.h>
#include <string>
#include <vector>

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass_bitwidth.hpp"

void setup_pass_bitwidth() {
  Pass_bitwidth p;
  p.setup();
}

void Pass_bitwidth::setup() {
  Eprp_method m1("pass.bitwidth", "MIT algorithm... FIXME", &Pass_bitwidth::trans);

  m1.add_label_optional("max_iterations", "maximum number of iterations to try", "10");

  register_pass(m1);
}

Pass_bitwidth::Pass_bitwidth()
    : Pass("bitwidth") {
}

void Pass_bitwidth::trans(Eprp_var &var) {

  Pass_bitwidth pass;

  pass.opack.max_iterations = std::stoi(var.get("max_iterations"));
  if(pass.opack.max_iterations == 0) {
    error(fmt::format("pass.bitwidth max_iterations:{} should be bigger than zero", var.get("max_iterations")));
    return;
  }

  std::vector<const LGraph *> lgs;
  for(const auto &l : var.lgs) {
    pass.do_trans(l);
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

  mark_all_outputs(lg, idx);
}

void Pass_bitwidth::iterate_logic(const LGraph *lg, Index_ID idx) {

  bool updated = false;
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();

    auto &      nb_output = lg->node_bitwidth_get(idx);
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);

    updated |= nb_output.i.expand(nb_input.i, true);
  }

  if(updated) {
    mark_all_outputs(lg, idx);
  }
}

void Pass_bitwidth::iterate_arith(const LGraph *lg, Index_ID idx) {

  bool updated = false;
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();

    auto &      nb_output = lg->node_bitwidth_get(idx);
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);

    updated |= nb_output.i.expand(nb_input.i, false);
  }

  if(updated) {
    mark_all_outputs(lg, idx);
  }
}

void Pass_bitwidth::iterate_pick(const LGraph *lg, Index_ID idx) {
  // At most like inputs, but constrain on output (pick can drop bits)
  Node_bitwidth::Implicit_range imp;

  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    auto &   nb      = lg->node_bitwidth_get(inp_idx);

    imp.expand(nb.i, true);
  }

  auto &nb = lg->node_bitwidth_get(idx);

  imp.pick(nb.e);

  if(imp.max != nb.i.max || imp.min != nb.i.min || imp.sign != nb.i.sign) {
    nb.i = imp;
    mark_all_outputs(lg, idx);
  }
}

//------------------------------------------------------------------
// MIT Algorithm
void Pass_bitwidth::bw_pass_setup(LGraph *lg) {

  lg->each_output_root_fast([this, lg](Index_ID idx, Port_ID pid) {
    if(lg->get_bits(idx) == 0)
      return;

    const auto &node = lg->node_type_get(idx);
    auto &      nb   = lg->node_bitwidth_get(idx);

    bool sign = false;
    if(node.op == U32Const_Op) {
      uint32_t val = lg->node_value_get(idx);
      nb.e.set_uconst(val);
    } else {
      if(node.has_may_gen_sign()) {
        bool all_signed = true;
        for(const auto &inp : lg->inp_edges(idx)) {
          if(node.is_input_signed(inp.get_inp_pin().get_pid()))
            continue;

          all_signed = false;
          break;
        }
        sign = all_signed;
      }

      if(sign)
        nb.e.set_sbits(lg->get_bits(idx));
      else
        nb.e.set_ubits(lg->get_bits(idx));
    }

    nb.set_implicit();
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
  case SFlop_Op:
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

  lg->each_input_root_fast([this, lg](Index_ID idx, Port_ID pid) {
    const auto &node = lg->node_type_get(idx);
    const auto &nb   = lg->node_bitwidth_get(idx);

    fmt::print("inp {}:{} {} ", idx, pid, node.get_name());
    nb.i.dump();
    fmt::print("\n  ");
    nb.e.dump();
    fmt::print("\n");
  });

  lg->each_output_root_fast([this, lg](Index_ID idx, Port_ID pid) {
    const auto &node = lg->node_type_get(idx);
    const auto &nb   = lg->node_bitwidth_get(idx);

    fmt::print("out {}:{} {} ", idx, pid, node.get_name());
    nb.i.dump();
    fmt::print("\n  ");
    nb.e.dump();
    fmt::print(" ({})\n", nb.niters);
  });
}

bool Pass_bitwidth::bw_pass_iterate(LGraph *lg) {

  int iterations = 0;

  do {
    assert(next_pending.none());

    Index_ID idx = pending.get_first();
    auto &   nb  = lg->node_bitwidth_get(idx);

    pending.clear_bit(idx);
    nb.niters++;
    if(nb.niters > opack.max_iterations) {
      fmt::print("bw_pass_iterate abort {}\n", iterations);
      return false;
    }

    do {
      iterate_node(lg, idx);
      idx = pending.extract_next(idx);
    } while(idx);

    assert(pending.none());
    if(next_pending.none()) {
      fmt::print("bw_pass_iterate pass {}\n", iterations);
      return true;
    }

    pending.swap(next_pending);

    iterations++;

  } while(true);

  assert(false);

  return false;
}

void Pass_bitwidth::do_trans(LGraph *lg) {
  {
    LGBench b("pass.bitwidth");

    bw_pass_setup(lg);

    bool done = bw_pass_iterate(lg);
    if(!done) {
      console->error("could not converge in the iterations FIXME: dump nice message on why\n");
    }
  }

  bw_pass_dump(lg);
}
