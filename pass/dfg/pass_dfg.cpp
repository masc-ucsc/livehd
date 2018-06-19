#include <cstdlib>
#include <cassert>
#include <vector>
#include <unordered_set>

#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"

using std::string; // FIXME: we use std::string and std::... all the time
using std::unordered_map;
using std::vector;

unsigned int Pass_dfg::temp_counter = 0;

void Pass_dfg::transform() {
  LGraph *dfg = new LGraph(opack.lgdb_path, opack.output_name, false);
  transform(dfg);
}

void Pass_dfg::transform(LGraph *dfg) {
  assert(!opack.graph_name.empty());
  LGraph *cfg = new LGraph(opack.lgdb_path, opack.graph_name, false);

  cfg_2_dfg(dfg, cfg);
}

void Pass_dfg::cfg_2_dfg(LGraph *dfg, const LGraph *cfg) {
  Index_ID    itr = find_root(cfg);
  CF2DF_State state(dfg);

  process_cfg(dfg, cfg, &state, itr);
  attach_outputs(dfg, &state);

  dfg->sync();
}

Index_ID Pass_dfg::process_cfg(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID top_node) {
  Index_ID itr = top_node;
  Index_ID last_itr = 0;

  while (itr != 0) {
    last_itr = itr;
    itr = process_node(dfg, cfg, state, itr);
  }

  return last_itr;
}

Index_ID Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID node) {
  CFG_Node_Data data(cfg, node);

  switch (cfg->node_type_get(node).op) {
  case CfgAssign_Op:
    process_assign(dfg, cfg, state, data, node);
    return get_child(cfg, node);
  case CfgIf_Op:
    return process_if(dfg, cfg, state, data, node);
  case CfgIfMerge_Op:
    return 0;
  default:
    fmt::print("*************Unrecognized node type[n={}]: {}\n",
      node, cfg->node_type_get(node).get_name());
    return get_child(cfg, node);
  }
}

void Pass_dfg::process_assign(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  const auto &target = data.get_target();
  Index_ID dfnode = create_node(dfg, state, target);

  dfg->node_type_set(dfnode, CfgAssign_Op);
  //dfg->set_node_instance(dfnode, data.get_operator());
  vector<Index_ID> operands = process_operands(dfg, cfg, state, data, node);

  for (Index_ID id : operands)
    dfg->add_edge(Node_Pin(id, 0, false), Node_Pin(dfnode, 0, true));
  
  if (state->fluid_df() && (is_output(target) || is_register(target)))
    add_write_marker(dfg, state, target);
}

Index_ID Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  Index_ID cond = state->get_reference(data.get_target());
  const auto &operands = data.get_operands();

  Index_ID tbranch = std::stol(operands[0]);
  CF2DF_State tstate = state->copy();
  Index_ID tb_next = get_child(cfg, process_cfg(dfg, cfg, &tstate, tbranch));

  Index_ID fbranch = std::stol(operands[1]);

  if (fbranch != node) {                                // there is an 'else' clause
    CF2DF_State fstate = state->copy();
    Index_ID fb_next = get_child(cfg, process_cfg(dfg, cfg, &fstate, fbranch));
    assert(tb_next == fb_next);
    add_phis(dfg, cfg, state, &tstate, &fstate, cond);  
  } else {
    add_phis(dfg, cfg, state, &tstate, state, cond);    // if there's no else, the 'state' of the 'else' branch is the same as the parent
  }

  return tb_next;
}

void Pass_dfg::add_phis(LGraph *dfg, const LGraph *cfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition) {
  std::unordered_set<string> phis_added;

  for (const auto &pair : tstate->references()) {
    if (reference_changed(parent, tstate, pair.first)) {
      add_phi(dfg, parent, tstate, fstate, condition, pair.first);
      phis_added.insert(pair.first);
    }
  }

  for (const auto &pair : fstate->references()) {
    if (phis_added.find(pair.first) == phis_added.end() && reference_changed(parent, fstate, pair.first))
      add_phi(dfg, parent, tstate, fstate, condition, pair.first);
  }
}

