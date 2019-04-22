//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <stack>
#include <algorithm>
#include <string>
#include <vector>

#include "cfg_node_data.hpp"
#include "inou_cfg.hpp"
#include "lgedgeiter.hpp"

typedef std::map<std::string, std::vector<std::string>> map_stk;

Inou_cfg::Inou_cfg()= default;

Inou_cfg::~Inou_cfg()= default;

std::vector<LGraph *> Inou_cfg::tolg() {
  assert(!opack.name.empty());
  assert(!opack.file.empty());
  //SH:FIXME:could rename_tab move to static declaration?
  std::unordered_map<std::string, std::string> rename_tab;

  const auto &cfg_file = opack.file;
  int         fd       = open(cfg_file.c_str(), O_RDONLY);
  if(fd < 0) {
    Pass::error("cannot find input file {}", cfg_file);
    exit(-3);
  }
  struct stat sb;
  fstat(fd, &sb);

  char *memblock = (char *)mmap(nullptr, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(memblock == MAP_FAILED) {
    Pass::error("error, mmap failed");
    exit(-3);
  }

  LGraph *g_cfg = LGraph::create(opack.path, opack.name, cfg_file);

  std::vector<LGraph *> lgs;
  lgs.push_back(g_cfg);

  cfg_2_lgraph(&memblock, lgs, rename_tab, cfg_file);

  for(LGraph *g : lgs)
    remove_fake_fcall(g);

  //SH:FIXME: do you really need this? I don't see it makes any sense
  lgs[0] = 0; // Do not clear g_cfg
  for(LGraph *g : lgs) {
    if (g)
      g->close();
  }
  lgs.clear();
  lgs.push_back(g_cfg); // Just keep the new cfg in the return

  //SH:FIXME: it looks like you only rename the top lgraph? what about the subgraphs?
  for(const auto &it : rename_tab) {
    fmt::print("Try to rename lgraph!\n");
    fmt::print("original subg_name:{}, new name:{}\n", it.first, it.second);
    LGraph::rename(opack.path, it.first, it.second);
  }

  close(fd);
  return lgs;
}

void Inou_cfg::fromlg(std::vector<LGraph *> &lgs) {
  assert(false); // Maybe in the future, we can generate a CFG from a cfg lgraph. Still not there
}

void Inou_cfg::cfg_2_lgraph(char **memblock, std::vector<LGraph *> &lgs,
                            std::unordered_map<std::string, std::string> &rename_tab,
                            const std::string &source) {
  //SH:FIXME:could the following variable be declared as static? -> avoid verbose argument passing!
  std::string                                   s;
  std::vector<std::map<std::string, Node>>      name2node_lgs(1);  //lgs = graphs
  //chain_stacks for every graph, use vector to implement stack
  std::vector<map_stk>                          branch_chain_stacks_lgs(1);
  std::vector<std::string>                      first_node_name_lgs(1); //nname = node name, begn = begin
  std::vector<Node>                             final_node_lgs(1);
  std::map<std::string, uint32_t>               n1st2gid;    //map for every sub-graph-gid and its first node name
  std::map<std::string, std::string>            n1st2lgname; //map for every sub-graph-name and its first node name

  LGraph*                             gtop = lgs[0];

  bool  gtop_begn_node_recorded = false;
  char *str_ptr                 = nullptr;
  char *p                       = strtok_r(*memblock, "\n\r\f", &str_ptr);

  while(p) {
    std::vector<std::string> words = split(p);

    if(*(words.begin()) == "END")
      break;

    std::string w1st = *(words.begin());
    std::string w3rd = *(words.begin() + 2);
    std::string w6th = *(words.begin() + 5);

    auto subgraph_id = (uint32_t)std::stoi(w3rd);

    if(!gtop_begn_node_recorded) {
      first_node_name_lgs[0]        = w1st;
      gtop_begn_node_recorded = true;
    }

    std::string dfg_data = p;

    if(subgraph_id != 0 && subgraph_id >= lgs.size()) { // create new sub-graph if different scope id
      LGraph *lg = LGraph::create(opack.path, "sub_method" + std::to_string(subgraph_id), source);
      lgs.push_back(lg);
      //SH:FIXME: check the resize v.s. emplace_back[null]
      name2node_lgs.resize(name2node_lgs.size() + 1);
      branch_chain_stacks_lgs.resize(branch_chain_stacks_lgs.size() + 1);
      final_node_lgs.resize(final_node_lgs.size() + 1);
      first_node_name_lgs.push_back(w1st);
      n1st2gid[w1st]    = lg->lg_id();
      n1st2lgname[w1st] = "sub_method" + std::to_string(subgraph_id);
      build_graph(words, dfg_data, lgs[subgraph_id], n1st2gid, n1st2lgname, name2node_lgs[subgraph_id], branch_chain_stacks_lgs[subgraph_id], rename_tab, final_node_lgs[subgraph_id]);
    } else if(subgraph_id != 0){
      build_graph(words, dfg_data, lgs[subgraph_id], n1st2gid, n1st2lgname, name2node_lgs[subgraph_id], branch_chain_stacks_lgs[subgraph_id], rename_tab, final_node_lgs[subgraph_id]);
    } else {
      build_graph(words, dfg_data, gtop, n1st2gid, n1st2lgname, name2node_lgs[0], branch_chain_stacks_lgs[0], rename_tab, final_node_lgs[0]);
    }

    fmt::print("\n");
    p = strtok_r(nullptr, "\n\r\f", &str_ptr);
  }


   // create in/out GIO for every graph
   for(long unsigned int i = 0; i < lgs.size(); i++) {
    //Graph input
    auto ipin = lgs[i]->add_graph_input("cfg_inp", 0, 0);
    auto dst_node = name2node_lgs[i][first_node_name_lgs[i]];
    I(dst_node.get_type().op != GraphIO_Op);
    lgs[i]->add_edge(ipin, dst_node.setup_sink_pin(0));

    //Graph output
    auto opin = lgs[i]->add_graph_output("cfg_out", 0, 0);
    auto src_node = final_node_lgs[i];
    lgs[i]->add_edge(src_node.setup_driver_pin(0), opin);
  }

  //update_ifs(lgs, name2node_lgs);
}

void Inou_cfg::build_graph(std::vector<std::string> &words, std::string &dfg_data, LGraph *g,
                           std::map<std::string, uint32_t>    &n1st2gid,
                           std::map<std::string, std::string> &n1st2lgname,
                           std::map<std::string, Node>        &name2node,
                           std::map<std::string, std::vector<std::string>> &branch_chain_stacks,
                           std::unordered_map<std::string, std::string> &rename_tab,
                           Node &final_node) {

  fmt::print("dfg_data:{}\n", dfg_data);

  std::string w1st = *(words.begin());
  std::string w2nd = *(words.begin() + 1);
  std::string w3rd = *(words.begin() + 2);
  std::string w4th = *(words.begin() + 3);
  std::string w5th = *(words.begin() + 4);
  std::string w6th = *(words.begin() + 5);
  std::string w7th = *(words.begin() + 6);
  std::string w8th = *(words.begin() + 7);
  std::string w9th; // 2018/11/24
  std::string w10th;
  if(w6th == "if" || w6th == "::{" || words.size()>=9)
    w9th = *(words.begin() + 8);

  if(w6th == "if")
    w10th = *(words.begin() + 9);

  static std::string subg_name;
  static bool        has_func_defined;
  // method subgraph renaming
  if(has_func_defined) {
    rename_tab[subg_name] = w7th + "_cfg";
    subg_name             = "";
    has_func_defined      = false;
  }

  //I. process 1st node, only assign node type for the first "K" in every line of cfg
  if(name2node.count(w1st) == 0) { // if node has not been created before
    name2node[w1st] = g->create_node();
    name2node[w1st].setup_driver_pin(0).set_name(w1st);
    final_node      = name2node[w1st]; // keep update the latest final nid

    fmt::print("create node:{}\n", w1st);
    //SH:FIXME: move to new attribute
    //g->node_loc_set(name2node[w1st], opack.file.c_str(), (uint32_t)std::stoi(w3rd), (uint32_t)std::stoi(w4th));
  } else {
    //SH:FIXME: move to new attribute
    //g->node_loc_set(name2node[w1st], opack.file.c_str(), (uint32_t)std::stoi(w3rd), (uint32_t)std::stoi(w4th));
  }

  I(name2node.count(w1st) > 0);

  if(w6th == ".()")
    name2node[w1st].set_type(CfgFunctionCall_Op);
  else if(w6th == "for")
    name2node[w1st].set_type(CfgFor_Op);
  else if(w6th == "while")
    name2node[w1st].set_type(CfgWhile_Op);
  else if(w6th == "if")
    name2node[w1st].set_type(CfgIf_Op);
  else if(w6th == "RD")
    name2node[w1st].set_type(CfgBeenRead_Op);
  else if(w6th == "::{") {
    name2node[w1st].set_type_subgraph(n1st2gid[w9th]);
    subg_name        = n1st2lgname[w9th];
    has_func_defined = true;
    fmt::print("set node:{} as subgraph, subgraph_id:{}\n", w1st, n1st2gid[w9th]);
  } else {
    name2node[w1st].set_type(CfgAssign_Op);
  }

  name2node[w1st].set_name(CFG_Node_Data(dfg_data).encode().c_str());
  fmt::print("node:{}, data:{}\n", w1st, CFG_Node_Data(dfg_data).encode().c_str());


  //II-0. process 10th word(if-else merging node)
  if(w6th == "if" && name2node.count(w10th) == 0) { // if node has not been created before
    name2node[w10th] = g->create_node();
    name2node[w10th].setup_driver_pin(0).set_name(w10th);
    final_node        = name2node[w10th]; // keep update the latest final nid
    fmt::print("create node:{}\n", w10th);
    name2node[w10th].set_type(CfgIfMerge_Op);
  }
  //II-1. process 2nd word
  if(w2nd != "null" && name2node.count(w2nd) == 0) {
    name2node[w2nd] = g->create_node();
    name2node[w2nd].setup_driver_pin(0).set_name(w2nd);
    final_node       = name2node[w2nd]; // keep update the latest final nid
    fmt::print("create node:{}\n", w2nd);
  }
  //collect target and 1st operand for fake function call analysis later
  if(name2node[w1st].get_type().op == CfgFunctionCall_Op)
    collect_fcall_info(g, name2node[w1st], w7th, w8th, w9th);
  else if(name2node[w1st].get_type().op == SubGraph_Op)
    collect_fcall_info(g, name2node[w1st], w7th, w8th, w9th);


  //III.deal with edge connection
  Node_pin dpin, spin;

  //process if statement
  if(w6th == "if") {
    // III-1. connect if node to the begin of "true chunk" statement
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w8th].setup_sink_pin(0);
    g->add_edge(dpin, spin);
    fmt::print("if statement:T, connect {}->{}\n", w1st, w8th);

    // III-2. connect if node to the begin of "false chunk" statement
    if(w9th != "null") {
      dpin = name2node[w1st].setup_driver_pin(1);
      spin = name2node[w9th].setup_sink_pin(0);
      g->add_edge(dpin, spin);
      fmt::print("if statement:F, connect {}->{}\n", w1st, w9th);
    }
    // III-3. connect top of stack to end if node
    I(branch_chain_stacks[w8th].back() != w10th);
    if(branch_chain_stacks[w8th].back() != w10th) {
      dpin = name2node[branch_chain_stacks[w8th].back()].setup_driver_pin(0);
      fmt::print("branch_chain_stacks[w8th] back{}\n", branch_chain_stacks[w8th].back());
      spin = name2node[w10th].setup_sink_pin(0);
      g->add_edge(dpin, spin);
      fmt::print("connect end of T branch to phi node {}->{}\n", branch_chain_stacks[w8th].back(), w10th);
    }
    if(w9th != "null") {
      I(branch_chain_stacks[w9th].back() != w10th);
      if(branch_chain_stacks[w9th].back() != w10th) {
        dpin = name2node[branch_chain_stacks[w9th].back()].setup_driver_pin(0);
        fmt::print("branch_chain_stacks[w9th] back{}\n", branch_chain_stacks[w9th].back());
        spin = name2node[w10th].setup_sink_pin(0);
        g->add_edge(dpin, spin);
        fmt::print("connect end of F branch to phi node {}->{}\n", branch_chain_stacks[w8th].back(), w10th);
      }
    } else { // only one branch of if. EX.  K13  K30  0  63  169  if ___g  K15  null 'K13
      dpin = name2node[w1st].setup_driver_pin(1);
      spin = name2node[w10th].setup_sink_pin(0);
      g->add_edge(dpin, spin);
      fmt::print("if statement, no false branch, connect {}->{}\n", w1st, w10th);
    }

    // III-4. if it is an outer if statement, link w10th to w2nd
    if(w2nd != "null") {
      dpin = name2node[w10th].setup_driver_pin(0);
      spin = name2node[w2nd].setup_sink_pin(0);
      g->add_edge(dpin, spin);
      fmt::print("if statement, connect driver_node:{} -> sink_node:{} ----- 6\n", w10th, w2nd);
    }
    // III-5. figure out the stack that w1st is belong to and push w10th into that stack
    for(auto const &x : branch_chain_stacks) {
      if(w1st == x.second.back()) {
        branch_chain_stacks[x.second[0]].push_back(w10th);
        break;
      }
    }
  //process for_loop statement
  } else if(w6th == "for") { //SH:FIXME:wait for pyrope for_loop feature development
    // I. True: connect for node to body
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w8th].setup_sink_pin(0);
    g->add_edge(dpin, spin);
    fmt::print("for statement, connect driver_node:{} -> sink_node:{}\n", w1st, w8th);
    // II. False: connect for node to next event
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w2nd].setup_sink_pin(0);
    g->add_edge(dpin, spin);
    fmt::print("for statement, connect driver_node:{} -> sink_node:{}\n", w1st, w2nd);
  //process while_loop statement
  } else if(w6th == "while") { //SH:FIXME:wait for pyrope while_loop feature development
    // I. connect while node to body
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w8th].setup_sink_pin(0);
    g->add_edge(dpin, spin);
    fmt::print("while statement, connect driver_node:{} -> sink_node:{}\n", w1st, w8th);

    // II. False: connect while node to next event
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w2nd].setup_sink_pin(0);
    g->add_edge(dpin, spin);
    fmt::print("while statement, connect driver_node:{} -> sink_node:{}\n", w1st, w2nd);
  } else if(w6th == "::{") {
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w2nd].setup_sink_pin(0);
    g->add_edge(dpin, spin);
  } else if(w2nd == "null") { // no w2nd to create edge, only update branch_chain_stacks
    bool   belong_tops   = false;
    std::string target_vec_id = w1st;

    // check equivalence between src_nid and every top of branch_chain_stacks
    for(auto const &x : branch_chain_stacks) {
      if(w1st == x.second.back()) {
        belong_tops   = true;
        target_vec_id = x.first;
        break;
      }
    }
    if(!belong_tops)
      branch_chain_stacks[target_vec_id].push_back(w1st);

  } else if(w2nd != "null") { // normal edge connection: Kx->Ky, update branch_chain_stacks
    dpin = name2node[w1st].setup_driver_pin(0);
    spin = name2node[w2nd].setup_sink_pin(0);
    g->add_edge(dpin, spin);
    fmt::print("normal case connection, connect driver_node:{} -> sink_node:{} ----- 5\n", w1st, w2nd);

    bool   belong_tops   = false;
    std::string target_vec_id = w1st;
    // check equivalence between src_nid and every top of branch_chain_stacks
    for(auto const &x : branch_chain_stacks) {
      if(w1st == x.second.back()) {
        belong_tops   = true;
        target_vec_id = x.first;
        break;
      }
    }

    if(!belong_tops) {
      branch_chain_stacks[target_vec_id].push_back(w1st);
      branch_chain_stacks[target_vec_id].push_back(w2nd);
    } else
      branch_chain_stacks[target_vec_id].push_back(w2nd);
  } // end of edge connection
} // end of build_graph
//


