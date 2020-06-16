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

    fmt::print("Phase-I: bitwidth pass setup\n");
    bw_pass_setup(lg);

    fmt::print("Phase-II: MIT algorithm iteration start\n");
    bool done = bw_pass_iterate();
    if (!done) {
      error("could not converge in the iterations FIXME: dump nice message on why\n");
    }
  }

  /* bw_pass_dump(lg); */
  fmt::print("Phase-III: Set Driver_pin bits\n");
  bw_implicit_range_to_bits(lg);
  bw_settle_graph_outputs(lg);

  fmt::print("Phase-IV: Replace Dp_assign Or_Op by Pick_Op\n");
  bw_replace_dp_node_by_pick(lg);


  fmt::print("Phase-V: Bits Extension\n");
  bw_bits_extension_by_join(lg);
}

//------------------------------------------------------------------
// MIT Algorithm
void Pass_bitwidth::bw_pass_setup(LGraph *lg) {
  // FIXME->sh: should we force all input bitwidth set explicitly? it's not necessarily true for sub-graph
  lg->each_graph_input([this](const Node_pin &dpin) {
    I(dpin.has_bitwidth());
    auto editable_pin = dpin;
    editable_pin.ref_bitwidth()->set_implicit();
    pending.push_back(dpin);
    mark_descendant_dpins(dpin, true);
  });

  fmt::print("\n");

  Node_pin null_dpin; // for 1st phase of dp_infer_table setup

  for (const auto &node : lg->fast()) {
    for (const auto &out_edge : node.out_edges()) {
      auto dpin = out_edge.driver;

      if (dpin.has_bitwidth()) {
        dpin.ref_bitwidth()->set_implicit();
        pending.push_back(dpin);
        mark_descendant_dpins(dpin, true);

      } else { //set bits 0 to avoid bitwidth attribute undefined issues
        dpin.ref_bitwidth()->e.set_ubits(0);
        dpin.ref_bitwidth()->set_implicit();
      }
    }

    // dp_assign tables initialization step I
    // handle "Or_Op as assign" specially as some of the dpin is not connected as edge but still needed for dp_assign...
    auto editable_node = node;
    /* if (editable_node.get_type().op == Or_Op && editable_node.setup_driver_pin(1).has_name()) { // the only way to check whether the dpin has been set before or not ... */
    if (editable_node.get_type().op == Or_Op && editable_node.inp_edges().size() == 1 && editable_node.setup_driver_pin(0).has_name()) { //or as assign, the only way to check whether the dpin has been set before or not ...
      auto dpin = editable_node.get_driver_pin(0); //or as assign
      pending.push_back(dpin);

      if (dpin.ref_bitwidth()->dp_flag) {
        dp_flagged_dpins.insert(dpin);
      }

      I(editable_node.get_driver_pin(0).has_ssa()); // or as assign
      vname2dpins[dpin.get_prp_vname()].emplace_back(dpin); 
    }
  }

  // dp_assign tables initialization step II
  dp_assign_initialization(lg);
}


// FIXME->sh: O(n^2), if we can somehow store the corresponding dp referred_dpin into BW class, it could be reduced to O(n)
void Pass_bitwidth::dp_assign_initialization(LGraph *lg) {
  // for (const auto &dp_flagged_dpin : dp_flagged_dpins)
  //   fmt::print("dp_flagged_dpin:{}\n", dp_flagged_dpin.debug_name());

  for (const auto &dp_flagged_dpin : dp_flagged_dpins) {

    // auto key_vname = dp_flagged_dpin.get_ssa().get_vname();
    auto key_vname = dp_flagged_dpin.get_prp_vname(); 
    auto key_subs  = dp_flagged_dpin.get_ssa().get_subs();
    fmt::print("key_name:{}\n", key_vname);
    if (key_subs == 0)
      continue;

    I(key_subs > 0); // must have at least one elder_brother, foo_0, to infer from
    const auto &subset_dpins = vname2dpins[key_vname];
    for (const auto &itr : subset_dpins) {
      I(itr.get_prp_vname() == key_vname);
      if (key_subs == (itr.get_ssa().get_subs() + 1) ) {
        dp_followed_by_table[itr] = dp_flagged_dpin;
      }
    }
  }
}