void Pass_dfg::add_phi(LGraph *dfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition, const string &variable) {
  Index_ID tid = resolve_phi_branch(dfg, parent, tstate, variable);
  Index_ID fid = resolve_phi_branch(dfg, parent, fstate, variable);

  Index_ID phi = dfg->create_node().get_nid();
  dfg->node_type_set(phi, Mux_Op);
  auto tp = dfg->node_type_get(phi);

  Port_ID tin = tp.get_input_match("B");
  Port_ID fin = tp.get_input_match("A");
  Port_ID cin = tp.get_input_match("S");

  dfg->add_edge(Node_Pin(tid, 0, false), Node_Pin(phi, tin, true));
  dfg->add_edge(Node_Pin(fid, 0, false), Node_Pin(phi, fin, true));
  dfg->add_edge(Node_Pin(condition, 0, false), Node_Pin(phi, cin, true));

  parent->update_reference(variable, phi);
}

Index_ID Pass_dfg::resolve_phi_branch(LGraph *dfg, CF2DF_State *parent, CF2DF_State *branch, const std::string &variable) {
  if (branch->has_reference(variable))
    return branch->get_reference(variable);
  else if (parent->has_reference(variable))
    return parent->get_reference(variable);
  else if (is_register(variable))
    return create_register(dfg, parent, variable);
  else if (is_input(variable))
    return create_input(dfg, parent, variable);
  else if (is_output(variable))
    return create_output(dfg, parent, variable);
  else
    return default_constant(dfg, parent);
}

vector<Index_ID> Pass_dfg::process_operands(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  const auto &dops = data.get_operands();
  vector<Index_ID> ops(dops.size());

  for (size_t i = 0; i < dops.size(); i++) {
    if (state->has_reference(dops[i]))
      ops[i] = state->get_reference(dops[i]);
    else {
      if (is_constant(dops[i]))
        ops[i] = default_constant(dfg, state);
      else if (is_register(dops[i]))
        ops[i] = create_register(dfg, state, dops[i]);
      else if (is_input(dops[i]))
        ops[i] = create_input(dfg, state, dops[i]);
      else if (is_output(dops[i]))
        ops[i] = create_output(dfg, state, dops[i]);
      else
        ops[i] = create_private(dfg, state, dops[i]);
    }

    if (state->fluid_df() && is_input(dops[i]))
      add_read_marker(dfg, state, dops[i]);
  }

  return ops;
}

void Pass_dfg::assign_to_true(LGraph *dfg, CF2DF_State *state, const std::string &v) {
  Index_ID node = create_node(dfg, state, v);
  dfg->node_type_set(node, CfgAssign_Op);

  Index_ID tc = true_constant(dfg, state);
  dfg->add_edge(Node_Pin(tc, 0, false), Node_Pin(node, 0, true));
}

void Pass_dfg::attach_outputs(LGraph *dfg, CF2DF_State *state) {
  for (const auto &pair : state->copy().references()) {
    const auto &var = pair.first;

    if (is_register(var) || is_output(var)) {
      Index_ID lref = pair.second;

      Index_ID oid = create_output(dfg, state, var);
      dfg->add_edge(Node_Pin(lref, 0, false), Node_Pin(oid, 0, true));
    }
  }

  if (state->fluid_df())
    add_fluid_behavior(dfg, state);
}

void Pass_dfg::add_fluid_behavior(LGraph *dfg, CF2DF_State *state) {
  vector<Index_ID> inputs, outputs;
  add_fluid_ports(dfg, state, inputs, outputs);

}

void Pass_dfg::add_fluid_ports(LGraph *dfg, CF2DF_State *state, vector<Index_ID> &data_inputs, vector<Index_ID> &data_outputs) {
  for (const auto &pair : state->outputs()) {
    if (!is_valid_marker(pair.first) && !is_retry_marker(pair.first)) {
      auto valid_output = valid_marker(pair.first);
      auto retry_input = retry_marker(pair.first);
      data_outputs.push_back(state->get_reference(pair.first));
      
      if (!state->has_reference(valid_output))
        create_output(dfg, state, valid_output);
      
      if (!state->has_reference(retry_input))
        create_input(dfg, state, retry_input);
    }
  }

  for (const auto &pair : state->inputs()) {
    if (!is_valid_marker(pair.first) && !is_retry_marker(pair.first)) {
      auto valid_input = valid_marker(pair.first);
      auto retry_output = retry_marker(pair.first);
      data_inputs.push_back(state->get_reference(pair.first));
      
      if (!state->has_reference(valid_input))
        create_input(dfg, state, valid_input);
      
      if (!state->has_reference(retry_output))
        create_output(dfg, state, retry_output);
    }
  }
}