std::vector<std::string> Inou_cfg::split(const std::string &str) {
  typedef std::string::const_iterator iter;
  std::vector<std::string>            ret;
  iter i = str.begin();

  while(i != str.end()) {
    i = find_if(i, str.end(), not_space);
    // find end of next word
    iter j = find_if(i, str.end(), space);

    // copy the characters in [i,j)
    if(i != str.end())
      ret.emplace_back(std::string(i, j));

    i = j;
  }
  return ret;
}

void Inou_cfg::lgraph_2_cfg(LGraph *g, const std::string &filename) {
  int line_cnt = 0;
  for(auto nid : g->fast()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    if(node.get_name() != nullptr) {
      fmt::print("{}\n", node.get_name()); // for now, just print out cfg, maybe mmap write later
      ++line_cnt;
    }
  }
  fmt::print("END\n");
  ++line_cnt;
  fmt::print("line_cnt = {}\n", line_cnt);
}

#if 1
//it just want to transform "null" to "0" as an indicator for DFG
//however, I think I could use the "null" directly now
void Inou_cfg::update_ifs(std::vector<LGraph *> &lgs, std::vector<std::map<std::string, Node>> &node_mappings) {
  for(size_t i = 0; i < lgs.size(); i++) {
    LGraph *g       = lgs[i];
    auto &  mapping = node_mappings[i];

    fmt::print("cfg update_ifs\n");
    for(auto nid : g->fast()) {
      auto node = Node(g, 0, Node::Compact(nid));
      CFG_Node_Data data(g, node);
      if(data.is_br_marker()) {
        const auto &   operands = data.get_operands();
        for(const auto& j: operands)
          fmt::print("operands:{}\n", j);
        std::vector<std::string> new_operands(operands.size());

        //lambda usage for std::transform
        std::transform(operands.begin(), operands.end(), new_operands.begin(),
          [&](const std::string &operand) -> std::string_view {
          if(operand == "null")
            return "0";
          else {
            auto key = (operand[0] == '\'') ? operand.substr(1) : operand;
            //return std::to_string(mapping[key]);
            return mapping[key].get_name();
          }
        });

        const CFG_Node_Data cnode(data.get_target(), new_operands, std::string(data.get_operator()));
        node.set_name(cnode.encode().c_str());
      }
    }
  }
}
#endif

