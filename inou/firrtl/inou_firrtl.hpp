//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// External package includes
#include <cstdint>
#include <string_view>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <string>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/btree_set.h"
#include "firrtl.pb.h"

#pragma GCC diagnostic pop
// LiveHD includes

#include "lconst.hpp"
#include "lgraph.hpp"
#include "lnast.hpp"
#include "pass.hpp"

struct Global_module_info {
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::tuple<std::string, uint16_t, bool>>>  module2outputs; // <hier_name, bits, sign>
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>                              module2inputs;  // <hier_name>
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::pair<std::string, std::string>>> ext_module2param;
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, uint16_t>>               module_var2vec_size;
  
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, absl::btree_set<std::pair<std::string, bool>>>> var2flip; 
};

class Inou_firrtl : public Pass {
public:
  static std::string convert_bigint_to_str(const firrtl::FirrtlPB_BigInt &bigint);
  static void        create_io_list(const firrtl::FirrtlPB_Type &type, uint8_t dir, std::string_view port_id,
                                    std::vector<std::tuple<std::string, uint8_t, uint32_t, bool>> &vec);
  static inline absl::flat_hash_map<firrtl::FirrtlPB_Expression_PrimOp_Op, std::string> op2firsub;
  inline static Global_module_info                                                      glob_info;

protected:
  inline static std::mutex eprp_var_mutex;

  void     iterate_circuits(Eprp_var &var, const firrtl::FirrtlPB &firrtl_input, std::string_view file_name);
  void     iterate_modules(Eprp_var &var, const firrtl::FirrtlPB_Circuit &circuit, std::string_view file_name);
  void     populate_all_mods_io(Eprp_var &var, const firrtl::FirrtlPB_Circuit &circuit, std::string_view file_name);
  void     add_port_to_map(std::string_view mod_id, const firrtl::FirrtlPB_Type &type, uint8_t dir, bool flipped, std::string_view port_id,
                           Sub_node &sub, uint64_t &inp_pos, uint64_t &out_pos);

  void     add_global_io_flipness(std::string_view mod_id, bool flipped_in, std::string_view port_id);
  void     add_port_sub(Sub_node &sub, uint64_t &inp_pos, uint64_t &out_pos, std::string_view port_id, uint8_t dir);
  void     grab_ext_module_info(const firrtl::FirrtlPB_Module_ExternalModule &emod);

  void user_module_to_lnast(Eprp_var &var, const firrtl::FirrtlPB_Module &fmodule, std::string_view file_name);
  void ext_module_to_lnast(Eprp_var &var, const firrtl::FirrtlPB_Module &fmodule, std::string_view file_name);

  static void to_lnast(Eprp_var &var);