void Pass_dfg::add_fluid_logic(LGraph *dfg, CF2DF_State *state, const vector<Index_ID> &data_inputs, const vector<Index_ID> &data_outputs) {
  //Index_ID abort_id = add_abort_logic(dfg, state, data_inputs, data_outputs);

  
}

void Pass_dfg::add_abort_logic(LGraph *dfg, CF2DF_State *state, const vector<Index_ID> &data_inputs, const vector<Index_ID> &data_outputs) {

}

Index_ID Pass_dfg::find_root(const LGraph *cfg) {
  for(auto idx : cfg->fast()) {
    if(cfg->is_root(idx))
      return idx;
  }

  assert(false);
}

Index_ID Pass_dfg::get_child(const LGraph *cfg, Index_ID node) {
  vector<Index_ID> children;

  for(const auto &cedge : cfg->out_edges(node))
    children.push_back(cedge.get_inp_pin().get_nid());

  if(children.size() == 1)
    return children[0];
  else if(children.size() == 0)
    return 0;
  else
    assert(false);
}

Index_ID Pass_dfg::create_register(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->node_type_set(nid, Flop_Op);
  state->add_register(var_name, nid);

  return nid;
}

Index_ID Pass_dfg::create_input(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->add_graph_input(var_name.c_str(), nid);

  return nid;
}

Index_ID Pass_dfg::create_output(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->add_graph_output(var_name.c_str(), nid);
  
  return nid;
}

Index_ID Pass_dfg::create_private(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->node_type_set(nid, CfgAssign_Op);

  g->add_edge(Node_Pin(nid, 0, false), Node_Pin(default_constant(g, state), 0, true));
 
  return nid;
}

Index_ID Pass_dfg::default_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  return nid;
}

Index_ID Pass_dfg::true_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 1);

  string var_name = temp();
  g->set_node_wirename(nid, var_name.c_str());
  state->symbol_table().add(var_name, Type::create_logical());

  return nid;
}

Index_ID Pass_dfg::create_node(LGraph *g, CF2DF_State *state, const string &v) {
  Index_ID nid = g->create_node().get_nid();
  state->update_reference(v, nid);
  g->set_node_wirename(nid, v.c_str());

  return nid;
}

Index_ID Pass_dfg::create_AND(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2) {
  return create_binary(g, state, op1, op2, LOGICAL_AND_OP);
}

Index_ID Pass_dfg::create_OR(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2) {
  return create_binary(g, state, op1, op2, LOGICAL_OR_OP);
}

