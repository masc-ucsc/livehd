//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <math.h>
#include <string>
#include <vector>

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "graph_library.hpp"

#include "pass_bitwidth.hpp"

#define DEBUG_STATEMENTS

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

  pass.opack.max_iterations = std::stoi(std::string(var.get("max_iterations")));
  if(pass.opack.max_iterations == 0) {
    error(fmt::format("pass.bitwidth max_iterations:{} should be bigger than zero", var.get("max_iterations")));
    return;
  }

  std::vector<const LGraph *> lgs;
  for(const auto &l : var.lgs) {
    pass.do_trans(l);
  }
}

void Pass_bitwidth::mark_all_outputs(const LGraph *lg, Node_pin &pin) {
  //Mark driver pins that need to change based off current driver pin.
  Node curr_node = pin.get_node();
  for (const auto &out_edge : curr_node.out_edges()) {
    auto spin = out_edge.sink;
    auto affected_node = spin.get_node();
    fmt::print("\t\tAFF_OUT EDGE {}\n", affected_node.get_type().op == GraphIO_Op);
    for (const auto &aff_out_edges : affected_node.out_edges()) {
      fmt::print("\t\t\tAFF_DRIVER_PIN\n");
      next_pending.push_back(aff_out_edges.driver);
    }
  }

  /*for(const auto &out : lg->out_edges(idx)) {
    Index_ID dest_idx = out.get_idx();
    assert(lg->is_root(dest_idx));

    next_pending.set_bit(dest_idx);
  }*/
}

/*void Pass_bitwidth::iterate_graphio(const LGraph *lg, Index_ID idx) {

  // This should be called only if we do cross module propagation

  //This helps us detect if the IO node is an output.
  //  If it is an output, we cascade range info down into it.
  for(const auto &inp : lg->inp_edges(idx)) {
    //An input node should have nothing as its input.
    //is_output_node = true;
    Index_ID inp_idx = inp.get_idx();
    const auto &op = lg->node_type_get(inp_idx);

    if(op.op == SubGraph_Op) {
      //No changes should be necessary, handled in iterate_subgraph.
    } else {
      auto &nb_output = lg->node_bitwidth_get(idx);
      const auto &nb_input  = lg->node_bitwidth_get(inp_idx);
      nb_output.i = nb_input.i;
    }

  }
  mark_all_outputs(lg, idx);
}

void Pass_bitwidth::iterate_logic(const LGraph *lg, Index_ID idx) {

  bool updated = false;
  auto &nb_output = lg->node_bitwidth_get(idx);
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);

    updated |= nb_output.i.expand(nb_input.i, true);
  }

  if(updated) {
    mark_all_outputs(lg, idx);
  }
}*/