  //----------- FOR toFIRRTL ----------
  // FIXME: change function name to livehd coding style
  static void toFIRRTL(Eprp_var &var);
  void        do_tofirrtl(const std::shared_ptr<Lnast> &ln, firrtl::FirrtlPB_Circuit *circuit);
  void        process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx_smts, firrtl::FirrtlPB_Module_UserModule *umod);
  void        process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx_smts, firrtl::FirrtlPB_Statement_When *when, uint8_t pos_to_add_to);

  bool process_ln_assign_op(Lnast &ln, const Lnast_nid &lnidx_assign, firrtl::FirrtlPB_Statement *fstmt);
  void process_tup_asg(Lnast &ln, const Lnast_nid &lnidx_asg, std::string_view lhs, firrtl::FirrtlPB_Statement *fstmt);
  void process_ln_nary_op(Lnast &ln, const Lnast_nid &lnidx_assign, firrtl::FirrtlPB_Statement *fstmt);
  void process_ln_bit_not_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);
  void process_ln_reduce_xor_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);
  firrtl::FirrtlPB_Statement_When *process_ln_if_op(Lnast &ln, const Lnast_nid &lnidx_if);

  bool process_ln_select(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);
  void handle_attr_assign(Lnast &ln, const Lnast_nid &lhs, const Lnast_nid &rhs);
  void handle_sign_attr(Lnast &ln, std::string_view var_name, const Lnast_nid &rhs);
  void handle_clock_attr(Lnast &ln, std::string_view var_name, const Lnast_nid &rhs);
  void handle_async_attr(Lnast &ln, std::string_view var_name, const Lnast_nid &rhs);
  void handle_reset_attr(Lnast &ln, std::string_view var_name, const Lnast_nid &rhs);
  firrtl::FirrtlPB_Expression_SubField *make_subfield_expr(std::string_view name);

  uint8_t process_op_children(Lnast &ln, const Lnast_nid &lnidx_if, std::string_view firrtl_op);
  void    make_assignment(Lnast &ln, const Lnast_nid &lnidx_lhs, firrtl::FirrtlPB_Expression *expr_rhs,
                          firrtl::FirrtlPB_Statement *fstmt);

  // Helper Functions
  bool is_inp(std::string_view str);
  bool is_outp(std::string_view str);
  bool is_reg(std::string_view str);
  bool is_wire(std::string_view str);
  void create_connect_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression *rhs_expr,
                           firrtl::FirrtlPB_Statement *fstmt);
  void create_node_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression *rhs_expr, firrtl::FirrtlPB_Statement *fstmt);
  static std::string_view get_firrtl_name_format(Lnast &ln, const Lnast_nid &lnidx);
  static std::string_view strip_prefixes(std::string_view str);
  void                    add_refcon_as_expr(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression *expr);
  void                    add_const_as_ilit(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression_IntegerLiteral *ilit);
  firrtl::FirrtlPB_Expression_PrimOp_Op get_firrtl_oper_code(const Lnast_ntype &op_type);

  // Finding Circuit Components
  void                         FindCircuitComps(Lnast &ln, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         SearchNode(Lnast &ln, const Lnast_nid &parent_node, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         CheckTuple(Lnast &ln, const Lnast_nid &tup_node, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         HandleMemTup(Lnast &ln, const Lnast_nid &ref_node, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         CheckRefForComp(Lnast &ln, const Lnast_nid &ref_node, firrtl::FirrtlPB_Module_UserModule *umod);
  firrtl::FirrtlPB_Type       *CreateTypeObject(uint32_t bitwidth);
  firrtl::FirrtlPB_Expression *CreateULitExpr(const uint32_t &val);
  void                         CreateSubmodInst(Lnast &ln, const Lnast_nid &fcall_node, firrtl::FirrtlPB_Module_UserModule *umod);
  std::string                  ConvergeFCallName(std::string_view func_out, std::string_view func_inp);

private:
  //----------- FOR toFIRRTL ---------
  absl::flat_hash_map<std::string, firrtl::FirrtlPB_Port *>      io_map;
  absl::flat_hash_map<std::string, firrtl::FirrtlPB_Statement *> reg_wire_map;
  // Contains wires that need to be renamed when found (mainly due to func. calls)
  absl::flat_hash_map<std::string, std::string> wire_rename_map;

  /* Since range and bit_sel nodes are separated, this map is used to track
   * the name used by the range node and then the low/high nodes. */
  absl::flat_hash_map<std::string, std::pair<Lnast_nid, Lnast_nid>> name_to_range_map;

  // This tracks the LHS name used for a dot node then the two other children.
  absl::flat_hash_map<std::string, std::pair<Lnast_nid, Lnast_nid>> dot_map;

  // This indicates which register have async reset (if not on here, it's sync)
  absl::flat_hash_set<std::string> async_regs;

  // This maps a port name to the tuple that defines that port's attributes.
  absl::flat_hash_map<std::string, Lnast_nid> pname_to_tup_map;
  // This map tracks all of the ports associated with a specific memory.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> mem_to_ports_lists;

public:
  Inou_firrtl(const Eprp_var &var);

  static void setup();
};

class Inou_firrtl_module {
  friend Inou_firrtl;

public:
  Inou_firrtl_module() {
    dummy_expr_node_cnt = 0;
    tmp_var_cnt         = 0;
    seq_cnt             = 0;
  }

protected:
  enum class Leaf_type { Const_num, Const_str, Ref };
  //----------- for to_lnast ----------
  uint32_t dummy_expr_node_cnt;
  uint32_t tmp_var_cnt;
  uint32_t seq_cnt;

  std::string create_tmp_var();
  std::string create_tmp_mut_var();
  void        setup_register_q_pin(Lnast &lnast, Lnast_nid &parent_node, std::string_view reg_name);
  void        declare_register(Lnast &lnast, Lnast_nid &parent_node, std::string_view reg_name);
  void        setup_register_reset_init(Lnast &lnast, Lnast_nid &parent_node, std::string_view reg_raw_name, const firrtl::FirrtlPB_Expression &resete,
                                        const firrtl::FirrtlPB_Expression &inite, std::string_view head_chopped_hier_name, bool bits_set_done);

  // Helper Functions (for handling specific cases)
  int32_t  get_bit_count(const firrtl::FirrtlPB_Type &type);
  void     wire_init_flip_handling(Lnast &lnast, const firrtl::FirrtlPB_Type &type, std::string id, bool flipped, Lnast_nid &parent_node);
  void     handle_register(Lnast &lnast, const firrtl::FirrtlPB_Type &type, std::string id, Lnast_nid &parent_node, const firrtl::FirrtlPB_Statement &stmt);
  static void  dump_var2flip(const absl::flat_hash_map<std::string, absl::btree_set<std::pair<std::string, bool>>> &module_table);
  void     add_local_flip_info(bool flipped_in, std::string_view port_id);
  void     setup_scalar_bits(Lnast &lnast, std::string_view id, uint32_t bitwidth, Lnast_nid &parent_node, bool sign);
  void     create_module_inst(Lnast &lnast, const firrtl::FirrtlPB_Statement_Instance &inst, Lnast_nid &parent_node);
  void     split_hier_name(std::string_view                                                    hier_name,
                           std::vector<std::pair<std::string, Inou_firrtl_module::Leaf_type>> &hier_subnames);
  void     split_hier_name(std::string_view full_name, std::vector<std::string> &hier_subnames);
  void     set_leaf_type(std::string_view subname, std::string_view hier_name, size_t prev,
                         std::vector<std::pair<std::string, Inou_firrtl_module::Leaf_type>> &hier_subnames);
  void     collect_memory_data_struct_hierarchy(std::string_view mem_name, const firrtl::FirrtlPB_Type &type_in,
                                                std::string_view hier_fields_concats);
  void     handle_mux_assign(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node,
                             std::string_view lhs_of_asg);
  void     handle_valid_if_assign(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node,
                                  std::string_view lhs_of_asg);
  void     add_lnast_assign (Lnast &lnast, Lnast_nid &parent_node, std::string_view lhs_str, std::string_view rhs_str);

  void handle_neq_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_unary_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_and_reduce_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                            std::string_view lhs);
  void handle_or_reduce_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                           std::string_view lhs);
  void handle_xor_reduce_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                            std::string_view lhs);
  void handle_negate_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_conv_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_extract_bits_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                              std::string_view lhs);
  void handle_head_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_tail_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_concat_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_pad_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void handle_binary_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                                std::string_view lhs);

  void handle_static_shift_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                              std::string_view lhs);
  void handle_type_conv_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                           std::string_view lhs);
  void handle_as_usint_op(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void attach_expr_str2node(Lnast &lnast, std::string_view access_str, Lnast_nid &parent_node);
  void tuple_flattened_connections(Lnast& lnast, Lnast_nid& parent_node, std::string_view lhs_head, std::string_view rhs_head, std::string_view flattened_element, bool is_flipped);
  void tuple_flattened_connections_instance_l(Lnast& lnast, Lnast_nid& parent_node, std::string_view tup_hier_name, std::string_view hier_name_r, bool is_flipped, bool is_input);
  void tuple_flattened_connections_instance_r(Lnast& lnast, Lnast_nid& parent_node, std::string_view lhs_head, std::string_view rhs_head, std::string_view flattened_element, bool is_flipped, std::string_view inst_name);

  bool check_submodule_io_flipness(Lnast& lnast, std::string_view submodule_name, std::string_view tup_head, std::string_view hier_name, bool is_sub_instance = false);

  // void handle_bundle_vec_acc(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node, const bool is_rhs,
  //                            const Lnast_node &value_node);
  void create_tuple_add_from_str(Lnast &ln, Lnast_nid &parent_node, std::string_view flattened_str, const Lnast_node &value_node);
  void create_tuple_get_from_str(Lnast &ln, Lnast_nid &parent_node, std::string_view flattened_str, const Lnast_node &target_node);
  void create_tuple_add_for_instance_itup(Lnast &ln, Lnast_nid &parent_node, std::string_view lhs_full_name, std::string rhs_full_name);
  void create_wires_for_instance_itup(Lnast &ln, Lnast_nid &parent_node, std::string_view lhs_full_name, std::string_view rhs_full_name);
  void create_tuple_get_for_instance_otup(Lnast &ln, Lnast_nid &parent_node, std::string_view rhs_full_name, std::string lhs_full_name);
  void direct_instances_connection(Lnast &ln, Lnast_nid &parent_node, std::string lhs_full_name, std::string rhs_full_name);
  void create_default_value_for_scalar_var(Lnast &ln, Lnast_nid &parent_node, std::string_view sv, const Lnast_node &value_node);

  void init_cmemory(Lnast &lnast, Lnast_nid &parent_node, const firrtl::FirrtlPB_Statement_CMemory &cmem);
  void handle_mport_declaration(Lnast &lnast, Lnast_nid &parent_node, const firrtl::FirrtlPB_Statement_MemoryPort &mport);
  void initialize_rd_mport_from_usage(Lnast &lnast, Lnast_nid &parent_node, std::string_view mport_name);
  void initialize_wr_mport_from_usage(Lnast &lnast, Lnast_nid &parent_node, std::string_view mport_name);
  void init_mem_din(Lnast &lnast, std::string_view mem_name, std::string_view port_cnt_str);
  void init_mem_res(Lnast &lnast, std::string_view mem_name, std::string_view port_cnt_str);

  // void RegResetInitialization(Lnast &lnast, Lnast_nid &parent_node);

  // Deconstructing Protobuf Hierarchy
  void list_port_info(Lnast &lnast, const firrtl::FirrtlPB_Port &port, Lnast_nid parent_node);
  void record_all_input_hierarchy(std::string_view port_name);
  void record_all_output_hierarchy(std::string_view port_name);

  void list_prime_op_info(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, std::string_view lhs);
  void init_expr_add(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node, std::string_view lhs_unalt);
  std::string expr_str_flattened_or_tg(Lnast &lnast, Lnast_nid &parent_node, const firrtl::FirrtlPB_Expression& expr);

  void list_statement_info(Lnast &lnast, const firrtl::FirrtlPB_Statement &stmt, Lnast_nid &parent_node);
  std::string get_expr_hier_name(const firrtl::FirrtlPB_Expression &expr, bool &is_runtime_idx);
  std::string get_expr_hier_name(Lnast &lnast, Lnast_nid &parent_node, const firrtl::FirrtlPB_Expression &expr);
  std::string get_runtime_idx_field_name(const firrtl::FirrtlPB_Expression &expr);
  std::string name_prefix_modifier_flattener(std::string_view term, const bool is_rhs);
  void handle_rhs_runtime_idx(Lnast &lnast, Lnast_nid &parent_node, std::string_view hier_name_l, std::string_view hier_name_r, const firrtl::FirrtlPB_Expression &rhs_expr);
  void handle_lhs_runtime_idx(Lnast &lnast, Lnast_nid &parent_node, std::string_view hier_name_l, std::string_view hier_name_r, const firrtl::FirrtlPB_Expression &rhs_expr);
  uint16_t get_vector_size(const Lnast &lnast, std::string_view vec_name);
  void final_mem_interface_assign(Lnast &lnast, Lnast_nid &parent_node);

private:
  // module local tables
  absl::flat_hash_set<std::string> input_names;
  absl::flat_hash_set<std::string> output_names;
  absl::flat_hash_set<std::string> memory_names;
  absl::flat_hash_set<std::string> wire_names;
  absl::flat_hash_set<std::string> node_names;
  absl::flat_hash_set<std::string> is_invalid_table;
  absl::flat_hash_set<std::string> async_rst_names;
  absl::flat_hash_set<std::string> mport_usage_visited;

  // Maps a register name to its q_pin
  absl::flat_hash_map<std::string, std::string> reg2qpin;
  // Maps an instance name to the module name.
  absl::flat_hash_map<std::string, std::string> inst2module;

	// record all vector size here including all module io, wire, register 
	absl::flat_hash_map<std::string, uint16_t> var2vec_size;

  // FIXME->sh: check and remove the third directional field if redundant 
  absl::flat_hash_map<std::string, absl::btree_set<std::pair<std::string, bool>>> var2flip;

  absl::flat_hash_map<std::string, uint8_t>     mem2port_cnt;
  absl::flat_hash_map<std::string, uint8_t>     mem2wensize;
  absl::flat_hash_map<std::string, Lnast_nid>   mem2initial_idx;
  absl::flat_hash_map<std::string, std::string> mport2mem;
  // control how many bits should be shifted at the bit-vector for masked mem_wr_enable
  absl::flat_hash_map<std::string, uint32_t>                 mport2mask_bitvec;
  absl::flat_hash_map<std::string, uint8_t>                  mport2mask_cnt;
  absl::flat_hash_map<std::string, uint8_t>                  mem2one_wr_mport;
  absl::flat_hash_map<std::string, std::vector<std::string>> mem2din_fields;
  // mem -> <(rd_port_name1,1), (rd_port_name_foo, 7)>
  absl::flat_hash_map<std::string, std::vector<std::pair<std::string, uint8_t>>> mem2rd_mports;
};
