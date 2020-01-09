//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_bitwidth.hpp"

#include <math.h>

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

#include "graph_library.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_bitwidth() { Pass_bitwidth::setup(); }

void Pass_bitwidth::setup() {
  Eprp_method m1("pass.bitwidth", "MIT algorithm... FIXME", &Pass_bitwidth::trans);

  m1.add_label_optional("max_iterations", "maximum number of iterations to try", "10");

  register_pass(m1);
}

Pass_bitwidth::Pass_bitwidth(const Eprp_var &var) : Pass("pass.bitwidth", var) {
  auto miters = var.get("max_iterations");

  std::from_chars(miters.data(), miters.data() + miters.size(), max_iterations);

  if (max_iterations == 0) {
    error("pass.bitwidth max_iterations:{} should be bigger than zero", miters);
    return;
  }
}

void Pass_bitwidth::trans(Eprp_var &var) {
  Pass_bitwidth p(var);

  std::vector<const LGraph *> lgs;
  for (const auto &l : var.lgs) {
    p.do_trans(l);
  }
}

void Pass_bitwidth::mark_all_outputs(const LGraph *lg, Node_pin &pin) {
  // Mark driver pins that need to change based off current driver pin.
  Node curr_node = pin.get_node();

  pin.get_bitwidth().i.dump();
  fmt::print("\n");
  for (const auto &out_edge : curr_node.out_edges()) {
    auto spin          = out_edge.sink;
    auto affected_node = spin.get_node();
    fmt::print("\t\tAFF_OUT EDGE {}\n", affected_node.get_type().op == GraphIO_Op);
    for (const auto &aff_out_edges : affected_node.out_edges()) {
      fmt::print("\t\t\tAFF_DRIVER_PIN\n");
      if (std::find(next_pending.begin(), next_pending.end(), aff_out_edges.driver) == next_pending.end()) {
        // If pin is not already in next_pending, add it to vector.
        next_pending.push_back(aff_out_edges.driver);
      }
    }
  }
}