bool Pass_bitwidth::bw_pass_iterate() {
  if (pending.empty())
    fmt::print("bw_pass_iterate pass -- no driver pins to iterate over\n");

  max_iterations = 1; //FIXME->sh: the concept of max_iteration should be deprecated
  int iterations = 0;
  do {
    I(next_pending.empty());
    fmt::print("\nIteration:{}\n", iterations);

    auto dpin = pending.front();
    pending.pop_front();
    dpin.ref_bitwidth()->niters++;

    do {
      iterate_driver_pin(dpin);
      if (pending.empty())
        break;
      dpin = pending.front();
      pending.pop_front();
    } while (true);
    fmt::print("Iteration:{}, all dpin in pending vector visited!\n", iterations);

    I(pending.empty());
    if (next_pending.empty()) {
      fmt::print("bw_pass_iterate pass:{}\n", iterations);
      return true;
    }


    pending = std::move(next_pending);
    next_pending.clear(); // need to clear the moved container or it will be in a "valid, but undefined state"

    iterations++;
  } while (true);
}



void Pass_bitwidth::iterate_driver_pin(Node_pin &node_dpin) {
  if (node_dpin.get_bitwidth().fixed)
    return;

  const auto node_type = node_dpin.get_node().get_type().op;

  switch (node_type) {
    case GraphIO_Op:
      //FIXME->Hunter: GraphIO will never happen here, right? Maybe from subgraph nodes?
      mark_descendant_dpins(node_dpin);
      break;
    case And_Op:
    case Or_Op:
    case Xor_Op:
    case Not_Op:
      iterate_logic(node_dpin);
      break;
    case Sum_Op:
    case Mult_Op:
    case Div_Op:
    case Mod_Op:
      iterate_arith(node_dpin);
      break;
    case LessThan_Op:
    case GreaterThan_Op:
    case LessEqualThan_Op:
    case GreaterEqualThan_Op:
      iterate_comparison(node_dpin);
      break;
    case Equals_Op:
      iterate_equals(node_dpin);
      break;
    case ShiftLeft_Op:
    case ShiftRight_Op:
      iterate_shift(node_dpin);
      break;
    case Join_Op:
      iterate_join(node_dpin);
      break;
    case Pick_Op:
      iterate_pick(node_dpin);
      break;
    case Const_Op:
      mark_descendant_dpins(node_dpin);
      break;
    case Mux_Op:
      iterate_mux(node_dpin);
      break;
    case SFlop_Op:
      iterate_flop(node_dpin);
      break;
    default: fmt::print("Op not yet supported in iterate_driver_pin\n");
  }
}


void Pass_bitwidth::mark_descendant_dpins(const Node_pin &dpin, bool ini_setup) {
  // Mark driver pins that need to change based off current driver pin.

  for (const auto &out_edge : dpin.out_edges()) {
    auto spin          = out_edge.sink;
    auto affected_node = spin.get_node();
    if (ini_setup) {
      for (const auto &aff_out_edge : affected_node.out_edges()) {
        if (std::find(pending.begin(), pending.end(), aff_out_edge.driver) == pending.end())
          pending.push_back(aff_out_edge.driver);
      }
    } else {
      for (const auto &aff_out_edge : affected_node.out_edges()) {
        if (aff_out_edge.driver.get_bitwidth().dp_flag) //don't need to update the dpin when its dp_flag is 1
          continue;

        if (std::find(next_pending.begin(), next_pending.end(), aff_out_edge.driver) == next_pending.end())
          next_pending.push_back(aff_out_edge.driver);
      }

      // handle Or_Op as assign specially as some Or_Op might have no fanout, but be inferred by some other dp_node
      if (affected_node.get_type().op == Or_Op && affected_node.inp_edges().size() == 1 && affected_node.setup_driver_pin(0).has_name()  ) { // the only way to check whether the dpin has been set before or not ... // or as assign
        if (!affected_node.get_driver_pin(0).get_bitwidth().dp_flag)  //don't need to update the dpin when its dp_flag is 1 // or as assign
          next_pending.push_back(affected_node.get_driver_pin(0)); // or as assign
      }
    }
  }
}