void Pass_bitwidth::iterate_arith(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {

  bool updated = false;

  //From this driver pin's node, look at inp edges and figure out bw info from those.
  Ann_bitwidth::Implicit_range imp;

  auto curr_node = pin.get_node();
  for (const auto &inp_edge : curr_node.inp_edges()) {
    auto dpin = inp_edge.driver;
    switch(op) {
      case Sum_Op:
        //FIXME: At this point in time, I'm just going to assume everything is +.
        //  Update later for -.
        imp.max += dpin.get_bitwidth().i.max;
        imp.min += dpin.get_bitwidth().i.min;
        break;
      case Mult_Op:
        break;
      case Div_Op:
        break;
      case Mod_Op:
        break;
      default:
        fmt::print("Error: arith op not understood\n");
    }
  }

  updated |= pin.ref_bitwidth()->i.update(imp);  //.i.expand(imp, false);
  /*int64_t max = 0;
  int64_t min = 0;

  bool first = true;
  bool add;
  Node_bitwidth::Implicit_range imp;
  auto &nb_output = lg->node_bitwidth_get(idx);

  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);

    const auto &op = lg->node_type_get(idx);
    switch(op.op) {
      case Sum_Op:
        if(first) {
          if(inp.get_inp_pin().get_pid() == 0 || inp.get_inp_pin().get_pid() == 1) {
            add = true;
          } else {
            add = false;
          }
          min = nb_input.i.min;
          max = nb_input.i.max;
          imp.sign = nb_input.i.sign;
          first = false;
        } else {
          if (add) {
            imp.min = nb_input.i.min + min;
            imp.max = nb_input.i.max + max;
          } else {
            imp.min = nb_input.i.min - max;
            imp.max = nb_input.i.max - min;
          }
          imp.sign &= nb_input.i.sign;
        }
        break;

      case Mult_Op:
        if (first) {
          min = nb_input.i.min;
          max = nb_input.i.max;
          imp.sign = nb_input.i.sign;
          first = false;
        } else {
          imp.min = nb_input.i.min * min;
          imp.max = nb_input.i.max * max;
          imp.sign &= nb_input.i.sign;
        }
        break;

      // In Verilator if I divide x by 0, the output is 0. We use that assumption here.
      case Div_Op:
        if (first) {
          min = nb_input.i.min;
          max = nb_input.i.max;
          imp.sign = nb_input.i.sign;
          first = false;
        } else {
          if (max == 0) {
            imp.min = 0;
          } else {
            imp.min = (int64_t)floor((double)nb_input.i.min / (double)max);
          }

          if (min == 0) {
            if (max == 0) {
              imp.max = 0;
            } else {
              imp.max = nb_input.i.max;
            }
          } else {
            imp.max = (int64_t)floor((double)nb_input.i.max / (double)min);
          }
          imp.sign &= nb_input.i.sign;
        }
        break;

      //FIXME: We have to make conservative estimates since modulo is hard to restrict range.
      case Mod_Op:
        updated |= imp.expand(nb_input.i, false);
        break;

      default:
        fmt::print("FIXME! op not found!");
    }
  }
  updated |= nb_output.i.expand(imp, false);*/

  if(updated) {
    fmt::print("\tDo update\n");
    mark_all_outputs(lg,pin);
  }
}

/*void Pass_bitwidth::iterate_shift(const LGraph *lg, Index_ID idx) {
  //FIXME: This does not work for >>> currently. Only looks at first two node values.
  bool updated = false;

  int pos = 0;
  int64_t val_to_shift_min, val_to_shift_max;
  for (const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();

    if (pos == 0) {
      auto &nb_input = lg->node_bitwidth_get(inp_idx);
      val_to_shift_min = nb_input.i.min;
      val_to_shift_max = nb_input.i.max;
    } else {
      auto nb_input = lg->node_bitwidth_get(inp_idx);
      auto &nb_output = lg->node_bitwidth_get(idx);

      const auto &op = lg->node_type_get(idx);
      switch(op.op) {
        case ShiftLeft_Op:
          nb_input.i.min = val_to_shift_min << nb_input.i.min;
          nb_input.i.max = val_to_shift_max << nb_input.i.max;
          break;
        case ShiftRight_Op:
          //FIXME: How to determine between >> and >>> ?
          nb_input.i.min = val_to_shift_min >> nb_input.i.min;
          nb_input.i.max = val_to_shift_max >> nb_input.i.max;
          break;
        default:
          fmt::print("op not found!\n");
      }

      updated |= nb_output.i.expand(nb_input.i, false);
    }
    pos++;
  }

  if(updated) {
    mark_all_outputs(lg, idx);
  }
}

void Pass_bitwidth::iterate_comparison(const LGraph *lg, Index_ID idx) {
  //NOTE: The system will take care of constant values such as (a >= 4'b0) or (4'b1111 > 4'b0111)
  //      behind the scenes.
  int64_t min1, max1;
  bool first = true;
  bool updated = false;

  auto &nb_output = lg->node_bitwidth_get(idx);
  for (const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);

    if (first) {
      min1 = nb_input.i.min;
      max1 = nb_input.i.max;
      first = false;
    } else {
      const auto &op = lg->node_type_get(idx);
      //NOTE: Need a data set to test on. I believe this functions are correct.
      switch(op.op) {
        case LessThan_Op:
          if (nb_input.i.max < min1) {
            //Output is always true
            nb_output.i.min = 1;
            nb_output.i.max = 1;
            updated = true;
          } else if (nb_input.i.min >= max1) {
            //Output is always false
            nb_output.i.min = 0;
            nb_output.i.max = 0;
            updated = true;
          }
          break;
        case GreaterThan_Op:
          if (nb_input.i.min > max1) {
            //True always
            nb_output.i.min = 1;
            nb_output.i.max = 1;
            updated = true;
          } else if (nb_input.i.max <= min1) {
            //False always
            nb_output.i.min = 0;
            nb_output.i.max = 0;
            updated = true;
          }
          break;
        case LessEqualThan_Op:
          if (nb_input.i.max <= min1) {
            //True always
            nb_output.i.min = 1;
            nb_output.i.max = 1;
            updated = true;
          } else if (nb_input.i.min > max1) {
            //False always
            nb_output.i.min = 0;
            nb_output.i.max = 0;
            updated = true;
          }
          break;
        case GreaterEqualThan_Op:
          if (nb_input.i.min >= max1) {
            //True always
            nb_output.i.min = 1;
            nb_output.i.max = 1;
            updated = true;
          } else if (nb_input.i.max < max1) {
            //False always
            nb_output.i.min = 0;
            nb_output.i.max = 0;
            updated = true;
          }
          break;
        default:
          fmt::print("op not found!\n");
      }
    }
  }

  if (updated) {
    mark_all_outputs(lg, idx);
  }
}

void Pass_bitwidth::iterate_pick(const LGraph *lg, Index_ID idx) {
  // At most like inputs, but constrain on output (pick can drop bits)
  Node_bitwidth::Implicit_range imp;

  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    auto &nb      = lg->node_bitwidth_get(inp_idx);

    imp.expand(nb.i, true);
  }

  auto &nb = lg->node_bitwidth_get(idx);

  imp.pick(nb.e);

  if(imp.max != nb.i.max || imp.min != nb.i.min || imp.sign != nb.i.sign) {
    nb.i = imp;
    mark_all_outputs(lg, idx);
  }
}

void Pass_bitwidth::iterate_equals(const LGraph *lg, Index_ID idx) {
  bool updated;
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    auto       &nb_output = lg->node_bitwidth_get(idx);
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);
    updated |= nb_output.i.expand(nb_input.i,false);
  }

  if(updated) {
    mark_all_outputs(lg,idx);
  }
}

void Pass_bitwidth::iterate_mux(const LGraph *lg, Index_ID idx) {
  Node_bitwidth::Implicit_range imp;

  bool updated = false;
  bool first = true;
  auto &nb_output = lg->node_bitwidth_get(idx);
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    const auto &nb_input  = lg->node_bitwidth_get(inp_idx);
    if (first) {
      //Selector value.
      first = false;
    } else {
      updated |= nb_output.i.expand(nb_input.i, false);
    }
  }

  if(updated) {
    mark_all_outputs(lg,idx);
  }
}

void Pass_bitwidth::iterate_subgraph(const LGraph *lg, Index_ID idx) {

  std::vector<LGraph *> lgs;
  bool updated = false;

  auto subgraph_name = lg->get_library().get_name(lg->subgraph_id_get(idx));
  LGraph *sg = LGraph::open(lg->get_path(), subgraph_name);

  //Here we populate our vector with all the subgraph's IO indices.
  //FIXME: There has to be a better way to find just the subgraph's graphio nodes.
  std::vector<Index_ID> sg_io_idx;
  lg->each_graph_output([this, sg, &sg_io_idx](const Node_pin &pin) {
    assert(sg->node_type_get(sg->get_node(pin).get_nid()).op == GraphIO_Op);
    sg_io_idx.push_back(pin.get_idx());
  });

  do_trans(sg);
  //Here we detect which indices of our main graph feed into/out of the
  //  subgraph.
  for(const auto &inp : lg->inp_edges(idx)) {
    Index_ID inp_idx = inp.get_idx();
    auto tmp = sg_io_idx.at(sg_io_idx.size() - inp.get_inp_pin().get_pid());
    const auto &lg_node  = lg->node_bitwidth_get(inp_idx);
    auto       &sg_node  = sg->node_bitwidth_get(tmp);

    updated |= sg_node.i.expand(lg_node.i, false);
  }

  for(const auto &out : lg->out_edges(idx)) {
    Index_ID out_idx = out.get_idx();
    auto tmp = sg_io_idx.at(sg_io_idx.size() - out.get_out_pin().get_pid());
    const auto &sg_node = sg->node_bitwidth_get(tmp);
    auto       &lg_node = lg->node_bitwidth_get(out_idx);

    updated |= lg_node.i.expand(sg_node.i, false);
  }

  if(updated) {
    mark_all_outputs(lg,idx);
  }
}*/

//------------------------------------------------------------------
// MIT Algorithm
void Pass_bitwidth::bw_pass_setup(LGraph *lg) {
  //Instantiate bitwidths for input pins to graph (from input graphio)
  lg->each_graph_input([this, lg](const Node_pin &pin) {
    fmt::print("inp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={}",
        pin.get_name(), pin.is_sink(), pin.is_driver(), pin.get_pid(), pin.has_bitwidth());

    if(pin.get_bits() == 0) {
      fmt::print(" -- implicit\n");
      return;
    } else {
      fmt::print(" -- explicit (bits set)\n");
    }

    //FIXME: At this point in time, I need to setup bitwidths for explicit.
    //    Eventually, Sheng should do this instead (so remove some of the explicit stuff later).
    Node_pin editable_pin = pin;
    //Set explicit.
    editable_pin.ref_bitwidth()->e.set_ubits(pin.get_bits());
    //Set implicit.
    editable_pin.ref_bitwidth()->set_implicit();
    //Print out ranges for debug
    fmt::print("\texp: ");
    pin.get_bitwidth().e.dump();
    fmt::print("\n\timp: ");
    pin.get_bitwidth().i.dump();
    fmt::print("\n");
  });

  fmt::print("\n");

  for (const auto &node:lg->forward()) {
    fmt::print("type: {} {}\n", node.get_type().op, node.get_type().op == Join_Op);

    //Iterate over inputs to some node.
    for (const auto &out_edge : node.out_edges()) {
      auto dpin = out_edge.driver;
      auto spin = out_edge.sink;
      fmt::print("name_o:{} {} pid:{} -> name:{} pid:{}\n",
          dpin.debug_name(), dpin.get_bits(), dpin.get_pid(), spin.debug_name(), spin.get_pid());

      //FIXME: Currently, first iteration will iterate over same driver pins multiple times, in some cases. (If more than 1 edge has pin X as its driver)
      pending.push_back(dpin);

      if(dpin.get_bits() == 0) {
        fmt::print(" -- implicit\n");
        return;
      }

      //bool sign = false;
      if(node.get_type().op == U32Const_Op) {
        //FIXME: Need to find API to figure out what value for this pin is.
        //uint32_t val = lg->node_value_get(dpin);
      } else {
        //Set bitwidth for output edge driver.
        //FIXME: Focused only on unsigned, will have to change later.
        //FIXME x2: Should I even be doing this? Should be Sheng, I think.
        dpin.ref_bitwidth()->e.set_ubits(out_edge.get_bits());
        fmt::print("\t");
        dpin.ref_bitwidth()->e.dump();
        fmt::print("\n");
      }
    }
    fmt::print("-----------------------\n\n");
  }
}

void Pass_bitwidth::iterate_driver_pin(LGraph *lg, Node_pin &pin) {
  const auto node = pin.get_node();
  const auto node_type = node.get_type().op;

  switch(node_type) {
    //NOTE TO SELF/FIXME: GraphIO will never happen here, right? Maybe from subgraph nodes?
    case GraphIO_Op:
      fmt::print("GraphIO_Op\n");
      break;
    case Sum_Op:
    case Mult_Op:
    case Div_Op:
    case Mod_Op:
      fmt::print("Arith Op\n");
      iterate_arith(lg, pin, node_type);
      break;
    case Join_Op:
      fmt::print("Join_Op\n");
      break;
    case Pick_Op:
      fmt::print("Pick_Op\n");
      break;
    default:
      fmt::print("Op not yet supported in iterate_driver_pin\n");
  }
}

//FIXME: I basically removed everything from this, should get it
//back to some working condition later for debugging.
void Pass_bitwidth::bw_pass_dump(LGraph *lg) {

  lg->each_graph_input([this, lg](const Node_pin &pin) {
    fmt::print("inp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={} ",
        pin.get_name(), pin.is_sink(), pin.is_driver(), pin.get_pid(), pin.has_bitwidth());
    pin.get_bitwidth().e.dump();
    fmt::print("\n");
  });

  lg->each_graph_output([this, lg](const Node_pin &pin) {
      fmt::print("outp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={}\n",
          pin.get_name(), pin.is_sink(), pin.is_driver(), pin.get_pid(), pin.has_bitwidth());
  });

  fmt::print("\n");
}

bool Pass_bitwidth::bw_pass_iterate(LGraph *lg) {
  if(pending.empty()) {
    fmt::print("\nbw_pass_iterate pass -- no driver pins to iterate over\n");
  }

  int iterations = 0;

  do {
    assert(next_pending.empty());
    fmt::print("Iteration {}:\n", iterations);

    auto &dpin = pending.back();
    pending.pop_back();
    dpin.ref_bitwidth()->niters++;

    if(dpin.ref_bitwidth()->niters > opack.max_iterations) {
      //FIXME: With using the current std::vector, this might fire off much earlier
      //  than I'd want it to (if pin got added mult times due to mult out edges).
      fmt::print("\nbw_pass_iterate abort {}\n", iterations);
      return false;
    }

    do {
      iterate_driver_pin(lg, dpin);
      if(pending.empty())
        break;
      dpin = pending.back();
      pending.pop_back();
    } while(true);

    assert(pending.empty());
    if(next_pending.empty()) {
      fmt::print("\nbw_pass_iterate pass {}\n", iterations);
      return true;
    }

    pending = next_pending;
    next_pending.clear();

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
      Pass::error("could not converge in the iterations FIXME: dump nice message on why\n");
    }
  }

  bw_pass_dump(lg);
}