/*void Pass_bitwidth::iterate_graphio(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {

  // This is called on the input GraphIO_Op node.

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

  // FIXME: Currently treating everything as unsigned
  Ann_bitwidth::Implicit_range imp;

  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
    fmt::print("\titer -- ");
    switch (op) {
      case And_Op:
        fmt::print(" AND\n");
        // Make bw = <0, max(inputs)> since & op can never exceed largest input value.
        if (first) {
          imp.min = 0;
          imp.max = inp_edge.driver.get_bitwidth().i.max;
          first   = false;
        } else if (inp_edge.driver.get_bitwidth().i.max < imp.max) {
          imp.max = inp_edge.driver.get_bitwidth().i.max;
        }
        fmt::print("\t\t{} {}\n", (int)imp.min, (int)imp.max);
        break;
      case Or_Op:
        // Make bw = <min(inputs), 2^n - 1> where n is the largest bitwidth of inputs to this node.
        fmt::print(" OR\n");
        if (first) {
          imp.min     = inp_edge.driver.get_bitwidth().i.min;
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
          imp.max     = pow(2, bits) - 1;
          first       = false;
        } else {
          if (inp_edge.driver.get_bitwidth().i.min > imp.min) {
            imp.min = inp_edge.driver.get_bitwidth().i.min;
          }

          if (inp_edge.driver.get_bitwidth().i.max > imp.max) {
            double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
            imp.max     = pow(2, bits) - 1;
          }
        }
        break;
      case Xor_Op:
        // Make bw = <0, 2^n - 1> where n is the largest bitwidth of inputs to this node.
        fmt::print(" XOR\n");
        if (first) {
          imp.min     = 0;
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
          imp.max     = pow(2, bits) - 1;
          first       = false;
        } else if (inp_edge.driver.get_bitwidth().i.max > imp.max) {
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
          imp.max     = pow(2, bits) - 1;
        }
        break;
      case Not_Op:
        // FIXME: Not gets really complicated... I need to think this over.
        fmt::print("Not op not yet implemented.\n");
        break;
      default: fmt::print("Error: logic op not understood\n");
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp);
  if (updated) {
    fmt::print("\tUpdate: ");
    mark_all_outputs(lg, pin);
  }
}

void Pass_bitwidth::iterate_arith(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  bool updated = false;

  // From this driver pin's node, look at inp edges and figure out bw info from those.
  Ann_bitwidth::Implicit_range imp;

  auto curr_node = pin.get_node();
  bool first     = true;
  for (const auto &inp_edge : curr_node.inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    switch (op) {
      case Sum_Op:
        // PID 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y. Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
        if (spin.get_pid() == 0 || spin.get_pid() == 1) {  // NOTE: I think this should be spin, rethink over this later.
          imp.max += dpin.get_bitwidth().i.max;
          imp.min += dpin.get_bitwidth().i.min;
          imp.dump();
          fmt::print("\n");
        } else {
          imp.min -= dpin.get_bitwidth().i.max;
          imp.max -= dpin.get_bitwidth().i.min;
          imp.dump();
          fmt::print("\n");
          // fmt::print("\tsub: imp.max = {}, imp.min = {}\n", imp.max, imp.min);
        }
        break;
      case Mult_Op:
        if (first) {
          imp.min = dpin.get_bitwidth().i.min;
          imp.max = dpin.get_bitwidth().i.max;
          first   = false;
        } else {
          imp.min *= dpin.get_bitwidth().i.min;
          imp.max *= dpin.get_bitwidth().i.max;
        }
        break;
      case Div_Op:
        // FIXME: I assume there's only one A (AS or AU) divided by one B (BS or BU). Could there be multiple As or Bs?
        // NOTE: In Verilator, if I divide x by 0, the output is 0. We use that assumption here.
        if (first) {
          imp.min = dpin.get_bitwidth().i.min;
          imp.max = dpin.get_bitwidth().i.max;
          first   = false;
        } else {
          if (dpin.get_bitwidth().i.max == 0) {
            imp.min = 0;
          } else {
            imp.min /= dpin.get_bitwidth().i.max;
          }
          fmt::print("TRY: {}\n", (int)dpin.get_bitwidth().i.min);
          if (dpin.get_bitwidth().i.min == 0) {
            imp.max = 0;
          } else {
            imp.min /= dpin.get_bitwidth().i.min;
          }
          // imp.max /= (dpin.get_bitwidth().i.min == 0) ? 0 : dpin.get_bitwidth().i.min;//FIXME: This might need to undergo
          // refinement.
        }
        break;
      case Mod_Op:
        // FIXME: Current thought process is that <A_min, A_max> % <B_min, B_max> can be <0, B_max - 1>.
        if (first) {
          first = false;
        } else {
          imp.min = 0;
          imp.max = dpin.get_bitwidth().i.max - 1;
        }
        break;
      default: fmt::print("Error: arith op not understood\n");
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp);
  if (updated) {
    fmt::print("\tUpdate: ");
    mark_all_outputs(lg, pin);
  }
}

void Pass_bitwidth::iterate_shift(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  Ann_bitwidth::Implicit_range imp;

  bool updated = false;
  int64_t pos = 0;

  int64_t shift_by_min, shift_by_max;
  char s_extend_pin_min, s_extend_pin_max;
          //double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));

  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    if(pos == 0) { // "A" pin
      imp.min = dpin.get_bitwidth().i.min;
      imp.max = dpin.get_bitwidth().i.max;
      pos++;
    } else if(pos == 1) { // "B" pin
      shift_by_min = dpin.get_bitwidth().i.min;
      shift_by_max = dpin.get_bitwidth().i.max;
      pos++;
    } else if(pos == 2) { // "S" pin, if applicable
      s_extend_pin_min = dpin.get_bitwidth().i.min;
      s_extend_pin_max = dpin.get_bitwidth().i.max;
      pos++;
    } else {
      //FIXME: This should never occur, I think?
      fmt::print("\tToo many pins connected to shift_op node.\n");
    }
  }

  //FIXME: The logic for shifts is pretty tough. Reconsider if this is right.
  //  The reason it's tough is because of how shifts can drop bits in explicit.
  //  So 4'b0110 << 2 has a new max of 4'b1000 (8) not 'b011000 (24).
  //SOLUTION?: Maybe I should just do it so if explicit is set, don't touch.
  //  As of now, I only do it as if everything is implicit.
  switch (op) {
    case LogicShiftRight_Op:
      //LogicShiftRight: A >> B
      imp.min = imp.min >> shift_by_max;
      imp.max = imp.max >> shift_by_min;
      break;
    case ShiftLeft_Op:
      //ShiftLeft: A << B
      //FIXME: Edge case exists where the below operation creates a # > 2^64.
      imp.min = imp.min << shift_by_min;
      imp.max = imp.max << shift_by_max;
      break;
    case ShiftRight_Op:
      //ShiftRight: A >> B [S == 1 sign extend, S == 2 B is signed]
    case DynamicShiftLeft_Op:
      //DynamicShiftLeft: Y = A[$signed(B) -: bit_width(A)]
    case DynamicShiftRight_Op:
      //DynamicShiftRight: Y = A[$signed(B) +: bit_width(A)]
    case ArithShiftRight_Op:
      //ArithShiftRight: $signed(A) >>> B
      fmt::print("This specific shift node type not yet supported.\n");
      break;
    default:
      fmt::print("Unknown comparison operator used.\n");
  }
}

void Pass_bitwidth::iterate_comparison(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  // NOTE: The system will take care of constant values such as (a >= 4'b0) or (4'b1111 > 4'b0111)
  //      behind the scenes. FIXME: This was an old note. Is it still true?

  bool                         updated   = false;
  bool                         set       = false;
  bool                         terminate = false;
  Ann_bitwidth::Implicit_range imp;
  imp.min = 1;
  imp.max = 1;

  // Due to nature of comparison type nodes, I need to iterate over every BS|BU pin
  //  for each AS|AU pin I iterate over. (i.e. I have to check A1 > B1, A2 > B1, ..., A1 > B2, ...
  for (const auto &inp_edge : pin.get_node().inp_edges()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    if (spin.get_pid() == 0 || spin.get_pid() == 1) {  // If AS or AU pins
      if(dpin.get_bitwidth().i.overflow | terminate) {
        //If overflow is involved, impossible to calculate.
        fmt::print("\tOverflow involved\n");
        imp.min = 0;
        imp.max = 1;
        set = true;
        break;
      }

      for (const auto &comp_edge : pin.get_node().inp_edges()) {
        auto comp_dpin = comp_edge.driver;
        auto comp_spin = comp_edge.sink;
        if (comp_spin.get_pid() == 2 || comp_spin.get_pid() == 3) {  // If BS or BU pins
          set = true;
          if(comp_dpin.get_bitwidth().i.overflow) {
            //B pin is set to overflow mode. Impossible to calculate bw now.
            terminate = true;
            break;
          }

          switch (op) {
            // NOTE: At some point, go back over this logic and make sure it's right.
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
            default: fmt::print("Error: comparison op not understood\n");
          }
        }
      }
    }
  }

  if (set) {
    updated = pin.ref_bitwidth()->i.update(imp);
  }

  if (updated) {
    fmt::print("\tUpdate: ");
    mark_all_outputs(lg, pin);
  }
}

void Pass_bitwidth::iterate_join(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  fmt::print("\titerate_join:\n");

  Ann_bitwidth::Implicit_range imp;
  int64_t total_bits_min = 0;
  int64_t total_bits_max = 0;
  bool ovfl = false;
  bool first = true;
  bool updated = false;

  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
    //FIXME: This will work for all non-negative numbers, I think.
    //  Figure out how to get working for negatives.
    //FIXME: My understanding is input 1 is A, input 2 is B, etc.,
    //  the final output Y should be [...,C,B,A]
    auto dpin = inp_edge.driver;
    if(first) {
      if(dpin.get_bitwidth().i.overflow) {//input in ovfl mode
        fmt::print("\t1\n");
        ovfl = true;
        imp.min = 0;//FIXME: Maybe set this to 1 bit? If so, need to also set t_b_min to 1
        imp.max = dpin.get_bitwidth().i.max;//In this case, dpin max is max # bits.
        total_bits_max = dpin.get_bitwidth().i.max;
      } else {
        fmt::print("\t2\n");
        imp.min = dpin.get_bitwidth().i.min;
        imp.max = dpin.get_bitwidth().i.max;
        total_bits_max = ceil(log2(dpin.get_bitwidth().i.max + 1));
      }
      first = false;
    } else if(dpin.get_bitwidth().i.overflow) {
      if(ovfl) {
        //Already in overflow mode, so no conversions necessary.
        fmt::print("\t6\n");
        imp.max += dpin.get_bitwidth().i.max;
      } else {
        //Convert current range to overflow mode.
        fmt::print("\t7\n");
        ovfl = true;
        imp.max = ceil(log2(imp.max + 1));
        imp.max += dpin.get_bitwidth().i.max;
      }
    } else {
      double bits_max = ceil(log2(dpin.get_bitwidth().i.max + 1));
      double bits_min = ceil(log2(dpin.get_bitwidth().i.min + 1));

      if(ovfl) {
        fmt::print("\t3\n");
        //Already in overflow mode.
        imp.max += bits_max;
      } else if ((bits_max + total_bits_max) > 63) {
        fmt::print("\t4\n");
        //Enter overflow mode.
        ovfl = true;
        imp.max = bits_max + total_bits_max;
        imp.min = 0;//FIXME: Like FIXME above, maybe set this to 1 instead?
      } else {
        fmt::print("\t5 -- {} {}\n", dpin.get_bitwidth().i.max, dpin.get_bitwidth().i.min);
        //Not in overflow mode.
        imp.max += dpin.get_bitwidth().i.max << total_bits_max;
        total_bits_max += bits_max;
        imp.min += dpin.get_bitwidth().i.min << total_bits_min;
        total_bits_min += bits_min;
      }
    }
    fmt::print("\t{} {} {}\n", (int64_t)imp.min, (int64_t)imp.max, total_bits_max);
  }
  updated = pin.ref_bitwidth()->i.update(imp);

  if (updated) {
    fmt::print("\tUpdate\n");
    mark_all_outputs(lg, pin);
  }
}

void Pass_bitwidth::iterate_pick(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  //NOTE: Pick is used to choose a certain number of bits like A[3:1]
  //Y = A[i,j]... pid 0 = A, pid 1 = offset... j = offset, i = offset + y-bw
  // FIXME: Still in progress.
  fmt::print("\titerate_pick:\n");
  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
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

void Pass_bitwidth::iterate_equals(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  // FIXME: Is my understanding correct? Equals_Op is for comparison, not assigns? Read note below, this works for 2 inputs not
  // more. NOTE: When using Verilog, "assign d = a == b == c" does (a == b) == c... thus you compare the result
  //  of a == b (0 or 1) to c, not if all three (a, b, c) are equal.
  bool                         updated = false;
  bool                         first   = true;
  int64_t                      check_min;
  int64_t                      check_max;
  Ann_bitwidth::Implicit_range imp;
  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    if (first) {
      check_min = dpin.get_bitwidth().i.min;
      check_max = dpin.get_bitwidth().i.max;
      first     = false;
      fmt::print("\t{} {}\n", check_min, check_max);
    } else {
      fmt::print("\t{} {}\n", dpin.get_bitwidth().i.min, dpin.get_bitwidth().i.max);
      if ((check_min == dpin.get_bitwidth().i.min) & (check_max == dpin.get_bitwidth().i.max) & (check_min == check_max)) {
        // Case: max_A == max_B == min_A == min_B... A always == B
        fmt::print("\tAlways 1\n");
        imp.min = 1;
        imp.max = 1;
      } else if ((check_max < dpin.get_bitwidth().i.min) | (check_min > dpin.get_bitwidth().i.max)) {
        // Case: A always > B... or A always < B... thus A never == B
        fmt::print("\tAlways 0\n");
        imp.min = 0;
        imp.max = 0;
      } else {
        // Case: Result of comparison could be either 0 or 1
        fmt::print("\tIndeterminable\n");
        imp.min = 0;
        imp.max = 1;
      }
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp);

  if (updated) {
    fmt::print("\tUpdate\n");
    mark_all_outputs(lg, pin);
  }
}


void Pass_bitwidth::iterate_mux(const LGraph *lg, Node_pin &pin, Node_Type_Op op) {
  Ann_bitwidth::Implicit_range imp;

  bool updated = false;
  bool first = true;
  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    if(spin.get_pid() != 0) { // Base bw off pins except "S" pin
      fmt::print("\tNot S pin bw: {} {}\n", dpin.get_bitwidth().i.min, dpin.get_bitwidth().i.max);
      if(first) {
        imp.min = dpin.get_bitwidth().i.min;
        imp.max = dpin.get_bitwidth().i.max;
        imp.overflow = dpin.get_bitwidth().i.overflow;
        first = false;
      } else if(dpin.get_bitwidth().i.overflow) {
        if(imp.overflow) {
          //Compare which overflow is larger (requires more bits).
          imp.max = (imp.max < dpin.get_bitwidth().i.max) ? dpin.get_bitwidth().i.max : imp.max;
        } else {
          //Output bw isn't yet in overflow mode so this has to be biggest.
          imp.min = 0;
          imp.max = dpin.get_bitwidth().i.max;
          imp.overflow = true;
        }
      } else if (!imp.overflow) {
        //If neither are in overflow mode, widen BW range to contain both ranges.
        imp.min = (imp.min > dpin.get_bitwidth().i.min) ? dpin.get_bitwidth().i.min : imp.min;
        imp.max = (imp.max < dpin.get_bitwidth().i.max) ? dpin.get_bitwidth().i.max : imp.max;
      }
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp);
  if (updated) {
    fmt::print("\tUpdate: ");
    mark_all_outputs(lg, pin);
  }
}

/*void Pass_bitwidth::iterate_subgraph(const LGraph *lg, Index_ID idx) {

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
  // Instantiate bitwidths for input pins to graph (from input graphio)
  lg->each_graph_input([this, lg](const Node_pin &pin) {
    fmt::print("inp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={}", pin.get_name(), pin.is_sink(), pin.is_driver(),
               pin.get_pid(), pin.has_bitwidth());

    if (pin.get_bits() == 0) {
      fmt::print(" -- implicit\n");
      return;
    } else {
      fmt::print(" -- explicit (bits set)\n");
    }

    // FIXME: At this point in time, I need to setup bitwidths for explicit.
    //    Eventually, Sheng should do this instead (so remove some of the explicit stuff later).
    Node_pin editable_pin = pin;
    // Set explicit.
    editable_pin.ref_bitwidth()->e.set_ubits(pin.get_bits());
    // Set implicit.
    editable_pin.ref_bitwidth()->set_implicit();
    // Print out ranges for debug
    fmt::print("\texp: ");
    pin.get_bitwidth().e.dump();
    fmt::print("\n\timp: ");
    pin.get_bitwidth().i.dump();
    fmt::print("\n");
    pending.push_back(pin);
  });

  fmt::print("\n");

  for (const auto &node : lg->fast()) {
    fmt::print("type: {}\n", node.get_type().op);

    // Iterate over inputs to some node.
    for (const auto &out_edge : node.out_edges()) {
      auto dpin = out_edge.driver;
      auto spin = out_edge.sink;
      fmt::print("name_o:{} {} pid:{} -> name:{} pid:{}\n", dpin.debug_name(), dpin.get_bits(), dpin.get_pid(), spin.debug_name(),
                 spin.get_pid());

      // FIXME: Currently, first iteration will iterate over same driver pins multiple times, in some cases. (If more than 1 edge
      // has pin X as its driver)

      if (dpin.get_bits() == 0) {
        fmt::print(" -- implicit\n");
        // return;
      } else {
        // bool sign = false;
        if (node.get_type().op == U32Const_Op) {
          dpin.ref_bitwidth()->e.set_uconst(
              out_edge.get_bits());  // FIXME: The argument to this function is way wrong, but needed for testing.
          dpin.ref_bitwidth()->set_implicit();
        } else {
          // Set bitwidth for output edge driver.
          // FIXME: Focused only on unsigned, will have to change later.
          // FIXME x2: Should I even be doing this? Should be Sheng, I think.
          dpin.ref_bitwidth()->e.set_ubits(out_edge.get_bits());
          fmt::print("\t");
          dpin.ref_bitwidth()->e.dump();
          fmt::print("\n");
        }
        pending.push_back(dpin);
      }
    }
    fmt::print("{}-----------------------\n\n", pending.size());
  }
}

void Pass_bitwidth::iterate_driver_pin(LGraph *lg, Node_pin &pin) {
  const auto node      = pin.get_node();
  const auto node_type = node.get_type().op;

  switch (node_type) {
    // NOTE TO SELF/FIXME: GraphIO will never happen here, right? Maybe from subgraph nodes?
    case GraphIO_Op:
      fmt::print("GraphIO_Op\n");
      mark_all_outputs(lg, pin);
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
      fmt::print("Arith Op --\n");
      iterate_arith(lg, pin, node_type);
      break;
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op:
      fmt::print("Comparison Op --\n");
      iterate_comparison(lg, pin, node_type);
      break;
    case Equals_Op:
      fmt::print("Equals Op --\n");
      iterate_equals(lg, pin, node_type);
      break;
    case ShiftLeft_Op:
    case ShiftRight_Op:
      fmt::print("Shift Op --\n");
      iterate_shift(lg, pin, node_type);
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
      fmt::print("U32Const_Op\n");
      mark_all_outputs(lg, pin);
      break;
    case Mux_Op:
      fmt::print("Mux_Op\n");
      iterate_mux(lg, pin, node_type);
      break;
    default: fmt::print("Op not yet supported in iterate_driver_pin\n");
  }
}

// FIXME: I basically removed everything from this, should get it
// back to some working condition later for debugging.
void Pass_bitwidth::bw_pass_dump(LGraph *lg) {
  lg->each_graph_input([this, lg](const Node_pin &pin) {
    fmt::print("inp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={} ", pin.get_name(), pin.is_sink(), pin.is_driver(),
               pin.get_pid(), pin.has_bitwidth());
    pin.get_bitwidth().e.dump();
    fmt::print("\n");
  });

  bool once = false;
  lg->each_graph_output([this, lg, &once](const Node_pin &pin) {
    /*if(once == false) {
      auto graph_out_node = pin.get_node();
      for (const auto &inp_edge : graph_out_node.inp_edges()) {
        fmt::print("iter! {} ", graph_out_node.get_type().op == GraphIO_Op);
        auto dpin = inp_edge.driver;
        auto spin = inp_edge.sink;
        fmt::print("outp_name={},\n", dpin.get_name());
      }
      once = true;
    }*/

    fmt::print("outp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={}\n", pin.get_name(), pin.is_sink(), pin.is_driver(),
               pin.get_pid(), pin.has_bitwidth());
  });

  fmt::print("\n");
}

