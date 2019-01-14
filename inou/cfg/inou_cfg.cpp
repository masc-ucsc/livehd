//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <fstream>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <string>
#include <vector>

#include "cfg_node_data.hpp"
#include "inou_cfg.hpp"
#include "lgedgeiter.hpp"

using std::map;
using std::string;
using std::unordered_map;
using std::vector;

Inou_cfg::Inou_cfg() {
}
Inou_cfg::~Inou_cfg() {
}

vector<LGraph *> Inou_cfg::tolg() {
  assert(!opack.name.empty());
  assert(!opack.file.empty());
  unordered_map<std::string, std::string> rename_tab;

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

  vector<LGraph *> lgs;
  lgs.push_back(LGraph::create(opack.path, opack.name, cfg_file));

  cfg_2_lgraph(&memblock, lgs, rename_tab, cfg_file);

  for(LGraph *g : lgs)
    remove_fake_fcall(g);

  for(LGraph *g : lgs)
    g->sync();

  for(const auto &it : rename_tab) {
    fmt::print("Try to rename lgraph!\n");
    fmt::print("original subg_name:{}, new name:{}\n", it.first, it.second);
    LGraph::rename("./" + opack.path, it.first, it.second + "_cfg");
  }
  auto gl = Graph_library::instance("./" + opack.path);
  gl->sync();

  close(fd);
  return lgs;
}

void Inou_cfg::fromlg(vector<const LGraph *> &lgs) {
  assert(false); // Maybe in the future, we can generate a CFG from a cfg lgraph. Still not there
}

void Inou_cfg::cfg_2_lgraph(char **memblock, vector<LGraph *> &lgs, unordered_map<std::string, std::string> &rename_tab, const std::string &source) {
  string                              s;
  vector<map<string, Index_ID>>       nname2nid_lgs(1);  // lgs = graphs
  vector<map<string, vector<string>>> chain_stks_lgs(1); // chain_stacks for every graph, use vector to implement stack
  vector<string>                      nname_begn_lgs(1); // nname = node name, bg = begin
  vector<Index_ID>                    nid_end_lgs(1);
  map<string, uint32_t> n1st2gid;  // map for every sub-graph-gid and its first node name
  map<string, string> n1st2lgname; // map for every sub-graph-name and its first node name
  LGraph *            gtop = lgs[0];

  bool  gtop_begn_nname_recorded = false;
  char *str_ptr                  = nullptr;

  char *   p           = strtok_r(*memblock, "\n\r\f", &str_ptr);

  while(p) {
    vector<string> words = split(p);

    if(*(words.begin()) == "END")
      break;

    string w1st = *(words.begin());
    string w3rd = *(words.begin() + 2);
    string w6th = *(words.begin() + 5);

    uint32_t gsub_id = std::stoi(w3rd);

    if(!gtop_begn_nname_recorded) {
      nname_begn_lgs[0]        = w1st;
      gtop_begn_nname_recorded = true;
    }

    string dfg_data = p;

    if(gsub_id != 0 && gsub_id >= lgs.size()) { // create new sub-graph if different scope id
      LGraph *lg = LGraph::create(opack.path, "sub_method" + std::to_string(gsub_id), source);
      lgs.push_back(lg);

      fmt::print("lgs size:{}\n", lgs.size());
      nname2nid_lgs.resize(nname2nid_lgs.size() + 1);
      chain_stks_lgs.resize(chain_stks_lgs.size() + 1);
      nid_end_lgs.resize(nid_end_lgs.size() + 1);
      nname_begn_lgs.push_back(w1st);
      n1st2gid[w1st]    = gsub_id;
      n1st2lgname[w1st] = "sub_method" + std::to_string(gsub_id);
      build_graph(words, dfg_data, lgs[gsub_id], n1st2gid, n1st2lgname, nname2nid_lgs[gsub_id], chain_stks_lgs[gsub_id], rename_tab, nid_end_lgs[gsub_id]);
    }else if(gsub_id != 0)
      build_graph(words, dfg_data, lgs[gsub_id], n1st2gid, n1st2lgname, nname2nid_lgs[gsub_id], chain_stks_lgs[gsub_id], rename_tab, nid_end_lgs[gsub_id]);
    else
      build_graph(words, dfg_data, gtop, n1st2gid, n1st2lgname, nname2nid_lgs[0], chain_stks_lgs[0], rename_tab, nid_end_lgs[0]);


    fmt::print("\n");
    p = strtok_r(nullptr, "\n\r\f", &str_ptr);
  }

  /*
    create in/out GIO for every graph
  */
   for(uint32_t i = 0; i < lgs.size(); i++) {
    //Graph input
    Node gio_node_begn = lgs[i]->create_node();
    lgs[i]->add_graph_input("ginp", gio_node_begn.get_nid(), 0, 0);
    Index_ID src_nid = gio_node_begn.get_nid();
    Index_ID dst_nid = nname2nid_lgs[i][nname_begn_lgs[i]];
    lgs[i]->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));

    //Graph output
    Node gio_node_ed = lgs[i]->create_node();
    lgs[i]->add_graph_output("gout", gio_node_ed.get_nid(), 0, 0);
    src_nid = nid_end_lgs[i];
    dst_nid = gio_node_ed.get_nid();
    lgs[i]->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
  }

