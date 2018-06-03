#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include <cstdlib>
#include <cassert>
#include <vector>
#include <unordered_set>
using std::string;
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

  for (int i = 0; i < dops.size(); i++) {
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
    add_fluid_ports_and_logic(dfg, state);
}

void Pass_dfg::add_fluid_ports_and_logic(LGraph *dfg, CF2DF_State *state) {
  
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

  string var_name = "__tmp" + std::to_string(temp_counter++);
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
  //const std::string str_in = "0x00_F234_5678_1234_5678_9A_u";//40 bits
  //const std::string str_in = "0b1111_1111_1111_1111_1111_1111_1111_1111";//buggy underscore
  //const std::string str_in = "0b00011111111111111111111111111111111s";
  //const std::string str_in = "-2147483647";
  //const std::string str_in = "-128";
  //const std::string str_in = "-4294967295";
  //const std::string str_in = "-2147483648";
  //const std::string str_in = "-2147483649";
  const std::string str_in = "-4294967296";
  //const std::string str_in = "2147483647";
  //const std::string str_in = "-8";

  bool              is_signed          = false;
  bool              is_in32b           = true;
  bool              is_explicit_signed = false; //explicit assign the signed mark
  uint32_t          val                = 0;
  uint32_t          explicit_bits      = 0;
  size_t            bit_width          = 0;

  resolve_constant(tg, str_in, is_signed, is_in32b, is_explicit_signed, val, explicit_bits, bit_width);

  fmt::print("\n");
  fmt::print("out of 32 bits range?   {}\n",!is_in32b);
  fmt::print("signed:                 {}\n",is_signed);
  fmt::print("explicit sign assigned: {}\n",is_explicit_signed);
  fmt::print("stored value:           {}\n",val);
  fmt::print("explicit_bits:          {}\n",explicit_bits);
  fmt::print("bit_width:              {}\n",bit_width);
  fmt::print("\n");
}


Index_ID Pass_dfg::resolve_constant(LGraph *g,
                                    const std::string& str,
                                    bool&              is_signed,
                                    bool&              is_in32b,
                                    bool&              is_explicit_signed,
                                    uint32_t&          val,
                                    uint32_t&          explicit_bits,
                                    size_t&            bit_width)
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

    //need to know the bit width of 1st character's
    uint8_t char1st_width = 0;
    if(token1st[0] == '1')
      char1st_width = 1;
    else if(token1st[0] >= '2' && token1st[0] <='3')
      char1st_width = 2;
    else if(token1st[0] >= '4' && token1st[0] <='7')
      char1st_width = 3;
    else if(token1st[0] >= '8' && token1st[0] <='9')
      char1st_width = 4;
    else if(token1st[0] >= 'a' && token1st[0] <='f')
      char1st_width = 4;
    else if(token1st[0] >= 'A' && token1st[0] <='F')
      char1st_width = 4;

    bit_width = explicit_bits? explicit_bits : (token1st.size()-1)*4 + char1st_width;
    is_in32b = bit_width > 32 ? false : true;

    return process_hex_token(g, token1st, (uint16_t)bit_width, val);
  }
  else if (token1st[0] == '0' && token1st[1] == 'b') {//binary
    token1st = token1st.substr(2);//exclude leading bin 0b
    idx = token1st.find_first_not_of('0');
    token1st = token1st.substr(idx);//exclude leading 0s
    bit_width = explicit_bits ? explicit_bits : token1st.size();

    is_in32b = bit_width > 32 ? false : true;
    return process_bin_token(g, token1st, (uint16_t)bit_width, val);

  }
  else{//decimal
    string s_2scmp;
    if(token1st[0] == '-'){
      is_explicit_signed = true;
      is_signed = true;
    }

    InfInt big_int = token1st;

    //first converting big_int(either positive or negative) to 2's complement form
    //then you could analyze by process_bin_token
    if(big_int > 0) {
      while(big_int != 0){
        s_2scmp += (big_int%2).toString();
        big_int /=  2;
      }
      reverse(s_2scmp.begin(), s_2scmp.end());
      //while(big_int > 0){
      //  big_int_length +=1;
      //  big_int = big_int/2;
      //}
    }
    else{// < 0
      fmt::print("token1st                            = {}\n", token1st);
      InfInt pos_big_int = -big_int;
      string s_binary;
      while(pos_big_int != 0){
        s_binary += (pos_big_int%2).toString();
        pos_big_int /=  2;
      }
      s_binary += '0'; //leading 0 before converting 2's complement
      reverse(s_binary.begin(), s_binary.end());
      fmt::print("before 2's complement, the s_binary = {}\n", s_binary);
      for(auto i = 0; i< s_binary.length(); i++){
        if(s_binary[i] == '0')
          s_binary[i] = '1';
        else
          s_binary[i] = '0';
      }
      fmt::print("middle 2's complement, the s_binary = {}\n", s_binary);

      int carry = 0;
      int s_binary_size = s_binary.size();
      fmt::print("s_binary_size = {}\n", s_binary_size);
      // Add all bits one by one
      for (int i = s_binary_size-1 ; i >= 0 ; i--)
      {
        int first_bit = s_binary.at(i) - '0';
        int second_bit = (i==s_binary_size-1) ? 1 : 0;
        // boolean expression for sum of 3 bits
        int sum = (first_bit ^ second_bit ^ carry)+'0';

        s_2scmp = (char)sum + s_2scmp;
        // boolean expression for 3-bit addition
        carry = (first_bit & second_bit) | (second_bit & carry) | (first_bit & carry);
      }
      fmt::print("after 2's complement, the s_binary = {}\n", s_2scmp);
    }

    bit_width = explicit_bits ? explicit_bits : s_2scmp.size();
    is_in32b = bit_width > 32 ? false : true;
    return process_bin_token(g, s_2scmp, (uint16_t)bit_width, val);

  }
}



