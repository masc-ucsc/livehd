
#include <fcntl.h>
#include <fstream>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>
#include <algorithm>

#include "inou_cfg.hpp"
#include "lgedgeiter.hpp"
#include "cfg_node_data.hpp"

using std::map;
using std::string;
using std::vector;

Inou_cfg::Inou_cfg() { }
Inou_cfg::~Inou_cfg() { }

Inou_cfg::Inou_cfg(const py::dict &dict) { opack.set(dict); }

vector<LGraph *> Inou_cfg::generate() {
  assert(!opack.graph_name.empty());
  assert(!opack.src.empty());

  vector<LGraph *> lgs;

  lgs.push_back(new LGraph(opack.lgdb, opack.graph_name, false));
  const auto &cfg_file = opack.src;

  int fd = open(cfg_file.c_str(), O_RDONLY);

  if(fd < 0) {
    console->error("cannot find input file {}\n", cfg_file);
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);

  char *memblock = (char *)mmap(nullptr, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(memblock == MAP_FAILED) {
    console->error("error, mmap failed\n");
    exit(-3);
  }

  cfg_2_lgraph(&memblock, lgs);

  for (LGraph *g : lgs)
    g->sync();

  close(fd);

  return lgs;
}

void Inou_cfg::generate(vector<const LGraph *> &lgs) {
  vector<LGraph *> gend = generate();
  
  for (const LGraph *g : gend)
    lgs.push_back(g);
}

void Inou_cfg::cfg_2_lgraph(char **memblock, vector<LGraph *> &lgs) {
  string                              s;
  vector<map<string, Index_ID>>       name2id_gs(1);    //gs = graphs
  vector<map<string, vector<string>>> chain_stks_gs(1); //chain_stacks for every graph, use vector to implement stack
  vector<string>                      nname_bg_gs(1);   //nname = node name
  vector<Index_ID>                    nid_ed_gs(1);
  map<string, uint32_t>               nfirst2gid; //map for every sub-graph and its first node name
  LGraph *                            gtop = lgs[0];

  bool gtop_bg_nname_recorded = false;
  char *str_ptr=0;

  char *p = strtok_r(*memblock, "\n\r\f", &str_ptr);

  while(p) {
    vector<string> words = split(p);

    if(*(words.begin()) == "END")
      break;

    string w1st = *(words.begin());
    string w3rd = *(words.begin() + 2);
    string w6th = *(words.begin() + 5);

    uint32_t gsub_id = std::stoi(w3rd);

    if(!gtop_bg_nname_recorded) {
      nname_bg_gs[0]         = w1st;
      gtop_bg_nname_recorded = true;
    }

    string dfg_data = p;

    if(w3rd != "0" && std::stoi(w3rd) >= lgs.size()) { //create sub-graph if different scope id
      lgs.push_back(new LGraph(opack.lgdb, opack.graph_name + std::to_string(lgs.size()), false));
      fmt::print("lgs size:{}", lgs.size());
      name2id_gs.resize(name2id_gs.size() + 1);
      chain_stks_gs.resize(chain_stks_gs.size() + 1);
      nid_ed_gs.resize(nid_ed_gs.size() + 1);
      nname_bg_gs.push_back(w1st);
      nfirst2gid[w1st] = gsub_id;
      build_graph(words, dfg_data, lgs[gsub_id], nfirst2gid, name2id_gs[gsub_id], chain_stks_gs[gsub_id], nid_ed_gs[gsub_id]);
    } else if(w3rd != "0") //construct sub-graph for function definition
      build_graph(words, dfg_data, lgs[gsub_id], nfirst2gid, name2id_gs[gsub_id], chain_stks_gs[gsub_id], nid_ed_gs[gsub_id]);
    else //build top graph
      build_graph(words, dfg_data, gtop, nfirst2gid, name2id_gs[0], chain_stks_gs[0], nid_ed_gs[0]);

    fmt::print("\n");
    p = strtok_r(nullptr, "\n\r\f", &str_ptr);
  } //end while loop

  for(size_t i = 0; i < chain_stks_gs.size(); i++) {
    for(auto &x : chain_stks_gs[i]) {
      fmt::print("\ncurrent is chain_stks_gs[{}], vid {} content is\n", i, x.first);
      for(auto j = x.second.rbegin(); j != x.second.rend(); ++j)
        fmt::print("{}\n", *j);
    }
  }

  update_ifs(lgs, name2id_gs);
}

void Inou_cfg::build_graph(vector<string> &words, string &dfg_data, LGraph *g, map<string, uint32_t> &nfirst2gid,
                           map<string, Index_ID> &name2id, map<string, vector<string>> &chain_stks, Index_ID &nid_end) {

  fmt::print("dfg_data:{}\n", dfg_data);
  string w1st = *(words.begin());
  string w2nd = *(words.begin() + 1);
  string w3rd = *(words.begin() + 2);
  string w4th = *(words.begin() + 3);
  string w5th = *(words.begin() + 4);
  string w6th = *(words.begin() + 5);
  string w7th = *(words.begin() + 6);
  string w8th = *(words.begin() + 7);
  string w9th;
  string w10th;
  if(w6th == "if" || w6th == "::{")
    w9th = *(words.begin() + 8);

  if(w6th == "if")
    w10th = *(words.begin() + 9);

  /*
    I.process 1st node
    only assign node type for first K in every line of cfg
  */

  if(name2id.count(w1st) == 0) { //if node has not been created before
    Node new_node = g->create_node();
    name2id[w1st] = new_node.get_nid();
    nid_end       = new_node.get_nid(); //keep update the latest final nid

    fmt::print("create node:{}, nid:{}\n", w1st, name2id[w1st]);

    g->node_loc_set(new_node.get_nid(), opack.src.c_str(), (uint32_t)std::stoi(w4th), (uint32_t)std::stoi(w5th));

    if(w6th == ".()")
      g->node_type_set(name2id[w1st], CfgFunctionCall_Op);
    else if(w6th == "for")
      g->node_type_set(name2id[w1st], CfgFor_Op);
    else if(w6th == "while")
      g->node_type_set(name2id[w1st], CfgWhile_Op);
    else if(w6th == "if")
      g->node_type_set(name2id[w1st], CfgIf_Op);
    else if(w6th == "RD")
      g->node_type_set(name2id[w1st], CfgBeenRead_Op);
    else
      g->node_type_set(name2id[w1st], CfgAssign_Op);
  } else {

    g->node_loc_set(name2id[w1st], opack.src.c_str(), (uint32_t)std::stoi(w4th), (uint32_t)std::stoi(w5th));

    if(w6th == ".()")
      g->node_type_set(name2id[w1st], CfgFunctionCall_Op);
    else if(w6th == "for")
      g->node_type_set(name2id[w1st], CfgFor_Op);
    else if(w6th == "while")
      g->node_type_set(name2id[w1st], CfgWhile_Op);
    else if(w6th == "if")
      g->node_type_set(name2id[w1st], CfgIf_Op);
    else if(w6th == "RD")
      g->node_type_set(name2id[w1st], CfgBeenRead_Op);
    else if(w6th == "::{") {
      g->node_subgraph_set(name2id[w1st], nfirst2gid[w9th]); //use nfirst2gid to get sub-graph gid
    } else {
      g->node_type_set(name2id[w1st], CfgAssign_Op);
    }
  }

  g->set_node_wirename(name2id[w1st], CFG_Node_Data(dfg_data).encode().c_str());
  /*

    II-0.process 2nd node and 9th node(if-else merging node)
  */

  if(w6th == "if" && name2id.count(w10th) == 0) { //if node has not been created before
    Node new_node  = g->create_node();
    name2id[w10th] = new_node.get_nid();
    nid_end        = new_node.get_nid(); //keep update the latest final nid
    fmt::print("create node:{}, nid:{}\n", w10th, name2id[w10th]);

    g->node_type_set(name2id[w10th], CfgIfMerge_Op);
  }

  if(w2nd != "null" && name2id.count(w2nd) == 0) {
    Node new_node = g->create_node();
    name2id[w2nd] = new_node.get_nid();
    nid_end       = new_node.get_nid(); //keep update the latest final nid
    fmt::print("create node:{}, nid:{}\n", w2nd, name2id[w2nd]);
  }

  /*
    III.deal with edge connection
  */
  Index_ID src_nid, dst_nid;

  if(w6th == "if") { //special case: if
    //III-1. connect if node to the begin of "true chunk" statement
    src_nid = name2id[w1st];
    dst_nid = name2id[w8th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("if statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

    //III-2. connect if node to the begin of "false chunk" statement
    if(w9th != "null") {
      src_nid = name2id[w1st];
      dst_nid = name2id[w9th];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 2\n", src_nid, dst_nid);
    }

    //III-3. connect top of stack to end if node
    if(chain_stks[w8th].back() != w10th) {
      src_nid = name2id[chain_stks[w8th].back()];
      fmt::print("chain_stks[w8th] back{}\n", chain_stks[w8th].back());
      dst_nid = name2id[w10th];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 3\n", src_nid, dst_nid);
    }

    if(w9th != "null") {
      if(chain_stks[w9th].back() != w10th) {
        src_nid = name2id[chain_stks[w9th].back()];
        fmt::print("chain_stks[w9th] back{}\n", chain_stks[w9th].back());
        dst_nid = name2id[w10th];
        g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
        fmt::print("if statement, connect src_node {} to dst_node {} ----- 4\n", src_nid, dst_nid);
      }
    } else { //only one branch of if. EX.  K13  K30   63  169  if   tmp3  K15  null 'K13
      src_nid = name2id[w1st];
      dst_nid = name2id[w10th];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);
    }

    //III-4. if it is an outer if statement, link w10th to w2nd
    if(w2nd != "null") {
      src_nid = name2id[w10th];
      dst_nid = name2id[w2nd];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 6\n", src_nid, dst_nid);
    }

    //III-5. figure out which stack w1st is belong to and push w10th into that stack
    for(auto const &x : chain_stks) {
      if(w1st == x.second.back()) {
        chain_stks[x.second[0]].push_back(w10th);
        break;
      }
    }

  } //end special case: if

  else if(w6th == "for") {
    //I. True: connect for node to body
    src_nid = name2id[w1st];
    dst_nid = name2id[w8th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("for statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
    //II. False: connect for node to next event
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("for statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
  } //end special case: for

  else if(w6th == "while") {
    //I. connect while node to body
    src_nid = name2id[w1st];
    dst_nid = name2id[w8th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("while statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);

    //II. False: connect while node to next event
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("while statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
  } //end special case: while

  else if(w6th == "::{") {
    //connect to the begin of function call
    src_nid = name2id[w1st];
    dst_nid = name2id[w9th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("function call statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);
  } //end special case: ::{

  else if(w2nd == "null") { //no w2nd to create edge, only update chain_stks
    bool   belong_tops   = false;
    string target_vec_id = w1st;

    //check equivalence between src_nid and every top of chain_stks
    for(auto const &x : chain_stks) {
      if(w1st == x.second.back()) {
        belong_tops   = true;
        target_vec_id = x.first;
        break;
      }
    }
    if(!belong_tops)
      chain_stks[target_vec_id].push_back(w1st);

  } else if(w2nd != "null") { //normal edge connection: Kx->Ky, update chain_stks
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("normal case connection, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);

    bool   belong_tops   = false;
    string target_vec_id = w1st;

    //check equivalence between src_nid and every top of chain_stks
    for(auto const &x : chain_stks) {
      if(w1st == x.second.back()) {
        belong_tops   = true;
        target_vec_id = x.first;
        break;
      }
    }

    if(!belong_tops) {
      chain_stks[target_vec_id].push_back(w1st);
      chain_stks[target_vec_id].push_back(w2nd);
    } else
      chain_stks[target_vec_id].push_back(w2nd);
  }

} //end of build_graph

vector<string> Inou_cfg::split(const string &str) {
  typedef string::const_iterator iter;
  vector<string>                 ret;

  iter i = str.begin();
  while(i != str.end()) {
    i = find_if(i, str.end(), not_space);
    // find end of next word
    iter j = find_if(i, str.end(), space);

    //copy the characters in [i,j)
    if(i != str.end())
      ret.push_back(string(i, j));

    i = j;
  }
  return ret;
}

void Inou_cfg::lgraph_2_cfg(const LGraph *g, const string &filename) {
  int line_cnt = 0;
  for(auto &idx : g->fast()) {
    if(g->get_node_wirename(idx) != nullptr) {
      fmt::print("{}\n", g->get_node_wirename(idx)); //for now, just print out cfg, maybe mmap write later
      ++line_cnt;
    }
  }
  fmt::print("END\n");
  ++line_cnt;
  fmt::print("line_cnt = {}\n", line_cnt);
}

// prp_get_value returns true if constant is within 32-bits; else returns string for const string in lgraph
bool prp_get_value(const string& str_in, string& str_out, bool &v_signed, uint32_t &explicit_bits, uint32_t &val){

  string token1st, token2nd;
  size_t s_pos = str_in.find('s');//O(n)
  size_t u_pos = 0;
  size_t idx;
  size_t bit_width;

  //decide 1st and 2nd tokens, delimiter: s or u
  if(s_pos != string::npos){
    char rm = '_';//remove _ in 0xF___FFFF
    string str_sub = str_in.substr(0,s_pos);
    str_sub.erase(std::remove(str_sub.begin(), str_sub.end(),rm), str_sub.end());
    token1st = str_sub;
    token2nd = str_in.substr(s_pos+1);
    v_signed = true;
  }
  else{
    u_pos = str_in.find('u');//O(n)
    if(u_pos != string::npos){
      token1st = str_in.substr(0,u_pos);
      token2nd = str_in.substr(u_pos+1);
    }
    else
      token1st = str_in;
  }

  //fmt::print("1st token:{}\n",token1st);

  //explicit bits width
  if(token2nd != "")
    explicit_bits = (uint32_t) std::stoi(token2nd);
  else
    explicit_bits = 0;


  if(token1st[0] == '0' && token1st[1] == 'x'){ //hexadecimal
    //detect the leading 1 and start from it
    idx = token1st.substr(2).find_first_not_of('0') + 2; //e.g. 0x000FFFFF, returns 5

    //need to determine first character's bit width
    uint8_t char1st_width = 0;
    if(token1st[idx] == '1')
      char1st_width = 1;
    else if(token1st[idx] >= '2' && token1st[idx] <='3')
      char1st_width = 2;
    else if(token1st[idx] >= '4' && token1st[idx] <='7')
      char1st_width = 3;
    else if(token1st[idx] >= '8' && token1st[idx] <='9')
      char1st_width = 4;
    else if(token1st[idx] >= 'a' && token1st[idx] <='f')
      char1st_width = 4;
    else if(token1st[idx] >= 'A' && token1st[idx] <='F')
      char1st_width = 4;


    bit_width = (token1st.size() - idx - 1) * 4 + char1st_width;
    //fmt::print("bit_width:{}\n", bit_width);
    if(bit_width > 32){
      str_out = token1st;
      return false;
    }

    while(token1st[idx]){
      uint8_t byte = token1st[idx];
      if (byte >= '0' && byte <= '9'){
        byte = byte - '0';
        val = val << 4 | (byte & 0xF);
      }
      else if (byte >= 'a' && byte <= 'f'){
        byte = byte - 'a' + 10;
        val = val << 4 | (byte & 0xF);
      }
      else if (byte >= 'A' && byte <= 'F'){
        byte = byte - 'A' + 10;
        val = val << 4 | (byte & 0xF);
      }
      idx++;
    }
  }
  else if (token1st[0] == '0' && token1st[1] == 'b') {//binary
    idx = token1st.substr(2).find_first_not_of('0') + 2; //e.g. 0b00011111, returns 5

    //fmt::print("idx:{}\n", idx);
    //fmt::print("token1st size:{}\n", token1st.size());
    bit_width = token1st.size() - idx;
    //fmt::print("bit_width:{}\n", bit_width);

    if(bit_width > 32){
      str_out = token1st;
      return false;
    }

    while(token1st[idx]){
      uint8_t byte = token1st[idx];
      if(byte >= '0' && byte <= '1'){
        byte = byte - '0';
        val = val << 1 | (byte & 0xF);
      }
      idx++;
    }
  }
  else{ //decimal
    //find leading 1
    if(token1st[0] == '-')
      idx = token1st.substr(1).find_first_not_of('0') + 1;
    else
      idx = token1st.find_first_not_of('0');

    string negative_max = "-2147483648";
    string positive_max =  "2147483647"; // size = 10

    if(token1st[0] == '-' && token1st.substr(idx).size() > 10){
      str_out = token1st;
      return false;
    }
    else if (token1st[0] != '-' && token1st.substr(idx).size() > 10){
      str_out = token1st;
      return false;
    }
    else if(token1st[0]  == '-' && token1st.substr(idx).size() == 10){
      int i=0;
      while(negative_max[i]){
        if(token1st[i] > negative_max[i]){
          str_out = token1st;
          return false;
        }
        i++;
      }
    }
    else if (token1st[0]  != '-' && token1st.substr(idx).size() == 10){
      int i=0;
      while(positive_max[i]){
        if(token1st[i] > positive_max[i]){
          str_out = token1st;
          return false;
        }
        i++;
      }
    }


    if (token1st[0] == '-') {//negative number
      while(token1st[idx])
        val = val*10 + (token1st[idx++] - '0');

      val = (uint32_t)(-val);
      v_signed = true;
    }
    else{
      while(token1st[idx])
        val = val*10 + (token1st[idx++] - '0');
    }
  }

  return true;
}

void Inou_cfg::update_ifs(vector<LGraph *> &lgs, vector<map<string, Index_ID>> &node_mappings)
{
  for (size_t i = 0; i < lgs.size(); i++) {
    LGraph *g = lgs[i];
    auto &mapping = node_mappings[i];

    for (auto idx : g->fast()) {
      CFG_Node_Data data(g, idx);

      if (data.get_operator() == COND_BR_MARKER) {
        const auto &dops = data.get_operands();
        vector<string> new_operands(dops.size());
        
        std::transform(dops.begin(), dops.end(), new_operands.begin(),
          [&](const string &op) -> string { return std::to_string(mapping[(op[0] == '\'') ? op.substr(1) : op]); });
        
        g->set_node_wirename(idx, CFG_Node_Data(data.get_target(), new_operands, data.get_operator()).encode().c_str());
      }
    }
  }
}

void Inou_cfg_options::set(const py::dict &dict) {
  for (auto item : dict) {
    const auto &key = item.first.cast<std::string>();

    try {
      if (is_opt(key,"src") ) {
        const auto &val = item.second.cast<string>();
        src = val;
      }else{
        set_val(key,item.second);
      }
    } catch (const std::invalid_argument& ia) {
      fmt::print("ERROR: key {} has an invalid argument {}\n",key);
    }
  }

  console->warn("pass_dfg src:{} lgdb:{} graph_name:{}"
      ,src,lgdb, graph_name);
}