Index_ID Pass_dfg::create_binary(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2, const char *oper) {
  auto target = temp();
  Index_ID dfnode = create_node(g, state, target);

  g->node_type_set(dfnode, CfgAssign_Op);
  //g->set_node_instance(dfnode, oper);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));
  g->add_edge(Node_Pin(op2, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}

Index_ID Pass_dfg::create_NOT(LGraph *g, CF2DF_State *state, Index_ID op1) {
  auto target = temp();
  Index_ID dfnode = create_node(g, state, target);

  g->node_type_set(dfnode, CfgAssign_Op);
  //g->set_node_instance(dfnode, LOGICAL_NOT_OP);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}


Pass_dfg_options_pack::Pass_dfg_options_pack() : Options_pack() {

  Options::get_desc()->add_options()(
      "output,o", boost::program_options::value(&output_name), "output graph-name");

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(*Options::get_desc()).allow_unregistered().run(), vm);

  output_name        = (vm.count("output") > 0) ? vm["output"].as<string>() : graph_name + "_df";
  console->info("inou_cfg graph_name:{}, gen-dots:{}", graph_name);
}


//Sheng Zone
void Pass_dfg::test_const_conversion() {
  LGraph *tg = new LGraph(opack.lgdb_path, opack.output_name, false);

  //const std::string str_in = "0d?";//24 bits
  //const std::string str_in = "0x00FF?FF_FF?_u";//24 bits
  //const std::string str_in = "0b1111u";//4 bits
  //const std::string str_in = "0xFFFF_FF_s";//24 bits
  //const std::string str_in = "0x7AFF_FFFF_FFFF_FFFF_FF_s";//40 bits
  //const std::string str_in = "0b1111_1111_1111_1111_1111_1111_1111_1111_1111";
  //const std::string str_in = "0b00011111111_11111111_11111111s";//legal but logic conflict declaration
  //const std::string str_in = "-0d2147483647";
  //const std::string str_in = "-0d128";
  const std::string str_in = "-0d4294967297";
  //const std::string str_in = "-0d2147483648";
  //const std::string str_in = "-0d2147483649";
  //const std::string str_in = "-0d5294967298";
  //const std::string str_in = "0b11101111_11101111_11101111_11101111_11101111???110?10";
  //const std::string str_in = "0d2147483647";
  //const std::string str_in = "-0d8";

  bool              is_signed          = false;
  bool              is_in32b           = true;
  bool              is_explicit_signed = false; //explicit assign the signed mark
  bool              has_bool_dc        = false; //dc = don't care, ex: 0x100??101
  bool              is_pure_dc         = false; //ex: ????
  uint32_t          val                = 0;
  uint32_t          explicit_bits      = 0;
  size_t            bit_width          = 0;

  resolve_constant(tg, str_in, is_signed, is_in32b, is_explicit_signed, has_bool_dc, is_pure_dc, val, explicit_bits, bit_width);

  fmt::print("\n");
  fmt::print("out of 32 bits range?      {}\n",!is_in32b);
  fmt::print("signed:                    {}\n",is_signed);
  fmt::print("explicit sign declaration: {}\n",is_explicit_signed);
  fmt::print("has boolean don't care:    {}\n",has_bool_dc);
  fmt::print("stored value:              {}\n",val);
  fmt::print("explicit_bits:             {}\n",explicit_bits);
  fmt::print("bit_width:                 {}\n",bit_width);
  fmt::print("\n");
  tg->sync();
}


Index_ID Pass_dfg::resolve_constant(LGraph *g, const std::string& str, bool& is_signed, bool& is_in32b,
                                    bool& is_explicit_signed, bool& has_bool_dc, bool& is_pure_dc, uint32_t& val,
                                    uint32_t& explicit_bits, size_t& bit_width)
{
  string token1st, token2nd;
  string str_in = str;
  char rm = '_';//remove '_' in 0xF___FFFF
  str_in.erase(std::remove(str_in.begin(), str_in.end(),rm), str_in.end());
  size_t s_pos = str_in.find('s');//O(n)
  size_t u_pos = 0;
  size_t idx;

  //extract 1st and 2nd tokens, delimiter: s or u
  if(s_pos != string::npos){
    token1st = str_in.substr(0,s_pos);
    token2nd = str_in.substr(s_pos+1);
    is_signed = true;
    is_explicit_signed = true;
  }
  else{
    u_pos = str_in.find('u');//O(n)
    if(u_pos != string::npos){
      token1st = str_in.substr(0,u_pos);
      token2nd = str_in.substr(u_pos+1);
      is_explicit_signed = true;
    }
    else
      token1st = str_in;
  }

  //explicit bits width
  if(token2nd != "")
    explicit_bits = (uint32_t) std::stoi(token2nd);
  else
    explicit_bits = 0;

  //main process for bin/hex/dec
  //hex
  if(token1st[0] == '0' && token1st[1] == 'x'){
    token1st = token1st.substr(2);//exclude leading hex 0x
    idx = token1st.find_first_not_of('0') ;
    token1st = token1st.substr(idx);//exclude leading 0s

    std::string h2b_token1st;

    h2b_token1st += hex_msb_char_to_bin(token1st[0]); //deal with msb char
    for(unsigned i = 1; i != token1st.length(); ++i)
      h2b_token1st += hex_char_to_bin(token1st[i]);

    bit_width = explicit_bits ? explicit_bits : h2b_token1st.size();
    is_in32b = bit_width > 32 ? false : true;
    size_t dc_pos = h2b_token1st.find('?'); //dc = don't care
    if(dc_pos != string::npos){
      has_bool_dc = true;
      return process_bin_token_with_dc(g, h2b_token1st);
    }
    else
      return process_bin_token(g, h2b_token1st, (uint16_t)bit_width, val);
  }
  //binary
  else if (token1st[0] == '0' && token1st[1] == 'b') {
    token1st = token1st.substr(2);//exclude leading bin 0b
    idx = token1st.find_first_not_of('0');
    token1st = token1st.substr(idx);//exclude leading 0s
    bit_width = explicit_bits ? explicit_bits : token1st.size();
    is_in32b = bit_width > 32 ? false : true;

    size_t dc_pos = token1st.find('?'); //dc = don't care
    if(dc_pos != string::npos){
      has_bool_dc = true;
      return process_bin_token_with_dc(g, token1st);
    }
    else
      return process_bin_token(g, token1st, (uint16_t)bit_width, val);

  }
  else{//decimal
    if(token1st[2] == '?') {//case of pure question mark
      is_pure_dc = true;
      return create_dontcare_node(g,0);
    }

    long long sum = 0;
    string d2b_token1st;
    string s_2scmp;

    if(token1st[0] == '-'){
      is_explicit_signed = true;
      is_signed = true;
      //token1st = token1st[0] + token1st.substr(3);//exclude middle "0d"
      token1st = token1st.substr(3);//exclude middle "-0d"

      for(size_t i = 0; i < token1st.size(); i++){
        sum = sum *10 + (token1st[i] - '0');
        fmt::print("now is round {}, token1st[{}] is {}, sum is {}\n", i, i, token1st[i], sum);
        if(sum >= 4294967296){
          uint32_t tmp = sum- 4294967296;
          int j = 0;
          while(j <= 31){
            s_2scmp = std::to_string(tmp%2) + s_2scmp;
            tmp = tmp >> 1;
            j ++;
          }
          sum = sum >> 32;
        }
      }

      while(sum != 0){
        s_2scmp = std::to_string(sum%2) + s_2scmp;
        sum = sum >> 1;
      }

      s_2scmp = '0' + s_2scmp; //add leading 0 before converting 2's complement
      fmt::print("before 2's complement, the s_binary = {}\n", s_2scmp);

      for(size_t i = 0; i< s_2scmp.length(); i++){
        if(s_2scmp[i] == '0')
          s_2scmp[i] = '1';
        else
          s_2scmp[i] = '0';
      }
      fmt::print("middle 2's complement, the s_binary = {}\n", s_2scmp);

      int carry = 0;
      int s_2scmp_size = s_2scmp.size();
      // Add all bits one by one
      for (int i = s_2scmp_size-1 ; i >= 0 ; i--)
      {
        int first_bit = s_2scmp.at(i) - '0';
        int second_bit = (i==s_2scmp_size-1) ? 1 : 0;
        // boolean expression for sum of 3 bits
        int s = (first_bit ^ second_bit ^ carry)+'0';

        d2b_token1st = (char)s + d2b_token1st;
        // boolean expression for 3-bit addition
        carry = (first_bit & second_bit) | (second_bit & carry) | (first_bit & carry);
      }
      fmt::print(" after 2's complement, the s_binary = {}\n", d2b_token1st);
    }
    else{
      token1st = token1st.substr(2);
      for(size_t i = 0; i < token1st.size(); i++){
        sum = sum *10 + (token1st[i] - '0');
        fmt::print("now is round {}, token1st[{}] is {}, sum is {}\n", i, i, token1st[i], sum);
        if(sum >= 4294967296){
          uint32_t tmp = sum- 4294967296;
          int j = 0;
          while(j <= 31){
            d2b_token1st = std::to_string(tmp%2) + d2b_token1st;
            tmp = tmp >> 1;
            j ++;
          }
          sum = sum >> 32;
        }
      }

      while(sum != 0){
        d2b_token1st = std::to_string(sum%2) + d2b_token1st;
        sum = sum >> 1;
      }
    }

    bit_width = explicit_bits ? explicit_bits : d2b_token1st.size();
    is_in32b = bit_width > 32 ? false : true;

    return process_bin_token(g, d2b_token1st, (uint16_t)bit_width, val);

  }
}


uint32_t Pass_dfg::cal_bin_val_32b(const std::string& token){
  uint16_t idx = 0;
  uint32_t val = 0;
  while(token[idx]){
    uint8_t byte = token[idx];
    if(byte >= '0' && byte <= '1'){
      byte = byte - '0';
      val = val << 1 | (byte & 0xF);
    }
    idx++;
  }
  return val;
}


Index_ID Pass_dfg::process_bin_token (LGraph *g, const std::string& token, const uint16_t & bit_width, uint32_t& val){
  if(bit_width > 32){
    std::vector<Node_Pin> inp_pins;
    Index_ID nid_const32;
    int t_size = (int)token.size();
    Index_ID nid_join = g->create_node().get_nid();
    g->node_type_set(nid_join, Join_Op);
    g->set_bits(nid_join, t_size);

    int i = 1;
    string token_chunk = token.substr(t_size-32*i,32);

    while(t_size-32*i > 0){
      fmt::print("@round{}, token_chunk:                  {}\n", i, token_chunk);
      nid_const32 = create_const32_node(g, token_chunk, 32, val);
      inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      if(t_size-(32*(i+1)) > 0)
        token_chunk = token.substr(t_size - 32*(i+1),32);
      else{
        token_chunk = token.substr(0,t_size-32*i);
        fmt::print("@round{}, token_chunk:                 {}\n", i+1, token_chunk);
        nid_const32 = create_const32_node(g, token_chunk, token_chunk.size(), val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      }
      i++;
    }
    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
      pid++;
    }
    val = 0;// val = 0 for values that are out of 32bit range
    return nid_join;
  }
  else{
    Index_ID nid_const32 = create_const32_node(g, token, bit_width, val);
    return nid_const32;
  }
}

Index_ID Pass_dfg::process_bin_token_with_dc (LGraph *g, const std::string& token){
  fmt::print("process binary with don't cares!\n");
  uint32_t val;
  std::vector<Node_Pin> inp_pins;
  int t_size = (int)token.size();
  Index_ID nid_join = g->create_node().get_nid();
  g->node_type_set(nid_join, Join_Op);
  g->set_bits(nid_join, t_size);

  string sdc_buf; //continuous don't care characters
  string sval_buf;//continuous val characters
  int token_size = token.size();
  for(int i = 0; i < token_size; i++){
    if(token[i] == '?'){
      if(sval_buf.size()){
        Index_ID nid_const32 = create_const32_node(g, sval_buf, sval_buf.size(), val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
        sval_buf.clear();
      }
      sdc_buf += '?';
      if(i == token_size-1){
        Index_ID nid_dc = create_dontcare_node(g,sdc_buf.size());
        inp_pins.push_back(Node_Pin(nid_dc, 0, false));
      }
      else if(i+1 < token_size && token[i+1]!= '?'){
        Index_ID nid_dc = create_dontcare_node(g,sdc_buf.size());
        inp_pins.push_back(Node_Pin(nid_dc, 0, false));
        sdc_buf.clear();
      }
    }
    else{// token[i] = some value char
      sval_buf += token[i];
      if(sval_buf.size() == 32){
        Index_ID nid_const32 = create_const32_node(g, sval_buf, 32, val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
        sval_buf = sval_buf.substr(32);
      }
      else if(i == token_size -1){
        Index_ID nid_const32 = create_const32_node(g, sval_buf, sval_buf.size(), val);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      }
    }
  }

  int pid = 0;
  for(auto &inp_pin : inp_pins) {
    g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
    pid++;
  }
  return nid_join;
}

Index_ID Pass_dfg::create_const32_node (LGraph *g, const std::string& val_str, uint16_t node_bit_width, uint32_t& val){
    val = cal_bin_val_32b(val_str);

  Index_ID nid_const32 = g->create_node().get_nid();
  g->node_u32type_set(nid_const32,val);
  g->set_bits(nid_const32, node_bit_width);
  return nid_const32;
}

Index_ID Pass_dfg::create_dontcare_node (LGraph *g, uint16_t node_bit_width ){
  Index_ID nid_dc = g->create_node().get_nid();
  g->node_type_set(nid_dc, DontCare_Op);
  g->set_bits(nid_dc, node_bit_width);
  return nid_dc;
}

std::string Pass_dfg::hex_char_to_bin(char c) {
  switch(toupper(c)) {
    case '0': return "0000"; case '1': return "0001"; case '2': return "0010"; case '3': return "0011";
    case '4': return "0100"; case '5': return "0101"; case '6': return "0110"; case '7': return "0111";
    case '8': return "1000"; case '9': return "1001"; case 'A': return "1010"; case 'B': return "1011";
    case 'C': return "1100"; case 'D': return "1101"; case 'E': return "1110"; case 'F': return "1111";
    case '?': return "????"; default : assert(false);
  }
}

std::string Pass_dfg::hex_msb_char_to_bin(char c) {
  switch(toupper(c)) {
    case '0': return "";     case '1': return "1";    case '2': return "10";   case '3': return "11";
    case '4': return "100";  case '5': return "101";  case '6': return "110";  case '7': return "111";
    case '8': return "1000"; case '9': return "1001"; case 'A': return "1010"; case 'B': return "1011";
    case 'C': return "1100"; case 'D': return "1101"; case 'E': return "1110"; case 'F': return "1111";
    case '?': return "????"; default : assert(false);
  }
}