uint32_t Pass_dfg::cal_hex_val_32b(const std::string& token){
  uint16_t idx = 0;
  uint32_t val = 0;
  while (token[idx]) {
    uint8_t byte = token[idx];
    if (byte >= '0' && byte <= '9') {
      byte = byte - '0';
      val = val << 4 | (byte & 0xF);
    } else if (byte >= 'a' && byte <= 'f') {
      byte = byte - 'a' + 10;
      val = val << 4 | (byte & 0xF);
    } else if (byte >= 'A' && byte <= 'F') {
      byte = byte - 'A' + 10;
      val = val << 4 | (byte & 0xF);
    }
    idx++;
  }
  return val;
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


Index_ID Pass_dfg::process_hex_token(LGraph *g, const std::string &token, const uint16_t &bit_width, uint32_t &val) {
  if(bit_width > 32) {
    std::vector<Node_Pin> inp_pins;
    int t_size = (int)token.size();
    uint32_t val_chunk = 0;
    Index_ID nid_join = g->create_node().get_nid();
    g->node_type_set(nid_join, Join_Op);
    g->set_bits(nid_join, t_size*4);

    int i = 1;
    string token_chunk = token.substr(t_size-8*i,8);

    while(t_size-8*i > 0){
      fmt::print("@round{}, token_chunk:{}\n", i, token_chunk);
      val_chunk = cal_hex_val_32b(token_chunk);
      Index_ID nid_const32 = g->create_node().get_nid();
      g->node_u32type_set(nid_const32,val_chunk);
      g->set_bits(nid_const32,32);
      inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      if(t_size-(8*(i+1)) > 0)
        token_chunk = token.substr(t_size - 8*(i+1),8);
      else{
        token_chunk = token.substr(0,t_size-8*i);
        fmt::print("@round{}, token_chunk:{}\n", i+1, token_chunk);
        val_chunk = cal_hex_val_32b(token_chunk);
        nid_const32 = g->create_node().get_nid();
        g->node_u32type_set(nid_const32,val_chunk);
        g->set_bits(nid_const32,32);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      }
      i++;
    }
    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
      pid++;
    }
    return nid_join;
  }
  else{
    val = cal_hex_val_32b(token);
    Index_ID nid_const32 = g->create_node().get_nid();
    g->node_u32type_set(nid_const32,val);
    g->set_bits(nid_const32,bit_width);
    return nid_const32;
  }
}


