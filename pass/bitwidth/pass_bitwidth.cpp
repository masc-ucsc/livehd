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
}*/

void Pass_bitwidth::iterate_logic(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  bool updated;
  bool first = true;

  //FIXME: Currently treating everything as unsigned
  Ann_bitwidth::Implicit_range imp;

  for (const auto &inp_edge : pin.get_node().inp_edges()) {
    fmt::print("\titer -- ");
    switch(op) {
      case And_Op:
        fmt::print(" AND\n");
      //Make bw = <0, max(inputs)> since & op can never exceed largest input value.
        if(first) {
          imp.min = 0;
          imp.max = inp_edge.driver.get_bitwidth().i.max;
          first = false;
        } else if(inp_edge.driver.get_bitwidth().i.max < imp.max) {
          imp.max = inp_edge.driver.get_bitwidth().i.max;
        }
        break;
      case Or_Op:
      //Make bw = <min(inputs), 2^n - 1> where n is the largest bitwidth of inputs to this node.
        fmt::print(" OR\n");
        if(first) {
          imp.min = inp_edge.driver.get_bitwidth().i.min;
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max+1));
          imp.max = pow(2, bits)-1;
          first = false;
        } else {
          if (inp_edge.driver.get_bitwidth().i.min > imp.min) {
            imp.min = inp_edge.driver.get_bitwidth().i.min;
          }

          if (inp_edge.driver.get_bitwidth().i.max > imp.max) {
            double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max+1));
            imp.max = pow(2, bits)-1;
          }
        }
        break;
      case Xor_Op:
      //Make bw = <0, 2^n - 1> where n is the largest bitwidth of inputs to this node.
        fmt::print(" XOR\n");
        if(first) {
          imp.min = 0;
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max+1));
          imp.max = pow(2, bits)-1;
          first = false;
        } else if(inp_edge.driver.get_bitwidth().i.max > imp.max) {
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max+1));
          imp.max = pow(2, bits)-1;
        }
        break;
      case Not_Op:
        //FIXME: Not gets really complicated... I need to think this over.
        fmt::print("Not op not yet implemented.\n");
        break;
      default:
        fmt::print("Error: logic op not understood\n");
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp);
  if(updated) {
    mark_all_outputs(lg, pin);
  }
}