#if 0
   for(size_t i = 0; i < chain_stks_lgs.size(); i++) {
     for(auto &x : chain_stks_lgs[i]) {
       fmt::print("\ncurrent is chain_stks_lgs[{}], vid {} content is\n", i, x.first);
       for(auto j = x.second.rbegin(); j != x.second.rend(); ++j)
         fmt::print("{}\n", *j);
     }
   }
#endif

  update_ifs(lgs, nname2nid_lgs);
}

void Inou_cfg::build_graph(vector<string> &words, string &dfg_data, LGraph *g, map<string, uint32_t> &n1st2gid,
                           map<string, string> &n1st2lgname, map<string, Index_ID> &name2id,
                           map<string, vector<string>> &chain_stks, unordered_map<string, string> &rename_tab, Index_ID &nid_end) {

  fmt::print("dfg_data:{}\n", dfg_data);

  string w1st = *(words.begin());
  string w2nd = *(words.begin() + 1);
  string w3rd = *(words.begin() + 2);
  string w4th = *(words.begin() + 3);
  string w5th = *(words.begin() + 4);
  string w6th = *(words.begin() + 5);
  string w7th = *(words.begin() + 6);
  string w8th = *(words.begin() + 7);
  string w9th; // 2018/11/24
  string w10th;
  if(w6th == "if" || w6th == "::{" || words.size()>=9)
    w9th = *(words.begin() + 8);

  if(w6th == "if")
    w10th = *(words.begin() + 9);

  static string subg_name;
  static bool   has_func_defined;
  // method subgraph renaming
  if(has_func_defined) {
    rename_tab[subg_name] = w7th;
    subg_name             = "";
    has_func_defined      = false;
  }
  /*
    I.process 1st node
    only assign node type for first K in every line of cfg
  */
  if(name2id.count(w1st) == 0) { // if node has not been created before
    Node new_node = g->create_node();
    name2id[w1st] = new_node.get_nid();
    nid_end       = new_node.get_nid(); // keep update the latest final nid
    fmt::print("create node:{}, nid:{}\n", w1st, name2id[w1st]);
    g->node_loc_set(new_node.get_nid(), opack.file.c_str(), (uint32_t)std::stoi(w3rd), (uint32_t)std::stoi(w4th));
  } else
    g->node_loc_set(name2id[w1st], opack.file.c_str(), (uint32_t)std::stoi(w3rd), (uint32_t)std::stoi(w4th));

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
    g->node_subgraph_set(name2id[w1st], n1st2gid[w9th]);
    subg_name        = n1st2lgname[w9th];
    has_func_defined = true;
    fmt::print("set node:{} as subgraph, subg_id is {}\n", name2id[w1st], n1st2gid[w9th]);
  } else
    g->node_type_set(name2id[w1st], CfgAssign_Op);

  g->set_node_wirename(name2id[w1st], CFG_Node_Data(dfg_data).encode().c_str());
  fmt::print("@cfg node:{}, wirename:{}\n", name2id[w1st], CFG_Node_Data(dfg_data).encode().c_str());

  /*
    II-0.process 2nd node and 9th node(if-else merging node)
  */

  if(w6th == "if" && name2id.count(w10th) == 0) { // if node has not been created before
    Node new_node  = g->create_node();
    name2id[w10th] = new_node.get_nid();
    nid_end        = new_node.get_nid(); // keep update the latest final nid
    fmt::print("create node:{}, nid:{}\n", w10th, name2id[w10th]);

    g->node_type_set(name2id[w10th], CfgIfMerge_Op);
  }

  if(w2nd != "null" && name2id.count(w2nd) == 0) {
    Node new_node = g->create_node();
    name2id[w2nd] = new_node.get_nid();
    nid_end       = new_node.get_nid(); // keep update the latest final nid
    fmt::print("create node:{}, nid:{}\n", w2nd, name2id[w2nd]);
  }
  collect_fcall_info(g, name2id[w1st], w7th, w8th, w9th); // collect target and 1st operand for fake fcall analysis later
  /*
    III.deal with edge connection
  */
  Index_ID src_nid, dst_nid;

  if(w6th == "if") {
    // III-1. connect if node to the begin of "true chunk" statement
    src_nid = name2id[w1st];
    dst_nid = name2id[w8th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("if statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);
    // III-2. connect if node to the begin of "false chunk" statement
    if(w9th != "null") {
      src_nid = name2id[w1st];
      dst_nid = name2id[w9th];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 2\n", src_nid, dst_nid);
    }
    // III-3. connect top of stack to end if node
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
    } else { // only one branch of if. EX.  K13  K30   63  169  if   tmp3  K15  null 'K13
      src_nid = name2id[w1st];
      dst_nid = name2id[w10th];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);
    }
    // III-4. if it is an outer if statement, link w10th to w2nd
    if(w2nd != "null") {
      src_nid = name2id[w10th];
      dst_nid = name2id[w2nd];
      g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
      fmt::print("if statement, connect src_node {} to dst_node {} ----- 6\n", src_nid, dst_nid);
    }
    // III-5. figure out which stack w1st is belong to and push w10th into that stack
    for(auto const &x : chain_stks) {
      if(w1st == x.second.back()) {
        chain_stks[x.second[0]].push_back(w10th);
        break;
      }
    }
  } else if(w6th == "for") {
    // I. True: connect for node to body
    src_nid = name2id[w1st];
    dst_nid = name2id[w8th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("for statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
    // II. False: connect for node to next event
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("for statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
  } else if(w6th == "while") {
    // I. connect while node to body
    src_nid = name2id[w1st];
    dst_nid = name2id[w8th];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("while statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);

    // II. False: connect while node to next event
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("while statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
  } else if(w6th == "::{") {
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
  } else if(w2nd == "null") { // no w2nd to create edge, only update chain_stks
    bool   belong_tops   = false;
    string target_vec_id = w1st;

    // check equivalence between src_nid and every top of chain_stks
    for(auto const &x : chain_stks) {
      if(w1st == x.second.back()) {
        belong_tops   = true;
        target_vec_id = x.first;
        break;
      }
    }
    if(!belong_tops)
      chain_stks[target_vec_id].push_back(w1st);

  } else if(w2nd != "null") { // normal edge connection: Kx->Ky, update chain_stks
    src_nid = name2id[w1st];
    dst_nid = name2id[w2nd];
    g->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    fmt::print("normal case connection, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);

    bool   belong_tops   = false;
    string target_vec_id = w1st;
    // check equivalence between src_nid and every top of chain_stks
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
  } // end of edge connection
} // end of build_graph

vector<string> Inou_cfg::split(const string &str) {
  typedef string::const_iterator iter;
  vector<string>                 ret;
  iter                           i = str.begin();

  while(i != str.end()) {
    i = find_if(i, str.end(), not_space);
    // find end of next word
    iter j = find_if(i, str.end(), space);

    // copy the characters in [i,j)
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
      fmt::print("{}\n", g->get_node_wirename(idx)); // for now, just print out cfg, maybe mmap write later
      ++line_cnt;
    }
  }
  fmt::print("END\n");
  ++line_cnt;
  fmt::print("line_cnt = {}\n", line_cnt);
}


void Inou_cfg::update_ifs(vector<LGraph *> &lgs, vector<map<string, Index_ID>> &node_mappings) {
  for(size_t i = 0; i < lgs.size(); i++) {
    LGraph *g       = lgs[i];
    auto &  mapping = node_mappings[i];

    fmt::print("cfg update_ifs\n");
    for(auto idx : g->fast()) {
      CFG_Node_Data data(g, idx);

      if(data.is_br_marker()) {
        const auto &   dops = data.get_operands();
        for(const auto& i: dops)
          fmt::print("dops:{}\n", i);
        vector<string> new_operands(dops.size());

        std::transform(dops.begin(), dops.end(), new_operands.begin(),
                       [&](const string &op) -> string {
          if(op == "null")
            return "0";
          return std::to_string(mapping[(op[0] == '\'') ? op.substr(1) : op]);
        });

        const CFG_Node_Data cnode(data.get_target(), new_operands, std::string(data.get_operator()));
        g->set_node_wirename(idx, cnode.encode().c_str());
      }
    }
  }
}

void Inou_cfg::collect_fcall_info(LGraph *g, Index_ID new_node, const std::string &w7th, const std::string &w8th,
                                  const std::string &w9th) {
  Index_ID pin_node1 = g->get_idx_from_pid(new_node, 1);
  Index_ID pin_node2 = g->get_idx_from_pid(new_node, 2);
  Index_ID pin_node3 = g->get_idx_from_pid(new_node, 3);
  g->set_node_wirename(pin_node1, w7th.c_str());
  g->set_node_wirename(pin_node2, w8th.c_str());
  g->set_node_wirename(pin_node3, w9th.c_str());
}

void Inou_cfg::remove_fake_fcall(LGraph *g) {
  std::set<std::string_view>                func_dcl_tab;
  std::set<std::string_view>                drive_tab;
  std::set<Index_ID>                        true_fcall_tab;
  std::unordered_map<std::string_view, Index_ID> name2id;
  //1st pass
  for(auto idx : g->fast()){
    if(g->node_type_get(idx).op == Invalid_Op)
      break;

    // fmt::print("node:{}, pid names:{},{},{}\n", idx, pid1_wn, pid2_wn, pid3_wn);
    if(g->node_type_get(idx).op == SubGraph_Op) {
      for(const auto &out : g->out_edges(idx)) {
        if(out.get_out_pin().get_pid() == 0) { // src_pid == 0
          Index_ID dst_nid = out.get_idx();
          auto     pid1_wn = g->get_node_wirename(g->get_idx_from_pid(dst_nid, 1));
          func_dcl_tab.insert(pid1_wn);
          fmt::print("push {} into func_dcl_tab\n", pid1_wn);
          break;
        }
      }
    } else if(g->node_type_get(idx).op == CfgFunctionCall_Op) {
      auto pid2_wn = g->get_node_wirename(g->get_idx_from_pid(idx, 2));
      auto pid3_wn = g->get_node_wirename(g->get_idx_from_pid(idx, 3));
      if(pid3_wn.empty()) { // oprd num = 1
        drive_tab.insert(pid2_wn);
        name2id[pid2_wn] = idx;
      } else { // oprd num >=1
        fmt::print("push {} into true_fcall_tab\n", idx);
        true_fcall_tab.insert(idx);
      }
    }
  }
  for(const auto &i : drive_tab) {
    fmt::print("in drive_tab:{}\n", i);
    if(func_dcl_tab.find((i)) != func_dcl_tab.end()) {
      true_fcall_tab.insert(name2id[i]);
      fmt::print("insert nid {} to true fcall table\n", name2id[i]);
    }
  }

  // 2nd pass
  for(auto idx : g->fast()) {
    if(g->node_type_get(idx).op == CfgFunctionCall_Op && true_fcall_tab.find(idx) == true_fcall_tab.end()) {
      g->node_type_set(idx, CfgAssign_Op);
      std::string wn(g->get_node_wirename(idx));
      wn = "=" + wn.substr(3); // FIXME: weird code!!! hardcoding positions? use string_view
      g->set_node_wirename(idx, wn.c_str()); // FIXME: the code should have no c_str()
      fmt::print("find out fake function call!!!!!!\n");
      fmt::print("change idx:{} to CfgAssign_Op\n", idx);
    }
  }
}

void Inou_cfg_options::set(const std::string &key, const std::string &value) {
  try {
    if(is_opt(key, "file"))
      file = value;
    else
      set_val(key, value);

  } catch(const std::invalid_argument &ia) { fmt::print("ERROR: key {} has an invalid argument {}\n", key); }
  Pass::warn("inou_cfg file:{} path:{} name:{}", file, path, name);
}