void Pass_bitwidth::iterate_logic(Node_pin &node_dpin) {
  bool updated;
  bool first = true;
  auto op = node_dpin.get_node().get_type().op;

  // FIXME: Currently treating everything as unsigned
  Ann_bitwidth::Implicit_range imp;

  for (const auto &inp_edge : node_dpin.get_node().inp_edges_ordered()) {
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
      case Or_Op: {
        // Make bw = <min(inputs), 2^n - 1> where n is the largest bitwidth of inputs to this node.
        I(node_dpin.get_node().get_type().op == Or_Op);

        if (node_dpin.ref_bitwidth()->dp_flag) {
          break; //it's a dp_node, the bitwidth info should follow the target Or_Op. The target Or_Op is recorded in dp_followed_by_table
        }


        if (first) {
          imp.min     = inp_edge.driver.get_bitwidth().i.min;
          double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));

          if (node_dpin.get_node().inp_edges().size() == 1) { imp.max = inp_edge.driver.get_bitwidth().i.max; } //or as assign
          else                                              { imp.max = pow(2, bits) - 1;                     }

          first       = false;

          //Pyrope dp_assign stuff:
          //whenever having a dp_assign node follower, pass the bitwidth information to it.
          if (node_dpin.get_pid() == 0 && dp_followed_by_table.find(node_dpin) != dp_followed_by_table.end()) { // or as assign
            auto dp_node_dpin = dp_followed_by_table[node_dpin];
            updated = dp_node_dpin.ref_bitwidth()->i.update(imp);
            if (updated) {
              mark_descendant_dpins(dp_node_dpin);
            }
          }

        } else {
          if (inp_edge.driver.get_bitwidth().i.min > imp.min)
            imp.min = inp_edge.driver.get_bitwidth().i.min;

          if (inp_edge.driver.get_bitwidth().i.max > imp.max) {
            double bits = ceil(log2(inp_edge.driver.get_bitwidth().i.max + 1));
            imp.max     = pow(2, bits) - 1;
          }
        }
        break;

      }
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
        I(node_dpin.get_node().get_type().op == Not_Op);
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

  updated = node_dpin.ref_bitwidth()->i.update(imp); 
  if (updated) {
    mark_descendant_dpins(node_dpin);
  }
}