void Inou_cfg::collect_fcall_info(LGraph *g, Node new_node, const std::string &w7th,
                                                            const std::string &w8th,
                                                            const std::string &w9th) {
  new_node.setup_driver_pin(1).set_name(w7th);
  new_node.setup_driver_pin(2).set_name(w8th);
  new_node.setup_driver_pin(3).set_name(w9th);
}

void Inou_cfg::remove_fake_fcall(LGraph *g) {
  std::set<std::string_view>                 func_dcl_tab;
  std::set<std::string_view>                 drive_tab;
  std::set<Node::Compact>                    true_fcall_tab;
  //SH:FIXME:change to absl::map
  std::unordered_map<std::string_view, Node> name2node;
  //1st pass
  for(auto nid : g->fast()){
    auto node = Node(g, 0, Node::Compact(nid));
    if(node.get_type().op == Invalid_Op)
      break;

    if(node.get_type().op == SubGraph_Op) {
      auto pid1_name = node.get_driver_pin(1).get_name();
      func_dcl_tab.insert(pid1_name);
    } else if(node.get_type().op == CfgFunctionCall_Op) {
      auto pid2_name = node.get_driver_pin(2).get_name();
      auto pid3_name = node.get_driver_pin(3).get_name();
      if(pid3_name.empty()) { // oprd num = 1
        drive_tab.insert(pid2_name);
        name2node[pid2_name] = node;
      } else { // operand number >=1
        auto node_name = node.get_name();
        //SH:FIXME:check correctness of the following debug message
        fmt::print("push node:{} into true_fcall_tab\n", node_name.substr(0, node_name.find(" ")));
        true_fcall_tab.insert(node.get_compact());
      }
    }
  }

  for(const auto &it : drive_tab) {
    fmt::print("in drive_tab:{}\n", it);
    if(func_dcl_tab.find((it)) != func_dcl_tab.end()) {
      true_fcall_tab.insert(name2node[it].get_compact());
      auto node_name = name2node[it].get_name();
      fmt::print("push node:{} into true_fcall_tab\n", node_name.substr(0, node_name.find(" ")));
    }
  }

  // 2nd pass
  for(auto nid : g->fast()) {
    auto node = Node(g, 0, Node::Compact(nid));
    if(node.get_type().op == CfgFunctionCall_Op && true_fcall_tab.find(node.get_compact()) == true_fcall_tab.end()) {
      node.set_type(CfgAssign_Op);
      std::string tmp_name(node.get_name().substr(3));
      //SH:FIXME: could you avoid hardcoding positions? also try to use string_view
      std::string new_node_name = "=" + tmp_name;
      node.set_name(new_node_name);
      fmt::print("find out fake function call!!!!!!\n");
      fmt::print("change node:{} to CfgAssign_Op\n", new_node_name.substr(0, new_node_name.find(" ")));
    }
  }
}

//SH:FIXME:this is the old style argument passing, change it.
void Inou_cfg_options::set(const std::string &key, const std::string &value) {
  try {
    if(is_opt(key, "files"))
      file = value;
    else
      set_val(key, value);

  } catch(const std::invalid_argument &ia) { fmt::print("ERROR: key {} has an invalid argument {}\n", key); }
  Pass::warn("inou_cfg file:{} path:{} name:{}", file, path, name);
}