Index_ID Pass_dfg::process_bin_token (LGraph *g, const std::string& token, const uint16_t & bit_width, uint32_t& val){
  if(bit_width > 32){
    std::vector<Node_Pin> inp_pins;
    int t_size = (int)token.size();
    uint32_t val_chunk = 0;
    Index_ID nid_join = g->create_node().get_nid();
    g->node_type_set(nid_join, Join_Op);
    g->set_bits(nid_join, t_size);

    int i = 1;
    string token_chunk = token.substr(t_size-32*i,32);

    while(t_size-32*i > 0){
      fmt::print("@round{}, token_chunk:{}\n", i, token_chunk);
      val_chunk = cal_bin_val_32b(token_chunk);
      Index_ID nid_const32 = g->create_node().get_nid();
      g->node_u32type_set(nid_const32,val_chunk);
      g->set_bits(nid_const32,32);
      inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      if(t_size-(32*(i+1)) > 0)
        token_chunk = token.substr(t_size - 32*(i+1),32);
      else{
        token_chunk = token.substr(0,t_size-32*i);
        fmt::print("@round{}, token_chunk:{}\n", i+1, token_chunk);
        val_chunk = cal_bin_val_32b(token_chunk);
        nid_const32 = g->create_node().get_nid();
        g->node_u32type_set(nid_const32,val_chunk);
        g->set_bits(nid_const32,32);
        inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      }
      i++;
    }
    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
      pid++;
    }
    return nid_join;
  }
  else{
    val = cal_bin_val_32b(token);
    Index_ID nid_const32 = g->create_node().get_nid();
    g->node_u32type_set(nid_const32,val);
    g->set_bits(nid_const32,bit_width);
    return nid_const32;
  }
}
Index_ID Pass_dfg::process_dec_token (LGraph *g, const std::string& token1st, const uint16_t& bit_width, uint32_t& val){
  size_t idx = 0;
  InfInt big_int = token1st;
  //std::cout <<"big_int = " << big_int << std::endl;

  //find leading 1
  if(token1st[0] == '-')
    idx = token1st.substr(1).find_first_not_of('0') + 1;
  else
    idx = token1st.find_first_not_of('0');

  if(bit_width > 32){
    std::vector<Node_Pin> inp_pins;
    uint32_t val_chunk = 0;
    Index_ID nid_join = g->create_node().get_nid();
    g->node_type_set(nid_join, Join_Op);
    g->set_bits(nid_join, bit_width);
    int i = 1;

    while(bit_width-i*32 > 0) {
      InfInt big_int_lsb32 = big_int % 4294967296;
      big_int = big_int / 4294967296;//no shift operator in InfInt, use division instead...
      fmt::print("@round {}, token_chunk:{}\n", i, big_int_lsb32.toInt());
      fmt::print("@round {}, big_int:{}\n", i, big_int.toInt());
      val_chunk = big_int_lsb32.toInt();
      Index_ID nid_const32 = g->create_node().get_nid();
      g->node_u32type_set(nid_const32,val_chunk);
      g->set_bits(nid_const32,32);
      inp_pins.push_back(Node_Pin(nid_const32, 0, false));
      i++;
    }
    if(big_int != 0){
      fmt::print("@final round{}, token_chunk:{}\n", i, big_int.toUnsignedInt());
      val_chunk = big_int.toUnsignedInt();
      Index_ID nid_const32 = g->create_node().get_nid();
      g->node_u32type_set(nid_const32,val_chunk);
      g->set_bits(nid_const32,32);
      inp_pins.push_back(Node_Pin(nid_const32, 0, false));
    }

    int pid = 0;
    for(auto &inp_pin : inp_pins) {
      g->add_edge(inp_pin, Node_Pin(nid_join, pid, true));
      pid++;
    }
    return nid_join;
  }//end of bit_width > 32
  else{
    //val = big_int.toUnsignedInt();
    val = big_int.toInt();
    //fmt::print("val = {}\n", val);
    Index_ID nid_const32 = g->create_node().get_nid();
    g->node_u32type_set(nid_const32,val);
    g->set_bits(nid_const32,bit_width);
    return nid_const32;
  }
}//end of decimal







#if 0
big_int_length = 32; //at least 32 bits
fmt::print("token1st           = {}\n", token1st);
fmt::print("big_int/2147483648 = {}\n\n\n", (big_int/2147483648).toInt());
while((big_int / 2147483648 ) <= -1){
big_int_length +=32;
big_int = big_int/2147483648;
}



    static const string hex_digits("0123456789ABCDEF");

    string hex;
    InfInt scratch = token1st;

    while (scratch != 0)
    {
      fmt::print("scratch lsb 4bits is {}\n", (scratch%16).toString());
      hex += hex_digits[(scratch % 16).toUnsignedInt()];
      scratch /= 16;
    }

    reverse(hex.begin(), hex.end());
    fmt::print("-------------------\n");
    fmt::print("the converted hex string is: {}\n",hex);
    fmt::print("-------------------\n");



#endif