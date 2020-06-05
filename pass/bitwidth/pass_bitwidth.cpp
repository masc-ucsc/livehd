//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <algorithm>
#include <vector>

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_bitwidth.hpp"

void setup_pass_bitwidth() { Pass_bitwidth::setup(); }

void Pass_bitwidth::setup() {
  Eprp_method m1("pass.bitwidth", "MIT algorithm for bitwidth optimization", &Pass_bitwidth::trans);

  m1.add_label_optional("max_iterations", "maximum number of iterations to try", "10");

  register_pass(m1);
}

Pass_bitwidth::Pass_bitwidth(const Eprp_var &var) : Pass("pass.bitwidth", var) {
  auto miters = var.get("max_iterations");

  bool ok = absl::SimpleAtoi(miters, &max_iterations);
  if (max_iterations == 0 || !ok) {
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

void Pass_bitwidth::do_trans(LGraph *lg) {
  {
    Lbench b("pass.bitwidth");

    bw_pass_setup(lg);

    bool done = bw_pass_iterate();
    if (!done) {
      error("could not converge in the iterations FIXME: dump nice message on why\n");
    }
  }

  /* bw_pass_dump(lg); */
  bw_implicit_range_to_bits(lg);
  bw_settle_graph_outputs(lg);
}

//------------------------------------------------------------------
// MIT Algorithm
void Pass_bitwidth::bw_pass_setup(LGraph *lg) {
  fmt::print("Phase-I: bitwidth pass setup\n");
  lg->each_graph_input([this](const Node_pin &dpin) {
    // FIXME->sh: should we force all input bitwidth set explicitly?
    // FIXME->sh: it's not necessarily true for sub-graph
    I(dpin.has_bitwidth());
    auto editable_pin = dpin;
    editable_pin.ref_bitwidth()->set_implicit();
    pending.push_back(dpin);
  });

  fmt::print("\n");

  for (const auto &node : lg->fast()) {
    // Iterate over inputs to some node.
    for (const auto &out_edge : node.out_edges()) {
      auto dpin = out_edge.driver;

      // currently, first iteration will iterate over same driver pins multiple times,
      // in some cases. (If more than 1 edge has pin X as its driver)
      // FIXME->sh: why?
      if (dpin.has_bitwidth()) {
        /* if(dpin.get_node().get_type().op == SFlop_Op) { */
        /*   dpin.ref_bitwidth()->e.dump(); */
        /*   I(false); */
        /* } */
        dpin.ref_bitwidth()->set_implicit();
        pending.push_back(dpin);
      } else { // if don't has bitwidth initially, set bits 0 to avoid unnecessary trouble that bitwidth attribute table undefined for some dpin
        dpin.ref_bitwidth()->e.set_ubits(0);
        dpin.ref_bitwidth()->set_implicit();
        ;
      }
      // FIXME->sh: will lead to unset imp insert to pending vector duplicately, why am I doing this?
      // else {
      //   initial_imp_unset.push_back(dpin);
      // }
    }
  }
}


bool Pass_bitwidth::bw_pass_iterate() {
  fmt::print("Phase-II: MIT algorithm iteration start\n");
  if (pending.empty())
    fmt::print("bw_pass_iterate pass -- no driver pins to iterate over\n");

  max_iterations = 10; //FIXME->sh: temporarily solution before := dp_assign supported
  int iterations = 0;
  do {
    I(next_pending.empty());
    fmt::print("\nIteration:{}\n", iterations);

    auto dpin = pending.front();
    pending.pop_front();
    dpin.ref_bitwidth()->niters++;

    //note: with using the current std::vector, this might fire off much earlier than
    //      I'd want it to (if pin got added multiple times due to multiple out edges).
    if (dpin.ref_bitwidth()->niters > max_iterations) {
      fmt::print("bw_pass_iterate abort:{}\n", iterations);
      /* return false; */
      return true;
    }

    do {
      iterate_driver_pin(dpin);
      if (pending.empty())
        break;
      dpin = pending.front();
      pending.pop_front();
    } while (true);
    fmt::print("Iteration:{}, all dpin in pending vector visited!\n", iterations);

    assert(pending.empty());
    if (next_pending.empty()) {
      fmt::print("bw_pass_iterate pass:{}\n", iterations);
      return true;
    }


    pending = std::move(next_pending);
    next_pending.clear(); // need to clear the moved container or it will be in a "valid, but undefined state"

    iterations++;
  } while (true);
}


void Pass_bitwidth::mark_all_outputs(Node_pin &dpin) {
  // Mark driver pins that need to change based off current driver pin.
  Node cur_node = dpin.get_node();

  /* dpin.get_bitwidth().i.dump(); */
  for (const auto &out_edge : cur_node.out_edges()) {
    auto spin          = out_edge.sink;
    auto affected_node = spin.get_node();
    for (const auto &aff_out_edge : affected_node.out_edges()) {
      if (std::find(next_pending.begin(), next_pending.end(), aff_out_edge.driver) == next_pending.end()) {
        next_pending.push_back(aff_out_edge.driver);
      }
    }
  }
}


void Pass_bitwidth::iterate_logic(Node_pin &pin) {
  bool updated;
  bool first = true;
  auto op = pin.get_node().get_type().op;

  // FIXME: Currently treating everything as unsigned
  Ann_bitwidth::Implicit_range imp;

  for (const auto &inp_edge : pin.get_node().inp_edges_ordered()) {
    switch (op) {
      case And_Op:
        // Make bw = <0, max(inputs)> since & op can never exceed largest input value.
        if (first) {
          imp.min = 0;
          imp.max = inp_edge.driver.get_bitwidth().i.max;
          first   = false;
        } else if (inp_edge.driver.get_bitwidth().i.max < imp.max) {
          imp.max = inp_edge.driver.get_bitwidth().i.max;
        }
        break;
      case Or_Op:
        // Make bw = <min(inputs), 2^n - 1> where n is the largest bitwidth of inputs to this node.
        I(pin.get_node().get_type().op == Or_Op);
        if (first) {
          imp.min     = inp_edge.driver.get_bitwidth().i.min;
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
          imp.max     = pow(2, bits) - 1;
          first       = false;
        } else {
          if (inp_edge.driver.get_bitwidth().i.min > imp.min)
            imp.min = inp_edge.driver.get_bitwidth().i.min;

          if (inp_edge.driver.get_bitwidth().i.max > imp.max) {
            double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
            imp.max     = pow(2, bits) - 1;
          }
        }
        break;
      case Xor_Op:
        // Make bw = <0, 2^n - 1> where n is the largest bitwidth of inputs to this node.
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
        {
        I(pin.get_node().get_type().op == Not_Op);
        auto bits_max = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
        auto ori_min    = inp_edge.driver.get_bitwidth().i.min;
        auto ori_max    = inp_edge.driver.get_bitwidth().i.max;
        imp.min         = ~ori_max & (int64_t)(pow(2, bits_max) -1);
        imp.max         = ~ori_min & (int64_t)(pow(2, bits_max) -1);
        }
        break;
      default: fmt::print("Error: logic op not understood\n");
    }
  }

  updated = pin.ref_bitwidth()->i.update(imp); //FIXME->sh: After the reduced_or, the imp is not changed????
  if (updated) {
    mark_all_outputs(pin);
  }
}

void Pass_bitwidth::iterate_arith(Node_pin &pin) {
  bool updated = false;
  auto op = pin.get_node().get_type().op;
  // From this driver pin's node, look at inp edges and figure out bw info from those.
  Ann_bitwidth::Implicit_range imp;

  auto curr_node = pin.get_node();
  bool first     = true;
  for (const auto &inp_edge : curr_node.inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    switch (op) {
      //if (spin.get_pid() == 0 || spin.get_pid() == 1) {  // NOTE: I think this should be spin, rethink over this later.
      case Sum_Op:  // PID 0 = AS, 1 = AU, 2 = BS, 3 = BU, 4 = Y. Y = (AS+...+AS+AU+...+AU) - (BS+...+BS+BU+...+BU)
        if (imp.overflow) {
          if (dpin.get_bitwidth().i.overflow) {
            //Case 1: Both are in overflow.
            //  Choose larger bw and add 1 to size.
            imp.max = (imp.max > dpin.get_bitwidth().i.max) ? imp.max + 1 : dpin.get_bitwidth().i.max + 1;
          } else {
            //Case 2: Current is in overflow but new isn't. Convert new to bit count.
            //  We only increase max # bits by 1 since current can only grow by 1 bit if new < current.
            imp.max += 1;
          }
        } else if (dpin.get_bitwidth().i.overflow) {
          //Case 3: Current isn't in ovfl, but new is.
          imp.max = first ? dpin.get_bitwidth().i.max : dpin.get_bitwidth().i.max + 1;
          imp.min = 0;
          imp.overflow = true;
        } else {
          double bitsC = ceil(log2(imp.max + 1));
          double bitsN = ceil(log2(dpin.get_bitwidth().i.max + 1));
          if ((bitsC == 63) | (bitsN == 63)) {
            //Case 4: Neither is in ovfl mode, but adding them leads to overflow.
            imp.max = 64;
            imp.min = 0;
            imp.overflow = true;
          } else {
            //Case 5: Neither is in ovfl mode, adding/sub'ing them together does not lead to overflow.
            if (spin.get_pid() == 0 || spin.get_pid() == 1) {
              imp.max += dpin.get_bitwidth().i.max;
              imp.min += dpin.get_bitwidth().i.min;
            } else {
              imp.min -= dpin.get_bitwidth().i.max;
              imp.max -= dpin.get_bitwidth().i.min;
            }
          }
        }
        first = false;
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
    mark_all_outputs(pin);
  }
}

void Pass_bitwidth::iterate_shift(Node_pin &dpin) {
  Ann_bitwidth::Implicit_range imp;
  auto op = dpin.get_node().get_type().op;
  auto node = dpin.get_node();

  bool updated = false;
  int64_t pos = 0;

  int64_t shift_by_min, shift_by_max;
  //char s_extend_pin_min, s_extend_pin_max;

  //double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));

  for (const auto &inp_edge : node.inp_edges_ordered()) {
    auto edge_dpin = inp_edge.driver;
    if(pos == 0) { // "A" pin
      imp.min = edge_dpin.get_bitwidth().i.min;
      imp.max = edge_dpin.get_bitwidth().i.max;
      /* pos++; */
    } else if(pos == 1) { // "B" pin
      shift_by_min = edge_dpin.get_bitwidth().i.min;
      shift_by_max = edge_dpin.get_bitwidth().i.max;
      /* pos++; */
    } else if(pos == 2) { // "S" pin, if applicable
      // s_extend_pin_min = dpin.get_bitwidth().i.min;
      // s_extend_pin_max = dpin.get_bitwidth().i.max;
      /* pos++; */
    } else {
      //FIXME: This should never occur, I think?
      fmt::print("\tToo many pins connected to shift_op node.\n");
      I(false);
    }
    pos++;
  }

  //FIXME: The logic for shifts is pretty tough. Reconsider if this is right.
  //  The reason it's tough is because of how shifts can drop bits in explicit.
  //  So 4'b0110 << 2 has a new max of 4'b1000 (8) not 'b011000 (24).
  //  SOLUTION?: Maybe I should just do it so if explicit is set, don't touch.
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
    case ShiftRight_Op: {
      //ShiftRight: A >> B [S == 1 sign extend, S == 2 B is signed]
      bool handled = false;
      for (const auto &e : node.inp_edges()) {
        if (e.sink.get_pid() == 2) {
          handled = true;
          if (e.driver.get_node().get_type().op != U32Const_Op) I(false, "Error: Shift sign is not a constant.\n");

          auto val = e.driver.get_node().get_type_const_value();
          if (val % 2 == 1) {
            ;//FIXME->sh: todo: handle arith shift right when S == 1
          } else if (val == 2) {
            ;//FIXME->sh: todo: handle signed number shift when S == 2
          } else {
            ;
          }
        }  
      }

      //Logical ShiftRight: A >> B, also drop the shifted MSB
      if (!handled) {
        imp.min = imp.min >> shift_by_max;
        imp.max = imp.max >> shift_by_min;
      }

      break;
    }
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
  updated = dpin.ref_bitwidth()->i.update(imp);

  if (updated) {
    mark_all_outputs(dpin);
  }
}

void Pass_bitwidth::iterate_comparison(Node_pin &dpin) {
  // FIXME->sh: the comparison op only has boolean output and only connect to mux selection pin?

  Ann_bitwidth::Implicit_range imp;
  imp.min = 1;
  imp.max = 1;
  dpin.ref_bitwidth()->i.update(imp);
}

void Pass_bitwidth::iterate_join(Node_pin &dpin) {

  Ann_bitwidth::Implicit_range imp;
  int64_t total_bits_min = 0;
  int64_t total_bits_max = 0;
  auto op = dpin.get_node().get_type().op;
  bool ovfl = false;
  bool first = true;
  bool updated = false;

  for (const auto &inp_edge : dpin.get_node().inp_edges_ordered()) {
    //FIXME->hunter: This will work for all non-negative numbers, I think. Figure out how to get working for negatives.
    //FIXME->hunter: My understanding is input 1 is A, input 2 is B, etc., the final output Y should be [...,C,B,A]
    auto dpin = inp_edge.driver;
    if(first) {
      if(dpin.get_bitwidth().i.overflow) {//input in ovfl mode
        ovfl = true;
        imp.min = 0;//FIXME->hunter: Maybe set this to 1 bit? If so, need to also set t_b_min to 1
        imp.max = dpin.get_bitwidth().i.max;//In this case, dpin max is max # bits.
        total_bits_max = dpin.get_bitwidth().i.max;
      } else {
        imp.min = dpin.get_bitwidth().i.min;
        imp.max = dpin.get_bitwidth().i.max;
        total_bits_max = ceil(log2(dpin.get_bitwidth().i.max + 1));
      }
      first = false;
    } else if(dpin.get_bitwidth().i.overflow) {
      if(ovfl) {
        //Already in overflow mode, so no conversions necessary.
        imp.max += dpin.get_bitwidth().i.max;
      } else {
        //Convert current range to overflow mode.
        ovfl = true;
        imp.max = ceil(log2(imp.max + 1));
        imp.max += dpin.get_bitwidth().i.max;
      }
    } else {
      double bits_max = ceil(log2(dpin.get_bitwidth().i.max + 1));
      double bits_min = ceil(log2(dpin.get_bitwidth().i.min + 1));

      if(ovfl) {
        //Already in overflow mode.
        imp.max += bits_max;
      } else if ((bits_max + total_bits_max) > 63) {
        //Enter overflow mode.
        ovfl = true;
        imp.max = bits_max + total_bits_max;
        imp.min = 0;//FIXME: Like FIXME above, maybe set this to 1 instead?
      } else {
        //Not in overflow mode.
        imp.max += dpin.get_bitwidth().i.max << total_bits_max;
        total_bits_max += bits_max;
        imp.min += dpin.get_bitwidth().i.min << total_bits_min;
        total_bits_min += bits_min;
      }
    }
  }
  updated = dpin.ref_bitwidth()->i.update(imp);

  if (updated) {
    mark_all_outputs(dpin);
  }
}

void Pass_bitwidth::iterate_pick(Node_pin &node_dpin) {
  //NOTE: Pick is used to choose a certain number of bits like A[3:1]
  //Y = A[i,j]... pid 0 = A, pid 1 = offset... j = offset, i = offset + y-bw
  // FIXME: Still in progress.
  auto op = node_dpin.get_node().get_type().op;
  for (const auto &inp_edge : node_dpin.get_node().inp_edges_ordered()) {
    auto spin = inp_edge.sink;
  }
}

void Pass_bitwidth::iterate_equals(Node_pin &node_dpin) {
  // FIXME: Is my understanding correct? Equals_Op is for comparison, not assigns? Read note below, this works for 2 inputs not
  // more. NOTE: When using Verilog, "assign d = a == b == c" does (a == b) == c... thus you compare the result
  //  of a == b (0 or 1) to c, not if all three (a, b, c) are equal.
  bool                         updated = false;
  bool                         first   = true;
  int64_t                      check_min;
  int64_t                      check_max;
  Ann_bitwidth::Implicit_range imp;
  auto op = node_dpin.get_node().get_type().op;
  for (const auto &inp_edge : node_dpin.get_node().inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    if (first) {
      check_min = dpin.get_bitwidth().i.min;
      check_max = dpin.get_bitwidth().i.max;
      first     = false;
    } else {
      if ((check_min == dpin.get_bitwidth().i.min) & (check_max == dpin.get_bitwidth().i.max) & (check_min == check_max)) {
        // Case: max_A == max_B == min_A == min_B... A always == B
        imp.min = 1;
        imp.max = 1;
      } else if ((check_max < dpin.get_bitwidth().i.min) | (check_min > dpin.get_bitwidth().i.max)) {
        // Case: A always > B... or A always < B... thus A never == B
        imp.min = 0;
        imp.max = 0;
      } else {
        // Case: Result of comparison could be either 0 or 1
        imp.min = 0;
        imp.max = 1;
      }
    }
  }

  updated = node_dpin.ref_bitwidth()->i.update(imp);

  if (updated) {
    mark_all_outputs(node_dpin);
  }
}

void Pass_bitwidth::iterate_flop(Node_pin &node_dpin) {
  I(node_dpin.get_node().get_type().op == SFlop_Op);
  fmt::print("Flop qpin ann_bits_dbg\n");
  node_dpin.ref_bitwidth()->e.dump();
  fmt::print("Flop qpin name:{}\n",    node_dpin.get_name());
  Ann_bitwidth::Implicit_range imp;
  bool updated = false;
  auto flop = node_dpin.get_node();
  auto flop_din_spin = flop.get_sink_pin("D");

  I(flop_din_spin.inp_edges().size() == 1);
  imp.max = flop_din_spin.inp_edges().begin()->driver.get_bitwidth().i.max;
  imp.min = flop_din_spin.inp_edges().begin()->driver.get_bitwidth().i.min;

  updated = node_dpin.ref_bitwidth()->i.update(imp);
  if (updated) {
    mark_all_outputs(node_dpin);
  }
}


void Pass_bitwidth::iterate_mux(Node_pin &node_dpin) {
  Ann_bitwidth::Implicit_range imp;

  auto updated = false;
  bool is_first_edge = true;
  Node_pin first_edge_dpin; // assuming always 2-to-1 mux in lgraph

  for (const auto &inp_edge : node_dpin.get_node().inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    if (spin.get_pid() == 0) {
      // Base bw off pins except "S" dpin
      continue;
    } else if (is_first_edge) {
      imp.min = dpin.get_bitwidth().i.min;
      imp.max = dpin.get_bitwidth().i.max;
      imp.overflow = dpin.get_bitwidth().i.overflow;
      is_first_edge = false;
      first_edge_dpin = dpin;
    } else if (dpin.get_bitwidth().i.overflow) {
      if (imp.overflow) {
        // Compare which overflow is larger (requires more bits).
        imp.max = (imp.max < dpin.get_bitwidth().i.max) ? dpin.get_bitwidth().i.max : imp.max;
      } else {
        // Output bw isn't yet in overflow mode so this has to be biggest.
        imp.min = 0;
        imp.max = dpin.get_bitwidth().i.max;
        imp.overflow = true;
      }
    } else if (!imp.overflow) {
      // If neither are in overflow mode, widen BW range to contain both ranges.
      imp.min = (imp.min > dpin.get_bitwidth().i.min) ? dpin.get_bitwidth().i.min : imp.min;
      imp.max = (imp.max < dpin.get_bitwidth().i.max) ? dpin.get_bitwidth().i.max : imp.max;
    }
  }

  updated = first_edge_dpin.ref_bitwidth()->i.update(imp);
  if (updated) {
    mark_all_outputs(first_edge_dpin);
  }


  updated = node_dpin.ref_bitwidth()->i.update(imp);
  if (updated) {
    mark_all_outputs(node_dpin);
  }
}


void Pass_bitwidth::iterate_driver_pin(Node_pin &dpin) {
  const auto node_type = dpin.get_node().get_type().op;

  switch (node_type) {
    //FIXME->Hunter: GraphIO will never happen here, right? Maybe from subgraph nodes?
    case GraphIO_Op:
      mark_all_outputs(dpin);
      break;
    case And_Op:
    case Or_Op:
    case Xor_Op:
    case Not_Op:
      iterate_logic(dpin);
      break;
    case Sum_Op:
    case Mult_Op:
    case Div_Op:
    case Mod_Op:
      iterate_arith(dpin);
      break;
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op:
      iterate_comparison(dpin);
      break;
    case Equals_Op:
      iterate_equals(dpin);
      break;
    case ShiftLeft_Op:
    case ShiftRight_Op:
      iterate_shift(dpin);
      break;
    case Join_Op:
      iterate_join(dpin);
      break;
    case Pick_Op:
      iterate_pick(dpin);
      break;
    case U32Const_Op:
      mark_all_outputs(dpin);
      break;
    case Mux_Op:
      iterate_mux(dpin);
      break;
    case SFlop_Op:
      iterate_flop(dpin);
      break;
    default: fmt::print("Op not yet supported in iterate_driver_pin\n");
  }
}

// back to some working condition later for debugging.
void Pass_bitwidth::bw_pass_dump(LGraph *lg) {
  lg->each_graph_input([](const Node_pin &pin) {
    fmt::print("inp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={}\n", pin.get_name(), pin.is_sink(), pin.is_driver(), pin.get_pid(), pin.has_bitwidth()); pin.get_bitwidth().e.dump();
  });

  lg->each_graph_output([](const Node_pin &pin) {
    fmt::print("outp name={}, sink?={}, driver?={}, pid={}, has_bitwidth={}\n", pin.get_name(), pin.is_sink(), pin.is_driver(), pin.get_pid(), pin.has_bitwidth());
  });

  fmt::print("\n");
}

void Pass_bitwidth::bw_implicit_range_to_bits(LGraph *lg) {
  auto graph_inp_node = lg->get_graph_input_node();
  for (auto &out : graph_inp_node.out_edges()) {
    if (out.driver.has_bitwidth()) {
      uint32_t bits;
      if (out.driver.get_bitwidth().i.max == 1 || out.driver.get_bitwidth().i.max == 0) {
        bits = 1;
      } else {
        bits = ceil(log2(out.driver.get_bitwidth().i.max));
      }
      out.driver.set_bits(bits);
    } else {
      fmt::print("graph input :{} doesn't have bitwidth assignment!\n", out.driver.debug_name());
      I(false); // compile error!
    }
  }

  for (const auto& node: lg->fast()) {
    /* if (node.get_type().op == U32Const_Op) */
    /*   continue; */

    for (auto& out:node.out_edges()) {
      if (out.driver.has_bitwidth()) {
        uint32_t bits;
        if (out.driver.get_bitwidth().i.max == 1 || out.driver.get_bitwidth().i.max == 0) {
          bits = 1;
        } else {
          bits = ceil(log2(out.driver.get_bitwidth().i.max + 1));
        }

        out.driver.set_bits(bits);
      } 
    }
  }

  //FIXME->sh: what about graph outputs? has it handled in lg->fast()?
}

void Pass_bitwidth::bw_settle_graph_outputs(LGraph *lg) {
  for (const auto &inp : lg->get_graph_output_node().inp_edges()) {
    auto bits = inp.driver.get_bits();
    auto spin = inp.sink;
    //note: in graph out node, spin_pid == dpin_pid is always true
    auto graph_output_driver_pin = spin.get_node().setup_driver_pin(spin.get_pid());
    graph_output_driver_pin.set_bits(bits);
  }
}
