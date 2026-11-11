//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "bitwidth_range.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"  // graph:graph — livehd::graph_util::* helpers
#include "pass.hpp"

class Bitwidth {
public:
  // The node's in-pin SNAPSHOT that every process_* below operates on.
  //
  // It is a snapshot ON PURPOSE, not for convenience: these rewrite the graph
  // while walking their own operands (connect_sink, connect_driver, del_edge,
  // del_node), and a lazy pin view is a view over the live edge storage those
  // calls mutate. The old spelling was inp_edges(), which materialized for the
  // same reason.
  //
  // One driver per sink pin (graph/cell.hpp), so a SINK PIN *is* an operand:
  // element `i` is the i-th operand's sink pin, `p.get_driver_pin()` is the
  // value feeding it, and `p.get_port_id()` is its port -- ascending, which is
  // what the positional readers (inp_edges[0], inp_edges[1]) rely on.
  using Inp_pins = absl::InlinedVector<hhds::Pin_class, 8>;

private:
  static inline int trace_module_cnt = 0;

protected:
  int  max_iterations;
  bool constrain_outputs;
  bool discovered_some_backward_nodes_try_again;

  enum class Attr { Set_other, Set_ubits, Set_sbits, Set_max, Set_min, Set_dp_assign };

  static Attr get_key_attr(std::string_view key);

  bool                                                   not_finished;
  absl::flat_hash_map<hhds::Class_index, Bitwidth_range> bwmap;  // keyed by driver pin's class_index

  // Nodes added by attr_set processing (Get_mask insertions) that must be
  // re-visited after the forward iterator finishes the current pass.
  std::vector<hhds::Node_class> pending_added_nodes;

  hhds::Graph* current_graph = nullptr;

public:
  // A compact-loop body's index input is the loop's own induction value: its
  // range is exactly [first .. first+step*(count-1)] (hull, either step sign),
  // not what the declared port width could hold. The parent's loop Sub knows
  // it; the body, compiled as its own graph, does not, so the pass collects it
  // from every loop Sub before the bodies run.
  struct Loop_index {
    hhds::Port_id port = 0;
    std::string   name;  // the index input's declared name: a specialized clone renumbers ports
    int64_t       lo   = 0;
    int64_t       hi   = 0;
  };
  void collect_loop_indices(hhds::Graph* g);

  // Allow memory addresses the pass cannot prove inside [0, size) (debug only;
  // otherwise it is the compile error `mem-address-overflow`).
  // Process-wide: a sim built with sim.debug keeps unproven memory addresses (checked at run time);
  // set by pass.bitwidth so the later private Bitwidth runs (specialize, satopt) agree.
  static inline bool allow_unproven_mem_address = false;

protected:
  absl::flat_hash_map<hhds::Gid, Loop_index> loop_indices;
  absl::flat_hash_set<hhds::Gid>             loop_index_conflict;
  absl::flat_hash_map<std::string, Loop_index> loop_indices_by_name;
  absl::flat_hash_set<std::string>             loop_index_name_conflict;
  std::optional<Loop_index>                    find_loop_index(hhds::Graph* g);
  bool                                         library_scanned = false;
  void                                       check_mem_address(const hhds::Node_class& node);

  void set_bw_1bit(hhds::Pin_class dpin);
  void set_bits_sign(const hhds::Pin_class& dpin, const Bitwidth_range& bw);
  void adjust_bw(hhds::Pin_class dpin, const Bitwidth_range& bw);

  void process_not(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_flop(hhds::Node_class& node);
  void process_memory(hhds::Node_class& node);
  void process_mux(hhds::Node_class& node, Inp_pins& inp_edges);
  std::optional<Bitwidth_range> refine_by_guard(const hhds::Node_class& mux, const hhds::Pin_class& arm_driver, bool true_arm,
                                                const Bitwidth_range& range);
  void process_hotmux(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_sra(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_shl(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_sum(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_div(hhds::Node_class& node);
  void process_mult(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_get_mask(hhds::Node_class& node);
  void process_set_mask(hhds::Node_class& node);
  void process_concat(hhds::Node_class& node);
  void process_sext(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_comparator(hhds::Node_class& node);
  void process_bit_or(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_bit_xor(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_bit_and(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_assignment_or(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_ror(hhds::Node_class& node, Inp_pins& inp_edges);
  void process_attr_set_dp_assign(hhds::Node_class& node);
  void process_attr_set_bw(hhds::Node_class& node, Bitwidth::Attr attr);
  void process_attr_set(hhds::Node_class& node);
  void insert_tposs_nodes(hhds::Node_class& node_attr, int32_t ubits);

  void debug_unconstrained_msg(hhds::Node_class& node, const hhds::Pin_class& d_dpin);
  void try_delete_attr_node(hhds::Node_class& node);
  void set_subgraph_boundary_bw(hhds::Node_class& node);
  void dump(hhds::Graph* g);

  // After the iteration budget is spent, warn (via diag) for every driver pin
  // whose bits/sign could not be bounded.
  void report_unbounded(hhds::Graph* g);
  void remove_mask_identities(hhds::Graph* g);

  void bw_pass(hhds::Graph* g);

public:
  explicit Bitwidth(int max_iterations, bool constrain_outputs = true);
  void do_trans(const std::shared_ptr<hhds::Graph>& g);
};