void Pass_bitwidth::iterate_arith(Node_pin &node_dpin) {
  bool updated = false;
  auto op = node_dpin.get_node().get_type().op;
  // From this driver pin's node, look at inp edges and figure out bw info from those.
  Ann_bitwidth::Implicit_range imp;

  auto curr_node = node_dpin.get_node();
  bool first     = true;
  for (const auto &inp_edge : curr_node.inp_edges_ordered()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    switch (op) {
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

  updated = node_dpin.ref_bitwidth()->i.update(imp);
  if (updated) {
    mark_descendant_dpins(node_dpin);
  }
}

void Pass_bitwidth::iterate_shift(Node_pin &node_dpin) {
  Ann_bitwidth::Implicit_range imp;
  auto op = node_dpin.get_node().get_type().op;
  auto node = node_dpin.get_node();

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
          if (e.driver.get_node().get_type().op != Const_Op) I(false, "Error: Shift sign is not a constant.\n");

          auto val = e.driver.get_node().get_type_const().to_i();
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
  updated = node_dpin.ref_bitwidth()->i.update(imp);

  if (updated) {
    mark_descendant_dpins(node_dpin);
  }
}

void Pass_bitwidth::iterate_comparison(Node_pin &node_dpin) {
  // FIXME->sh: the comparison op only has boolean output and only connect to mux selection pin?

  Ann_bitwidth::Implicit_range imp;
  imp.min = 1;
  imp.max = 1;
  node_dpin.ref_bitwidth()->i.update(imp);
}

void Pass_bitwidth::iterate_join(Node_pin &node_dpin) {

  Ann_bitwidth::Implicit_range imp;
  int64_t total_bits_min = 0;
  int64_t total_bits_max = 0;
  auto op = node_dpin.get_node().get_type().op;
  bool ovfl = false;
  bool first = true;
  bool updated = false;

  for (const auto &inp_edge : node_dpin.get_node().inp_edges_ordered()) {
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
  updated = node_dpin.ref_bitwidth()->i.update(imp);

  if (updated) {
    mark_descendant_dpins(node_dpin);
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
    mark_descendant_dpins(node_dpin);
  }
}

void Pass_bitwidth::iterate_flop(Node_pin &node_dpin) {
  I(node_dpin.get_node().get_type().op == SFlop_Op);
  Ann_bitwidth::Implicit_range imp;
  bool updated = false;
  auto flop = node_dpin.get_node();
  auto flop_din_spin = flop.get_sink_pin("D");

  I(flop_din_spin.inp_edges().size() == 1);
  imp.max = flop_din_spin.inp_edges().begin()->driver.get_bitwidth().i.max;
  imp.min = flop_din_spin.inp_edges().begin()->driver.get_bitwidth().i.min;

  updated = node_dpin.ref_bitwidth()->i.update(imp);
  if (updated) {
    mark_descendant_dpins(node_dpin);
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
      continue; // Base bw off pins except "S" dpin
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
    mark_descendant_dpins(first_edge_dpin);
  }


  updated = node_dpin.ref_bitwidth()->i.update(imp);
  if (updated) {
    mark_descendant_dpins(node_dpin);
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
    /* if (node.get_type().op == Const_Op) */
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


//we need this pass because in Pyrope, some node's bitwidth will be fixed and won't be affected by BW algorithm
//FIXME->sh: I think this case would only possible at "Or_Op as assign", which basically represents every prp variable
//FIXME->sh: when performing bits extension, another complex problem is to extend signed or unsigned?

void Pass_bitwidth::bw_bits_extension_by_join(LGraph *lg) {
  for (const auto & node : lg->fast()) {
    if (node.get_type().op == Or_Op && node.inp_edges().size() == 1 && node.get_driver_pin(0).has_bitwidth()) { // or as assign
      auto inp_edge_bits = node.inp_edges().begin()->driver.get_bits();
      for (const auto & out_edge : node.out_edges()) {
        auto out_edge_bits = out_edge.driver.get_bits();
        if (inp_edge_bits > out_edge_bits) {
          if (node.get_driver_pin(0).get_bitwidth().fixed) I(false, "Compile Error: lhs bits is fixed, rhs bits larger than lhs bits but cannot propagate over it"); // or as assign
          else                                             I(false, "Compile Error: rhs bits larger than lhs bits after bitwidth pass"); //FIXME->sh: merge these two assertions?
        } else if (inp_edge_bits < out_edge_bits) {
          uint16_t offset = out_edge_bits - inp_edge_bits;
          auto join_node = lg->create_node(Join_Op);
          auto zero_ext_dpin = lg->create_node_const(Lconst(0, offset)).setup_driver_pin();
          lg->add_edge(zero_ext_dpin, join_node.setup_sink_pin(1));
          lg->add_edge(node.inp_edges().begin()->driver, join_node.setup_sink_pin(0));
          lg->add_edge(join_node.setup_driver_pin(), node.inp_edges().begin()->sink);
          join_node.get_driver_pin().set_bits(out_edge_bits);


          node.inp_edges().begin()->del_edge();
          break; //only one of the output edge of the Or_Op is enough to insert a Join_Op
        }
      }
    }
  }
}

void Pass_bitwidth::bw_replace_dp_node_by_pick(LGraph *lg) {
  for (const auto & node : lg->fast()) {
    if (node.get_type().op == Or_Op && node.inp_edges().size() == 1 && node.get_driver_pin(0).get_bitwidth().dp_flag) { // or as assign
      auto or_dpin = node.get_driver_pin(0); // or as assign
      auto or_dpin_bits = or_dpin.get_bits();
      auto original_driver = node.inp_edges().begin()->driver;
      auto original_driver_bits = original_driver.get_bits();
      if (or_dpin_bits >= original_driver_bits)  continue; //x := y but x.__bits >= y.__bits


      auto pick_node  = lg->create_node(Pick_Op);
      auto zero_dpin  = lg->create_node_const(Lconst(0,1)).get_driver_pin();
      pick_node.setup_driver_pin().set_bits(or_dpin_bits);
      lg->add_edge(original_driver, pick_node.setup_sink_pin(0));
      lg->add_edge(zero_dpin, pick_node.setup_sink_pin(1));

      for (auto & or_out_edge : node.out_edges()) {
        auto ori_sink   = or_out_edge.sink;
        lg->add_edge(pick_node.get_driver_pin(), ori_sink);
        or_out_edge.del_edge();
      }
      node.inp_edges().begin()->del_edge();
    }
  }
}