bool Pass_bitwidth::bw_pass_iterate(LGraph *lg) {
  if (pending.empty()) {
    fmt::print("\nbw_pass_iterate pass -- no driver pins to iterate over\n");
  }

  int iterations = 0;

  do {
    assert(next_pending.empty());
    fmt::print("Iteration {}:\n", iterations);

    auto &dpin = pending.back();
    pending.pop_back();
    dpin.ref_bitwidth()->niters++;

    if (dpin.ref_bitwidth()->niters > max_iterations) {
      // FIXME: With using the current std::vector, this might fire off much earlier
      //  than I'd want it to (if pin got added mult times due to mult out edges).
      fmt::print("\nbw_pass_iterate abort {}\n", iterations);
      return false;
    }

    do {
      iterate_driver_pin(lg, dpin);
      if (pending.empty()) break;
      dpin = pending.back();
      pending.pop_back();
    } while (true);

    assert(pending.empty());
    if (next_pending.empty()) {
      fmt::print("\nbw_pass_iterate pass {}\n", iterations);
      return true;
    }

    pending = next_pending;
    next_pending.clear();

    iterations++;
  } while (true);

  assert(false);

  return false;
}

void Pass_bitwidth::do_trans(LGraph *lg) {
  {
    Lbench b("pass.bitwidth");

    bw_pass_setup(lg);

    bool done = bw_pass_iterate(lg);
    if (!done) {
      error("could not converge in the iterations FIXME: dump nice message on why\n");
    }
  }

  bw_pass_dump(lg);
}