void Pass_bitwidth::iterate_arith(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {

  bool updated;

  //From this driver pin's node, look at inp edges and figure out bw info from those.
  Ann_bitwidth::Implicit_range imp;

  auto curr_node = pin.get_node();
  bool first = true;
  for (const auto &inp_edge : curr_node.inp_edges()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    switch(op) {
      case Sum_Op:
        //FIXME: At this point in time, I'm just going to assume everything is +.
        //  Update later for -.
        //PID 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y. Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
        if(spin.get_pid() == 0 || spin.get_pid() == 1) {//NOTE: I think this should be spin, rethink over this later.
          imp.max += dpin.get_bitwidth().i.max;
          imp.min += dpin.get_bitwidth().i.min;
          fmt::print("\tadd: ");
          imp.dump();
          fmt::print("\n");
        } else {
          imp.min -= dpin.get_bitwidth().i.max;
          imp.max -= dpin.get_bitwidth().i.min;
          fmt::print("\tadd: ");
          imp.dump();
          fmt::print("\n");
          //fmt::print("\tsub: imp.max = {}, imp.min = {}\n", imp.max, imp.min);
        }
        break;
      case Mult_Op:
        if(first) {
          imp.min = dpin.get_bitwidth().i.min;
          imp.max = dpin.get_bitwidth().i.max;
          first = false;
        } else {
          imp.min *= dpin.get_bitwidth().i.min;
          imp.max *= dpin.get_bitwidth().i.max;
        }
        break;
      case Div_Op:
        //FIXME: Implement
        break;
      case Mod_Op:
        //FIXME: Implement
        break;
      default:
        fmt::print("Error: arith op not understood\n");
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp);
  if(updated) {
    fmt::print("\tDo update\n");
    mark_all_outputs(lg,pin);
  }

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
}*/

void Pass_bitwidth::iterate_comparison(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  //NOTE: The system will take care of constant values such as (a >= 4'b0) or (4'b1111 > 4'b0111)
  //      behind the scenes. FIXME: This was an old note. Is it still true?

  bool updated = false;
  bool set = false;
  Ann_bitwidth::Implicit_range imp;
  imp.min = 1;
  imp.max = 1;

  //Due to nature of comparison type nodes, I need to iterate over every BS|BU pin
  //  for each AS|AU pin I iterate over.
  for (const auto &inp_edge : pin.get_node().inp_edges()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    if(spin.get_pid() == 0 || spin.get_pid() == 1) {//If AS or AU pins
      for (const auto &comp_edge : pin.get_node().inp_edges()) {
        auto comp_dpin = comp_edge.driver;
        auto comp_spin = comp_edge.sink;
        if(comp_spin.get_pid() == 2 || comp_spin.get_pid() == 3) {//If BS or BU pins
          set = true;
          switch(op) {
            //NOTE: At some point, go back over this logic and make sure it's right.
            case LessThan_Op:
              imp.min &= dpin.get_bitwidth().i.max < comp_dpin.get_bitwidth().i.min;
              imp.max &= dpin.get_bitwidth().i.min < comp_dpin.get_bitwidth().i.max;
              break;
            case GreaterThan_Op:
              imp.min &= dpin.get_bitwidth().i.min > comp_dpin.get_bitwidth().i.max;
              imp.max &= dpin.get_bitwidth().i.max > comp_dpin.get_bitwidth().i.min;
              break;
            case LessEqualThan_Op:
              imp.min &= dpin.get_bitwidth().i.max <= comp_dpin.get_bitwidth().i.min;
              imp.max &= dpin.get_bitwidth().i.min <= comp_dpin.get_bitwidth().i.max;
              break;
            case GreaterEqualThan_Op:
              imp.min &= dpin.get_bitwidth().i.min >= comp_dpin.get_bitwidth().i.max;
              imp.max &= dpin.get_bitwidth().i.max >= comp_dpin.get_bitwidth().i.min;
              break;
            default:
              fmt::print("Error: comparison op not understood\n");
          }
        }
      }
    }
  }

  if(set) {
    updated = pin.ref_bitwidth()->i.update(imp);
  }

  if(updated) {
    fmt::print("\tDo update\n");
    mark_all_outputs(lg,pin);
  }
}

void Pass_bitwidth::iterate_join(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  //FIXME: Still in progress.
  fmt::print("\titerate_join:\n");
  for (const auto &inp_edge : pin.get_node().inp_edges()) {
    auto spin = inp_edge.sink;
    fmt::print("\t\t{} {}\n", spin.debug_name(), spin.get_pid());
  }
  for (const auto &out_edge : pin.get_node().out_edges()) {
    auto dpin = out_edge.driver;
    fmt::print("\t\t{} {}\n", dpin.debug_name(), dpin.get_pid());
  }
}

void Pass_bitwidth::iterate_pick(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  //FIXME: Still in progress.
  fmt::print("\titerate_pick:\n");
  for (const auto &inp_edge : pin.get_node().inp_edges()) {
    auto spin = inp_edge.sink;
    fmt::print("\t\t{}\n", spin.get_pid());
  }

  // At most like inputs, but constrain on output (pick can drop bits)
  /*Node_bitwidth::Implicit_range imp;

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
  }*/
}

/*void Pass_bitwidth::iterate_equals(const LGraph *lg, Index_ID idx) {
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
        dpin.ref_bitwidth()->e.set_uconst(out_edge.get_bits());//FIXME: The argument to this function is way wrong, but needed for testing.
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
    case And_Op:
    case Or_Op:
    case Xor_Op:
    case Not_Op:
      fmt::print("Logic Op\n");
      iterate_logic(lg, pin, node_type);
      break;
    case Sum_Op:
    case Mult_Op:
    case Div_Op:
    case Mod_Op:
      fmt::print("Arith Op\n");
      iterate_arith(lg, pin, node_type);
      break;
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op:
      iterate_comparison(lg, pin, node_type);
      break;
    case Equals_Op:
      //iterate_equals(lg, pin, node_type);
      break;
    case ShiftLeft_Op:
    case ShiftRight_Op:
      //iterate_shift(lg, pin, node_type);
      break;
    case Join_Op:
      fmt::print("Join_Op\n");
      iterate_join(lg, pin, node_type);
      break;
    case Pick_Op:
      fmt::print("Pick_Op\n");
      iterate_pick(lg, pin, node_type);
      break;
    case U32Const_Op:
      //Nothing needs to be done for constants.
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
