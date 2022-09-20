//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "traverse_lg.hpp"

#include <iostream>
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "perf_tracing.hpp"
#include <fstream>
#include <utility>
#include <string>
#include <algorithm>

#define VISITED_COLORED 401
static Pass_plugin sample("traverse_lg", Traverse_lg::setup);

void Traverse_lg::setup() {
  Eprp_method m1("inou.traverse_lg", "Prints all nodes (and each of their IOs) of LG", &Traverse_lg::travers);
  //m1.add_label_optional("odir", "path to print the text to", ".");
  register_pass(m1);
}

Traverse_lg::Traverse_lg(const Eprp_var& var) : Pass("inou.traverse_lg", var) {}

void Traverse_lg::travers(Eprp_var& var) {
  //TRACE_EVENT("inou", "LNAST_FROMLG_trans");
  TRACE_EVENT("inou","traverse_lg");
  Lbench b("traverse_lg");

  Traverse_lg p(var);

  fmt::print("\nLGs : {}\n",var.lgs.size());
  I(var.lgs.size()==2,"\n\nINPUT ERROR:\n\t provide exactly 2 lgraphs to inou.traverse_lg. CHECK: The 2 lgraphs provided to inou.traverse_lg should be of different names. First LG will be the pre-synth LG and Second LG should be the one from synth netlist.\n\n");

#ifdef DE_DUP
  //Traverse_lg::setMap map_pre_synth;
  Traverse_lg::setMap_pairKey map_post_synth;
  p.do_travers(var.lgs.back(), map_post_synth);//synth LG
  p.do_travers(var.lgs.front(), map_post_synth);//original LG (pre-syn LG)
#endif

#ifdef DEBUG
  for (const auto& l : var.lgs) {
    p.do_travers(l);
  }
#endif

}

//FOR DEBUG:
void Traverse_lg::do_travers(Lgraph* lg) {
  //std::string_view module_name = lg->get_name();
  std::ofstream ofs;
  ofs.open(std::string(lg->get_name()), std::ofstream::out | std::ofstream::trunc);
  if(!ofs.is_open()) {
    fmt::print("unable to open file {}.txt\n",lg->get_name());
    return ;
  }
  ofs<<std::endl<<std::endl<<lg->get_name()<<std::endl<<std::endl<<std::endl;

  for (const auto& node : lg->forward(true)) {
    ofs<<"===============================\n";
    ofs<<node.debug_name()<<std::endl; //this is the main node for which we will record IO
    if (! (node.has_inputs() || node.has_outputs())) {//no i/ps as well as no o/ps
      ofs<<"---------------\n";
      continue; //do not keep such nodes in nodeIOmap
    }

    
    if (node.has_inputs()){
    ofs<<"INPUTS:\n";
      for (const auto& indr : node.inp_drivers()) {
        if(node.get_type_op()==Ntype_op::AttrSet && indr.get_node().get_type_op()==Ntype_op::Const) {continue;}
        //auto inp_node = indr.get_node();
        get_input_node(indr, ofs);
      }
    }

    if (node.has_outputs()) {
    ofs<<"OUTPUTS:\n";
      for (const auto& outdr : node.out_sinks() ) {//outdr is the pin of the output node.
        //auto out_node = outdr.get_node();
        get_output_node(outdr,ofs);
      }
    }

  }//enf of for lg-> traversal

  if(ofs.is_open()) {
    ofs.close();
  }
}


void Traverse_lg::get_input_node(const Node_pin &node_pin, std::ofstream& ofs) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_type_const() || node.is_graph_input() ) {
    if(node.is_graph_io()) {
      ofs<<node_pin.get_pin_name()<<std::endl;
    } else {
      ofs<<node.get_type_name();
      //if(node.is_type_const()){ ofs<<":"<<node.get_type_const().to_pyrope();}
      //else 
      if(node.is_type_flop()){ ofs<<":"<<node_pin.get_pin_name()<<"->"<<node.get_driver_pin().get_wire_name();}
      ofs<<std::endl;
    }
    return;
  } else {
      for (const auto& indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        get_input_node(indr,ofs);
      }
  }
}

void Traverse_lg::get_output_node(const Node_pin &node_pin, std::ofstream& ofs) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_graph_output() ) {
    if(node.is_graph_io()) {
      ofs<<node_pin.get_pin_name()<<std::endl;
    } else {
      ofs<<node.get_type_name();
      //if(node.is_type_const()){ ofs<<":"<<node.get_type_const().to_pyrope();}
      //else 
      if(node.is_type_flop()){ ofs<<":"<<node_pin.get_pin_name()<<"->"<<node.get_driver_pin().get_wire_name();}
      ofs<<std::endl;
    }
    return;
  } else {
      for (const auto& outdr : node.out_sinks() ) {
        //auto out_node = outdr.get_node();
        get_output_node(outdr,ofs);
      }
  }
}

//FOR SET:
//DE_DUP
void Traverse_lg::do_travers(Lgraph* lg, Traverse_lg::setMap_pairKey &nodeIOmap) {

  bool do_matching=false; //for post syn graph, make map. do not match map!
  if (!nodeIOmap.empty()) {
    do_matching=true; //now we have pre-syn graph and post-syn map ready. 
  } // if do_matching is false then the lg is post-syn-LG
 
  bool req_flops_matched = false;
  bool dealing_flop=false;
  bool dealing_comb=false;

  for (const auto & node : lg->forward(true)) {
    dealing_flop=false;
    dealing_comb=false;
    // absl::btree_set<std::string> in_set;
    // absl::btree_set<std::string> out_set;
    std::set<std::string> in_set;
    std::set<std::string> out_set;
    std::set<std::string> io_set;
    std::set<std::string> in_comb_set;
    std::set<std::string> out_comb_set;
    std::set<std::string> io_comb_set;
    fmt::print("{}\n", node.debug_name());
    
    /* For post syn LG -> if the node is flop then calc all IOs in in_set and out_set and keep in map*/
    if (node.is_type_flop() || (node.is_type_sub()?((std::string(node.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false)) {
      dealing_flop=true;
      for (const auto & indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        get_input_node(indr, in_set, io_set);
      }
      for (const auto & outdr : node.out_sinks() ) {//outdr is the pin of the output node.
        //auto out_node = outdr.get_node();
        get_output_node(outdr, out_set, io_set);
      }

      /*add to crit_flop_list if !do_matching and flop node is colored*/
      if (!do_matching && node.has_color() ) {
        auto colr = node.get_color();
        crit_flop_list.emplace_back(node.get_compact_flat());
        crit_flop_map[node.get_compact_flat()] = colr;
        fmt::print("\t{}\n",colr); 
      }
      
    } else { /*else if node is in crit_cell_list then keep its IO in cellIOMap_synth*/
      dealing_comb=true;
      /*add to crit_cell_list if !do_matching and cell node is colored*/
      if (!do_matching && node.has_color()) { 
        auto node_val = node.get_compact_flat();
        auto colr = node.get_color();
        crit_cell_list.emplace_back(node_val);
        crit_cell_map[node.get_compact_flat()] = colr;
        fmt::print("\t{}\n",colr); 
      
        //calc node's IO
        for (const auto & indr : node.inp_drivers()) {
          get_input_node(indr, in_comb_set, io_comb_set, true);
        }
        for (const auto & outdr : node.out_sinks() ) {
          get_output_node(outdr, out_comb_set, io_comb_set, true);
        }
        
        //insert this node IO in the map
      }
    }

    if (in_set.empty() && out_set.empty() && in_comb_set.empty() && out_comb_set.empty()) {//no i/ps as well as no o/ps
      continue;//do not keep such nodes in nodeIOmap or cellIOMap_synth
    }
    
    //print the set formed
    fmt::print("INPUTS:\n");
    if (dealing_flop) {
      for (const auto & i:in_set) {
        fmt::print("\t{}\n",i);
      } 
    } else {
      for (const auto & i:in_comb_set) {
        fmt::print("\t{}\n",i);
      } 
    }
    fmt::print("OUTPUTS:\n");
    if (dealing_flop) {
      for (const auto & i:out_set) {
        fmt::print("\t{}\n",i);
      }
    } else {
      for (const auto & i:out_comb_set) {
        fmt::print("\t{}\n",i);
      }
    }
    fmt::print("IOs:\n");
    if (dealing_flop) {
      for (const auto & i:io_set) {
        fmt::print("\t{}\n",i);
      }
    } else {
      for (const auto & i:io_comb_set) {
        fmt::print("\t{}\n",i);
      }
    }

    if(!do_matching) {
      //insert in map
      const auto& nodeid = node.get_compact_flat();
      std::vector<Node::Compact_flat> tmpVec;
      if (dealing_flop){/*dealing with flops*/
        if(nodeIOmap.find(std::make_pair(in_set, out_set)) != nodeIOmap.end()) {
          tmpVec.assign((nodeIOmap[std::make_pair(in_set, out_set)]).begin() , (nodeIOmap[std::make_pair(in_set, out_set)]).end() );
          tmpVec.emplace_back(nodeid);
        } else {
          tmpVec.emplace_back(nodeid);
        }
        nodeIOmap[std::make_pair(in_set,out_set)] = tmpVec;//FIXME: make hash of set and change datatype accordingly
      } else if (dealing_comb) {/*dealing with combinational*/
        if(cellIOMap_synth.find(std::make_pair(in_comb_set, out_comb_set)) != cellIOMap_synth.end()) {
          tmpVec.assign((cellIOMap_synth[std::make_pair(in_comb_set, out_comb_set)]).begin() , (cellIOMap_synth[std::make_pair(in_comb_set, out_comb_set)]).end() );
          tmpVec.emplace_back(nodeid);
        } else {
          tmpVec.emplace_back(nodeid);
        }
        cellIOMap_synth[std::make_pair(in_comb_set,out_comb_set)] = tmpVec;//FIXME: make hash of set and change datatype accordingly
      } else {I(false,"not possible to enter this part! node is neither combo nor seq!? Debug! Check!");}

      //the IOtoNodeMap_synth making:
      if(dealing_flop && !io_set.empty()) {
        std::vector<Node::Compact_flat> tempnodeVec;
        setMap_pairKey internalMap;
        if(IOtoNodeMap_synth.find(io_set) != IOtoNodeMap_synth.end()) {
          //founf IO, insert in internal map against this location.
          internalMap=IOtoNodeMap_synth[io_set];
          if(internalMap.find(std::make_pair(in_set, out_set)) != internalMap.end()) {
            tempnodeVec.assign((internalMap[std::make_pair(in_set, out_set)]).begin() , (internalMap[std::make_pair(in_set, out_set)]).end() );
            tempnodeVec.emplace_back(nodeid);
          } else {
            tempnodeVec.emplace_back(nodeid);
          }
          internalMap[std::make_pair(in_set,out_set)] = tempnodeVec;

        } else {
          //make a new entry in the internal map 
          tempnodeVec.emplace_back(nodeid);
          internalMap[std::make_pair(in_set,out_set)] = tempnodeVec;
        }
          IOtoNodeMap_synth[io_set] = internalMap;//FIXME: make hash of set and change datatype accordingly
      }

    }

    if(do_matching && !req_flops_matched) {
      /*if orig graph IO-set pair is as-is found in the synth nodeIOmap, then it is direct match!*/
      if(nodeIOmap.find(std::make_pair(in_set, out_set)) != nodeIOmap.end()) {
        //put this in matching_map.
        matched_map[node.get_compact_flat()]=nodeIOmap[std::make_pair(in_set, out_set)];//FIXME:put this in matching_map. no need of matched_map then
        //const auto & synNode = nodeIOmap[std::make_pair(in_set, out_set)];
        //std::vector<Node::Compact_flat> tmpVec;
        //if(matching_map.find(synNode) != matching_map.end()) {
        //  tmpVec.assign((matching_map[synNode]).begin() , (matching_map[synNode]).end() );
        //  tmpVec.emplace_back(node.get_compact_flat());
        //} else {
        //  tmpVec.emplace_back(node.get_compact_flat());
        //}
        //matching_map[synNode]=tmpVec;
        ///*if synNode in crit_flop_list, remove from crit_flop_list;*/
        //for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
        //  if(*cfl_it == synNode) {
        //    crit_flop_list.erase(cfl_it); 
        //    cfl_it--;
        //  }
        //}
        ///*if synNode in crit_flop_map, remove the entry from crit_flop_map*/
        //if (crit_flop_list.empty()){ req_flops_matched = true;}
        //if (crit_flop_map.find(synNode)!=crit_flop_map.end()) {
        //  //if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
        //  for (const auto & o_n: matching_map[synNode]) {
        //    matched_color_map[o_n]=crit_flop_map[synNode];
        //  }
        //}

      } else {
        unmatched_map[node.get_compact_flat()]=std::make_pair(in_set, out_set);//FIXME: (no more needed; kept for debug purposes) (old fixme msg: this won't be needed once matched map is removed and entries put in matching_map are erased from the main maps!
      }
      /*insert in full_orig_map (against same set of IOs)*/
      const auto& nodeid = node.get_compact_flat();
      std::vector<Node::Compact_flat> tmpVec;
      if(full_orig_map.find(std::make_pair(in_set, out_set)) != full_orig_map.end()) {
        tmpVec.assign((full_orig_map[std::make_pair(in_set, out_set)]).begin() , (full_orig_map[std::make_pair(in_set, out_set)]).end() );
        tmpVec.emplace_back(nodeid);
      } else {
        tmpVec.emplace_back(nodeid);
      }
      full_orig_map[std::make_pair(in_set,out_set)] = tmpVec;//FIXME: make hash of set and change datatype accordingly
      /*IOtoNodeMap_orig insertion*/
      std::vector<Node::Compact_flat> tempVec;
      if(IOtoNodeMap_orig.find(io_set) != IOtoNodeMap_orig.end()) {
        tempVec.assign((IOtoNodeMap_orig[io_set]).begin() , (IOtoNodeMap_orig[io_set]).end() );
        tempVec.emplace_back(nodeid);
      } else {
        tempVec.emplace_back(nodeid);
      }
      if(!io_set.empty()) {
        IOtoNodeMap_orig[io_set] = tempVec;//FIXME: make hash of set and change datatype accordingly
      }
    }//end of if(do_matching)

  }//enf of for lg-> traversal

  if(!do_matching) {
    /*for the sequential part:*/ 
    I(!nodeIOmap.empty(), "\n\nDEBUG?? \tNO FLOP IN THE SYNTHESISED DESIGN\n\n");
    //print the map
    fmt::print("\n\nMAP FORMED IS:\n");
    for(const auto & [ioPair, n_list]: nodeIOmap) {
      for (const auto & ip: ioPair.first) {
        fmt::print("{}\t",ip);
      }
      fmt::print("||| \t");
      for (const auto & op:ioPair.second) {
        fmt::print("{}\t", op);
      }
      fmt::print("::: \t");
      for (const auto & n:n_list) {
        fmt::print("{}\t", n.get_nid());
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nIOtoNodeMap_synth MAP FORMED IS:\n");
    print_IOtoNodeMap_synth(IOtoNodeMap_synth);
    fmt::print("\n\n\n");
    /*for the combo part:*/ 
    //print the cellIOMap_synth
    fmt::print("\n\nthe cellIOMap_synth FORMED IS:\n");
    print_MapOf_SetPairAndVec(cellIOMap_synth);
  } else if(do_matching) { //do_matching
    fmt::print("\n\nThe IOtoNodeMap_orig map is:\n");
    for(const auto& [iov,fn]: IOtoNodeMap_orig) {
      for (const auto & ip: iov) {
        fmt::print("{}\t",ip);
      }
      fmt::print("::: \t");
      for (const auto & op:fn) {
        fmt::print("{}\t", op.get_nid());
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nThe complete orig map (full_orig_map) is:\n");
    print_MapOf_SetPairAndVec(full_orig_map) ;
    fmt::print("\n\n===============================\n");

    fmt::print("\n\n The unmatched flops are:\n");
    for(const auto& [fn,iov]: unmatched_map) {
      fmt::print("{}\n",fn.get_nid());
      for (const auto& ip: iov.first) {
        fmt::print("{}\t",ip);
      }
      fmt::print("||| \t");
      for (const auto& op:iov.second) {
        fmt::print("{}\t", op);
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nmatched_map (matching done is):\n");
    for(const auto& [k, n_list]: matched_map) {
      fmt::print("{}\t", k.get_nid());
      fmt::print("::: \t");
      for (const auto& n:n_list) {
        fmt::print("{}\t", n.get_nid());
      }
      fmt::print("\n");
    }
    fmt::print("\n\n===============================\n");
  }

  if(do_matching && !req_flops_matched) {
    /*doing the actual matching here*/ //(pass_1)
    
    //for(const auto& [iov,fn]: IOtoNodeMap_orig) 
    for (absl::node_hash_map<std::set<std::string>, std::vector<Node::Compact_flat> >::iterator it = IOtoNodeMap_orig.begin(); it!= IOtoNodeMap_orig.end();) {
      if (req_flops_matched) {break;}
      auto iov = it->first;
      auto fn = it->second;
      if(fn.size()!=1) {
        ++it;
        continue;
      }
      auto orig_node = fn.front();
      //go to the best match in IOtoNodeMap_synth
      if(IOtoNodeMap_synth.find(iov)!=IOtoNodeMap_synth.end()) {
        for (const auto& [k,synNodes]: IOtoNodeMap_synth[iov]) {
          for (const auto& synNode: synNodes) {
            //std::vector<Node::Compact_flat> vc = synNode;
            /*inserting in matching_map*/
            //const auto& nodeid = node.get_compact_flat();
            std::vector<Node::Compact_flat> tmpVec;
            if(matching_map.find(synNode) != matching_map.end()) {
              tmpVec.assign((matching_map[synNode]).begin() , (matching_map[synNode]).end() );
              tmpVec.emplace_back(orig_node);
            } else {
              tmpVec.emplace_back(orig_node);
            }
            matching_map[synNode]=tmpVec;
            /*if synNode in crit_flop_list, remove from crit_flop_list;*/
            for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
              if(*cfl_it == synNode) {
                crit_flop_list.erase(cfl_it); 
                cfl_it--;
              }
            }
            /*if synNode in crit_flop_map, remove the entry from crit_flop_map*/
            if (crit_flop_list.empty()){ req_flops_matched = true;}
            if (crit_flop_map.find(synNode)!=crit_flop_map.end()) {
              //if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
              for (const auto & o_n: matching_map[synNode]) {
                //need o_n_node to be of type Node (not compact/compact_flat)
                //Node o_n_node(lg,o_n);//points to the node in orig LG
                //o_n_node.set_color(crit_flop_map[synNode]);//FIXME: o_n is compact_flat here. we need it of Node type?
                matched_color_map[o_n]=crit_flop_map[synNode];
              }
            }
          }
        }
        IOtoNodeMap_orig.erase(it++);
        IOtoNodeMap_synth.erase(iov);
        // req. for pass_3 //erase the node from full_orig_map  - value has nodes.
        // req. for pass_3 for (auto it_o = full_orig_map.begin(); it_o!= full_orig_map.end();) {
        // req. for pass_3   if (std::find( (it_o->second).begin(), (it_o->second).end(), orig_node) != (it_o->second).end() ) {
        // req. for pass_3     full_orig_map.erase(it_o++);
        // req. for pass_3   } else ++it_o;
        // req. for pass_3 }
      } else {
        ++it;
      }
    }

    I(!matching_map.empty(), "There should be some node in matching_map to further go to pass_2");

    /*entry on the graph IO matched! moving to pass_2 in matching*/
    bool change_done = true;
    while (change_done) {
      change_done = false;
      /*First: resolve the map*/
      for ( auto & [k,v_map]: IOtoNodeMap_synth) {
        //for (auto& [iov,n]:v_map) 
        for(auto it=v_map.begin(); it!=v_map.end();){
          auto& iv = (it->first).first;//this is i/p set for [n]
          auto& ov = (it->first).second;//this is o/p set for [n]
          //auto& n = it->second; //FIXME: is this correct coding sthyle? (auto& var_name = something;) ?
  
          /*resolve here: if iv contains any entry from matching_map, resolve and make ivResolved. ||ly for ov*/
          std::set<std::string> randSet1;
          for (auto set_it =iv.begin(); set_it!=iv.end(); set_it++) {
            if ((*set_it).find("flop:") != std::string::npos) {//if iv has flop
              auto SflopID = (*set_it).substr(5);//synth flop name captured for comparison
              std::vector<std::string> OflopID = get_map_val(matching_map, SflopID);//FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
              I(OflopID.size()<2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
              if(!(OflopID).empty()){
                std::string i_r="flop:"+OflopID[0];
                randSet1.emplace(i_r);//resolved entry in randSet1
                //change_done=true;//SG:test (for a bug)
                //iv.erase(*set_it);
              } else {
                randSet1.emplace(*set_it);
              }
            } else { //iv value does NOT have flop
              randSet1.emplace(*set_it);
            }
          }/*now we have iv resolved in randSet1!*/
          std::set<std::string> randSet2;
          for (auto set_it =ov.begin(); set_it!=ov.end(); set_it++) {
            if ((*set_it).find("flop:") != std::string::npos) {//if ov has flop
              auto SflopID = (*set_it).substr(5);//synth flop name captured for comparison
              std::vector<std::string> OflopID = get_map_val(matching_map, SflopID);//FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
              I(OflopID.size()<2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
              if(!(OflopID).empty()){
                std::string o_r="flop:"+OflopID[0];
                randSet2.emplace(o_r);//resolved entry in randSet2
                //change_done=true;//SG:test (for a bug)
                //ov.erase(*set_it);
              } else {
                randSet2.emplace(*set_it);
              }
            } else { //ov value does NOT have flop
              randSet2.emplace(*set_it);
            }
          }/*now we have ov resolved in randSet2!*/
          /*make pair<randSet1,randSet2> and replace <iv,ov> with it:*/
          auto extracted_entry = v_map.extract(it++);
          extracted_entry.key() = std::make_pair(randSet1,randSet2);
          v_map.insert(std::move(extracted_entry));
        }//end of for(auto it=v_map.begin(); it!=v_map.end();
      }// end of for ( auto & [k,v_map]: IOtoNodeMap_synth) //map resolved
  
      /*printing the resolved map*/
      fmt::print("\n\nIOtoNodeMap_synth MAP RESOLVED IS:\n");
      print_IOtoNodeMap_synth(IOtoNodeMap_synth); 

      /*Second: do the matching part post resolution*/
      for ( auto & [k,v_map]: IOtoNodeMap_synth) {
        if (req_flops_matched) {break;}
        //for (auto& [iov,n]:v_map) 
        for(auto it=v_map.begin(); it!=v_map.end();){
          if (req_flops_matched) {break;}
          auto& iv = (it->first).first;//this is i/p set for [n]
          auto& ov = (it->first).second;//this is o/p set for [n]
          auto& n = it->second; 
          /*start finding and matching in the resolved map!!:*/
          bool foundFull = false;
          bool foundPartial = false;
          //for (auto [pairIO, nods]:full_orig_map) 
          for(auto ito=full_orig_map.begin(); ito!=full_orig_map.end(); ito++){
            if (req_flops_matched) {break;}
            auto pairIO = ito->first;
            auto nods = ito->second;
            if (iv == pairIO.first && ov==pairIO.second) {
              foundFull = true;
              //std::for_each(n.begin(), n.end(), [](const auto& n1) {matching_map[n1]=nods;});
              for (const auto & n1:n){
                matching_map[n1]=nods;//FIXME: see if the entry is already there in matching_map and then append to the pre-existing vector
                /*if synNode in crit_flop_list, remove from crit_flop_list;*/
                for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
                  if(*cfl_it == n1) {
                    crit_flop_list.erase(cfl_it);
                    cfl_it--;
                  }
                }
                if (crit_flop_list.empty()){ req_flops_matched = true;}
                if (crit_flop_map.find(n1)!=crit_flop_map.end()) {
                  //if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
                  for (const auto & o_n: matching_map[n1]) {
                    //o_n.set_color(crit_flop_map[n1]);//FIXME: o_n is compact_flat here. we need it of Node type?
                    matched_color_map[o_n]=crit_flop_map[n1];
                  }
                }
              }
              //full_orig_map.erase(ito++);
              continue;
            } else if (iv == pairIO.first || ov==pairIO.second) {
              foundPartial = true;
              for (const auto & n1:n){
                matching_map[n1]=nods;//FIXME: see if the entry is already there in matching_map and then append to the pre-existing vector
                /*if synNode in crit_flop_list, remove from crit_flop_list;*/
                for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
                  if(*cfl_it == n1) {
                    crit_flop_list.erase(cfl_it);
                    cfl_it--;
                  }
                }
                if (crit_flop_list.empty()){ req_flops_matched = true;}
                if (crit_flop_map.find(n1)!=crit_flop_map.end()) {
                  //if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color
                  for (const auto & o_n: matching_map[n1]) {
                    //o_n.set_color(crit_flop_map[n1]);//FIXME: o_n is compact_flat here. we need it of Node type?
                    matched_color_map[o_n]=crit_flop_map[n1];
                  }
                }
              }
              //full_orig_map.erase(ito++);
              continue;
            } //else {++ito;}
          } 
          if (foundFull || foundPartial) {
            v_map.erase(it++);
            change_done = true;//FIXME: should change_done be evaluated here only?
          } else {++it;}   
                              
        }//end of for(auto it=v_map.begin(); it!=v_map.end();
      }// end of for ( auto & [k,v_map]: IOtoNodeMap_synth)

      /*if no change_done, i.e. nothing happened in pass_2
       * try pass_3 and then see if some change possible? */
      if(!change_done && !crit_flop_list.empty()) {//start pass_3
        for (auto & [k,v_map]: IOtoNodeMap_synth) {
          for (auto it=v_map.begin(); it!=v_map.end();) {
            change_done = probabilistic_match(it, full_orig_map);
            if (crit_flop_list.empty()){ req_flops_matched = true;}
            if (change_done ){ 
              v_map.erase(it++);
            } else {++it;}
          }
        }
      }//end of if(!change_done && !crit_flop_list.empty()) 
      
    }//end of while (change_done)

    /*Printing "matching map"*/
    fmt::print("\n THE MATCHING_MAP is:\n");
    for (const auto& [k,v]:matching_map) {
      fmt::print("\n{}\t:::\t",k.get_nid());
      for (const auto& v1:v) {
        fmt::print("{}\t", v1.get_nid());
      }
    }
      fmt::print("\n");

    /*printing changed map*/
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nThe complete orig map (full_orig_map) ALTERED is:\n");
    print_MapOf_SetPairAndVec(full_orig_map);
    fmt::print("\n\n===============================\n");
    fmt::print("\n\nThe IOtoNodeMap_orig map ALTERED is:\n");
    for(const auto& [iov,fn]: IOtoNodeMap_orig) {
      for (const auto& ip: iov) {
        fmt::print("{}\t",ip);
      }
      fmt::print("::: \t");
      for (const auto& op:fn) {
        fmt::print("{}\t", op.get_nid());
      }
      fmt::print("\n\n");
    }
    fmt::print("\n\n===============================\n");

    fmt::print("\n\nIOtoNodeMap_synth MAP ALTERED IS:\n");
    print_IOtoNodeMap_synth(IOtoNodeMap_synth);

  }//if(do_matching) closes here

  //FIXME: put some assertion to check if req_flops_matched is still false (when the function completes).
  //coz if it still false at completion, then the combo match will not enter even at the end!
  //Atleast flag it!!
  if (do_matching) {
    //if(!req_flops_matched) { fmt::print("\nMESSAGE: crit_flop_list is not empty. Should have been empty by now.");}
    fmt::print("\n crit_flop_list at this point: \n");
    for (auto & n:crit_flop_list){
      fmt::print("{}\t", n.get_nid());
    }
    I(req_flops_matched,"\n crit_flop_list is not empty. Should have been empty by now.\n");
  }
  bool cellIOMap_synth_resolved = false;
  if(do_matching && !cellIOMap_synth.empty() ){
    /*resolve cellIOMap_synth with help of matching_map*/
    std::set<std::pair<std::set<std::string>, std::set<std::string>> > noOverwrite_in_cellIOMapSynth;
    for(auto it=cellIOMap_synth.begin(); it!=cellIOMap_synth.end();){
      auto& iv = (it->first).first;//this is i/p set for [n]
      auto& ov = (it->first).second;//this is o/p set for [n]
      /*resolve here: if iv contains any entry from matching_map, resolve and make ivResolved. ||ly for ov*/
      std::set<std::string> randSet1;
      for (auto set_it =iv.begin(); set_it!=iv.end(); set_it++) {
        if ((*set_it).find("flop:") != std::string::npos) {//if iv has flop
          auto SflopID = (*set_it).substr(5);//synth flop name captured for comparison
          std::vector<std::string> OflopID = get_map_val(matching_map, SflopID);//FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
          I(OflopID.size()<2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
          if(!(OflopID).empty()){
            std::string i_r="flop:"+OflopID[0];
            randSet1.emplace(i_r);//resolved entry in randSet1
          } else {
            randSet1.emplace(*set_it);
          }
        } else { //iv value does NOT have flop
          randSet1.emplace(*set_it);
        }
      }/*now we have iv resolved in randSet1!*/
      std::set<std::string> randSet2;
      for (auto set_it =ov.begin(); set_it!=ov.end(); set_it++) {
        if ((*set_it).find("flop:") != std::string::npos) {//if ov has flop
          auto SflopID = (*set_it).substr(5);//synth flop name captured for comparison
          std::vector<std::string> OflopID = get_map_val(matching_map, SflopID);//FIXME: pass by reference??//get_map_val will give the orig_flop_ID corresponding toSflopID.
          I(OflopID.size()<2, "\n\n1 synth flop matched with many orig flops.... look into it... how to process it.\n\n");
          if(!(OflopID).empty()){
            std::string o_r="flop:"+OflopID[0];
            randSet2.emplace(o_r);//resolved entry in randSet2
            //ov.erase(*set_it);
          } else {
            randSet2.emplace(*set_it);
          }
        } else { //ov value does NOT have flop
          randSet2.emplace(*set_it);
        }
      }/*now we have ov resolved in randSet2!*/
      /*make pair<randSet1,randSet2> and replace <iv,ov> with it:*/
      auto extracted_entry = cellIOMap_synth.extract(it++);
      extracted_entry.key() = std::make_pair(randSet1,randSet2);
      /*if key already in noOverwrite-Set, then take extracted_entry.mapped() and append to map.find(extracted_entry.key()). 
       * else just store this key in noOverwrite_in_cellIOMapSynth.*/
      if (noOverwrite_in_cellIOMapSynth.find(extracted_entry.key())!=noOverwrite_in_cellIOMapSynth.end()) {
        for (const auto &m:cellIOMap_synth[extracted_entry.key()]){
          (extracted_entry.mapped()).emplace_back(m);
        }
        cellIOMap_synth.erase(extracted_entry.key());//won't overwrite the same key value!
      } else {
        noOverwrite_in_cellIOMapSynth.insert(extracted_entry.key());
      }
       /*... and insertion to cellIOMap_synth remains same in both cases.*/
      cellIOMap_synth.insert(std::move(extracted_entry));
    }//end of for(auto it=cellIOMap_synth.begin(); it!=cellIOMap_synth.end();

    //print the cellIOMap_synth
    fmt::print("\n\nthe cellIOMap_synth RESOLVED IS:\n");
    print_MapOf_SetPairAndVec(cellIOMap_synth);
    cellIOMap_synth_resolved=true;
    //print crit_cell_list
    fmt::print("\n crit_cell_list at this point: \n");
    for (auto & n:crit_cell_list){
      fmt::print("{}\t", n.get_nid());
    }
  }//if(do_matching && req_flops_matched && !cellIOMap_synth.empty()) ends here

  fmt::print("\n\n\n");
  if(do_matching && cellIOMap_synth_resolved){
    /*matching of combo part happens here with help of pre-synth LG!
     * 1. take LGorig and go to the SP for 1st cell in cellIOMap_synth*/
    for (const auto & [k,v]: cellIOMap_synth){
      auto & allSPs = k.first;
      auto & allEPs = k.second;
      
      for (const auto & sp:allSPs) fmt::print("{} ", sp);
      fmt::print("\n-\n");
      for (const auto & ep:allEPs) fmt::print("{}\n", ep);
      fmt::print("\n\n\n");

      /*go to 1st SP of allSPs for 1st entry
       * and start iterating from there*/
      //const auto required_node = *(allSPs.begin());
      for (const auto &required_node : allSPs) {
        if ((required_node).substr(0,4)!= "flop") {//then it is graph IO
          //Node startPoint_node(lg, required_node );
          lg->each_graph_input([required_node,this](Node_pin &dpin) {
            const auto & in_node = dpin.get_node();
            std::string comp_name = (dpin.has_name()?dpin.get_name():dpin.get_pin_name());
            if(comp_name==required_node){
              path_traversal(in_node);
            }
            });
        } else {
          for (const auto& startPoint_node : lg->fast(true)) {//FIXME:REM
            if(std::to_string(startPoint_node.get_nid().value)==required_node.substr(5)){//FIXME:REM
              fmt::print("Found node {}\n", startPoint_node.get_nid());//FIXME:REM
              //keep traversing forward until you hit an EP
              path_traversal(startPoint_node);

            }//FIXME:REM
          }//end of for (const auto& startPoint_node : lg->forward())//FIXME:REM
        }
      }
    }//for (auto [k,v]: cellIOMap_synth) ends here
    /*Printing "matching map"*/             
    fmt::print("\n THE FINAL (combo matched) MATCHING_MAP is:\n");
    for (const auto& [k,v]:matching_map) {  
      fmt::print("\n{}\t:::\t",k.get_nid());
      for (const auto& v1:v) {
        fmt::print("{}\t", v1.get_nid());   
      }
    }
      fmt::print("\n");                     
  }//if(cellIOMap_synth_resolved) ends here

  if(!matched_color_map.empty()) {
    /*Printing "matched_color_map"*/
    fmt::print("\n THE matched_color_map is:\n");
    for (const auto& [k,v]:matched_color_map) {
      fmt::print("\t{}\t:::\t{}\n",k.get_nid(), v);
    }
    fmt::print("\n");
    //print crit_cell_list
    fmt::print("\n crit_cell_list at this point: \n");
    for (auto & n:crit_cell_list){
      fmt::print("{}\t", n.get_nid());
    }
  }

}

void Traverse_lg::path_traversal(const Node &start_node){
  Node this_node = start_node;
  for(auto s : this_node.out_sinks()){
    this_node = s.get_node();
    if ( (this_node.has_color()?(this_node.get_color()!=VISITED_COLORED):true) && (!is_endpoint(this_node)) ) {
      /*it is unvisited combinational cell in orig graph*/
      this_node.set_color(VISITED_COLORED);
      
      std::set<std::string> nodes_in_set;
      std::set<std::string> nodes_out_set;
      std::set<std::string> nodes_io_set;
      get_input_node(s, nodes_in_set, nodes_io_set);
      get_output_node(s, nodes_out_set, nodes_io_set) ;


      fmt::print("\n -- Entering check_in_cellIOMap_synth( {} ) with: --\n", this_node.get_nid());
      fmt::print("\t in_set:\n");
      for( const auto & i: nodes_in_set){
        fmt::print("\t\t{}, ", i);
      }
      fmt::print("\n\t out_set:\n");
      for( const auto & i: nodes_out_set){
        fmt::print("\t\t{}, ", i);
      }
  
      auto val = check_in_cellIOMap_synth(nodes_in_set, nodes_out_set, this_node);

      if (val) {
        path_traversal(this_node);
      }
    } else if ( this_node.has_color()?(this_node.get_color()==VISITED_COLORED):false) {
      continue;
    } else if (is_endpoint(this_node)) {
      continue;
    } else {
      I(false,"\nISSUE TO DEBUG!\n" );
    }
  }
  return;
}

bool Traverse_lg::check_in_cellIOMap_synth(std::set<std::string> &in_set, std::set<std::string> &out_set, Node &start_node) {
  /*if (this node is NOT a subset of any entry in cellIOMap_synth) return false
   *  -currently a complete match is considered; subset is a future FIXME-
   * returning F because : stop iterating for its sink pins as this path is not going anywhere in criticality
   * else return T 
   * Also: put the data in matching_map? or some other map to reflect matched combo cells?
   * */ 

  //make io pair of the 2 sets from LGorig
  auto pair_to_find = std::make_pair(in_set, out_set); 
  //find the pair in keys of the cellIOMap_synth
  if(cellIOMap_synth.find(pair_to_find) != cellIOMap_synth.end()) {
    //if found, put this start_node of LGorig against value from cellIOMap_synth for LGsynth to matching_map AND return T
    auto found_synth_cell_vals = cellIOMap_synth[pair_to_find];
    for(const auto & n:found_synth_cell_vals) {
      std::vector<Node::Compact_flat> tmpVec;
      if(matching_map.find(n) != matching_map.end()) {
        tmpVec.assign((matching_map[n]).begin() , (matching_map[n]).end() );
        tmpVec.emplace_back(start_node.get_compact_flat());
      } else {
        tmpVec.emplace_back(start_node.get_compact_flat());
      }
      matching_map[n]=tmpVec;
      //FIXME: remove from crit_cell_list; better still: remove crit_cell_list (not required)
      for (auto cfl_it = crit_cell_list.begin(); cfl_it != crit_cell_list.end(); cfl_it++) {
        if(*cfl_it == n) {
          crit_cell_list.erase(cfl_it); 
          cfl_it--;
        }
      }
      //put orig combo node in matched_color_map. color value will be crit_cell_map[n]:
      matched_color_map[start_node.get_compact_flat()] = crit_cell_map[n];
    }
    
    return true;
  }
  //if not found, return F
  return false;
}

bool Traverse_lg::is_startpoint(Node node_to_eval ){
  /*if (node is flop or graph input) return true else return false*/
  if(node_to_eval.is_type_flop() || (node_to_eval.is_type_sub()?((std::string(node_to_eval.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false) || node_to_eval.is_graph_input()){
    return true;
  } 
  return false;
}

bool Traverse_lg::is_endpoint(Node node_to_eval){
  /*if (node is flop or graph output) return true else return false*/
  if(node_to_eval.is_type_flop() ||  (node_to_eval.is_type_sub()?((std::string(node_to_eval.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false) || node_to_eval.is_graph_output()){
    return true;
  } 
  return false;

}

std::vector<std::string> Traverse_lg::get_map_val(absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat> >& find_in_map, std::string key_str) {
  std::vector<std::string> ret_vec;
  for (auto & [k,v]: find_in_map) {
    auto node_str = std::to_string(k.get_nid().value);
    //fmt::print("**{}, {}\n", node_str, key_str);
    if (node_str==key_str) {
      for (auto v1: v) {
        ret_vec.emplace_back(std::to_string(v1.get_nid().value));
      }
    }
  }
  return ret_vec;
}

// void Traverse_lg::get_input_node(const Node_pin &node_pin, absl::btree_set<std::string>& in_set) {
void Traverse_lg::get_input_node(const Node_pin &node_pin, std::set<std::string>& in_set, std::set<std::string>& io_set, bool addToCFL) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_graph_input()  || (node.is_type_sub()?((std::string(node.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false)) {
    if (node.is_type_const()) {
      //do not keep const for future reference
      return;
    } else if (node.is_graph_io()) {
      auto tmp_x = node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name();
      if (tmp_x!="reset" && tmp_x!="clock") {
        in_set.insert(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
        io_set.insert(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
      }
    } else {
      bool isFlop = (node.is_type_flop() || (node.is_type_sub()?((std::string(node.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false));
      std::string temp_str (isFlop?"flop":(node.is_type_sub()?(std::string(node.get_type_sub_node().get_name())) : node.get_type_name()));//if it is a flop, write "flop" else evaluate

      if(isFlop){ 
        temp_str+=":";
        //temp_str+=node_pin.get_pin_name();//in inputs, this is always Y/Q.
        //temp_str+="->";
        //temp_str+=node.get_driver_pin().get_pin_name();
        //temp_str+="(";
        //temp_str+=(node.get_driver_pin().get_wire_name());
        //temp_str+=(node.has_name()?node.get_name():node.out_connected_pins()[0].get_wire_name());//FIXME:changed to line below temporarily for debugging. revert back!
        temp_str+=std::to_string(node.get_compact_flat().get_nid());
        //temp_str+=")";
        if(addToCFL && (std::find(crit_flop_list.begin(), crit_flop_list.end(),(node.get_compact_flat()))==crit_flop_list.end())) {
          crit_flop_list.emplace_back(node.get_compact_flat());
          crit_flop_map[node.get_compact_flat()] = node.has_color()?node.get_color():0;//keeping here 0 for no color. for now.
        }
      }
      in_set.insert(temp_str);
      if(!isFlop) {io_set.insert(temp_str);}//do not want flops in pure io_set
    }
    return;
  } else {
      for (const auto& indr : node.inp_drivers()) {
        //auto inp_node = indr.get_node();
        get_input_node(indr, in_set, io_set, addToCFL);
      }
  }
}

// void Traverse_lg::get_output_node(const Node_pin &node_pin, absl::btree_set<std::string>& out_set) {
void Traverse_lg::get_output_node(const Node_pin &node_pin, std::set<std::string>& out_set, std::set<std::string>& io_set, bool addToCFL) {
  auto node = node_pin.get_node();
  if(node.is_type_flop() || node.is_graph_output()  || (node.is_type_sub()?((std::string(node.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false)) {
    if(node.is_graph_io()) {
      //out_set.emplace_back(node_pin.has_name()?node_pin.get_name():node_pin.get_pin_name());
      out_set.insert(node_pin.get_pin_name());
      io_set.insert(node_pin.get_pin_name());
    } else {
      bool isFlop = (node.is_type_flop() || (node.is_type_sub()?((std::string(node.get_type_sub_node().get_name())).find("_df")!=std::string::npos):false));
      std::string temp_str (isFlop?"flop":(node.is_type_sub()?(std::string(node.get_type_sub_node().get_name())) : node.get_type_name()));//if it is a flop, write "flop" else evaluate
      
      if(isFlop){ 
        temp_str+=":";
        //temp_str+=node_pin.get_pin_name();//in outputs, this is always din/D
        //temp_str+="->";
        //temp_str+=node.get_driver_pin().get_pin_name();
        //temp_str+="(";
        //temp_str+=(node.get_driver_pin().get_wire_name());
        //temp_str+=(node.has_name()?node.get_name():node.out_connected_pins()[0].get_wire_name());//FIXME:changed to line below temporarily for debugging. revert back!
        temp_str+=std::to_string(node.get_compact_flat().get_nid());
        //temp_str+=")";
        if(addToCFL && (std::find(crit_flop_list.begin(), crit_flop_list.end(),(node.get_compact_flat()))==crit_flop_list.end())) {
          crit_flop_list.emplace_back(node.get_compact_flat());
          crit_flop_map[node.get_compact_flat()] = node.has_color()?node.get_color():0;//keeping here 0 for no color. for now.
        }
      }
      out_set.insert(temp_str);
      if(!isFlop) {io_set.insert(temp_str);}//do not want flops in pure io_set
    }
    return;
  } else {
      for (const auto& outdr : node.out_sinks() ) {
        //auto out_node = outdr.get_node();
        get_output_node(outdr, out_set, io_set, addToCFL);
      }
  }
}




void Traverse_lg::print_IOtoNodeMap_synth(const absl::node_hash_map<std::set<std::string>, setMap_pairKey > &mapInMap ){

    for(const auto& [ioval,inMap]: mapInMap) {
      for (const auto& ip: ioval) {
        fmt::print("{}\t",ip);
      }
      fmt::print("\n");
      
      for(const auto& [ioPair, n_list]: inMap) {
        fmt::print("\t\t\t\t");
        for (const auto& ip: ioPair.first) {
          fmt::print("{}\t",ip);
        }
        fmt::print("||| \t");
        for (const auto& op:ioPair.second) {
          fmt::print("{}\t", op);
        }
        fmt::print("::: \t");
        for (const auto& n:n_list) {
          fmt::print("{}\t", n.get_nid());
        }
        fmt::print("\n");
      }
      fmt::print("\n");
    }
}

void Traverse_lg::print_MapOf_SetPairAndVec(const setMap_pairKey &MapOf_SetPairAndVec){
    for(const auto& [iov,fn]: MapOf_SetPairAndVec) {
      for (const auto& ip: iov.first) {
        fmt::print("{}\t",ip);
      }
      fmt::print("||| \t");
      for (const auto& op:iov.second) {
        fmt::print("{}\t", op);
      }
      fmt::print("::: \t");
      for (const auto& op:fn) {
        fmt::print("{}\t", op.get_nid());
      }
      fmt::print("\n\n");
    }
}

bool Traverse_lg::probabilistic_match(setMap_pairKey::iterator &map_it, setMap_pairKey &orig_map) {

  auto map_entry = *map_it;

  auto synth_set = getUnion(map_entry.first.first, map_entry.first.second);
  int match_count = 0;
  //auto mismatch_count = synth_set.size(); mismatch_count = 0;
  unsigned long mismatch_count = 0;
  std::vector<Node::Compact_flat> orig_nodes_matched;
  
  for (auto &[k,v]:orig_map){
    auto orig_set = getUnion(k.first, k.second);
    
    std::vector<std::string> setIntersectionVec (synth_set.size()+orig_set.size());
    std::vector<std::string>::iterator ls;
    ls = std::set_intersection(synth_set.begin(), synth_set.end(), orig_set.begin(), orig_set.end(), setIntersectionVec.begin());
    auto intersectionVal = ls-setIntersectionVec.begin();//(v3.resize(ls-v3.begin())).size()
    
    auto unionSet = getUnion(synth_set,orig_set);
    auto unionVal = unionSet.size();

    auto new_match_count = intersectionVal;
    auto new_mismatch_count = unionVal-intersectionVal;

    if(new_match_count>match_count) {
      /*this is a better match. keep "v" in orig_nodes_matched. update match_count=new_match_count*/
      orig_nodes_matched = v;
      match_count = new_match_count;
    } else if (new_match_count==match_count){
      if (new_mismatch_count < mismatch_count){
        /*this is a better match. keep "v" in orig_nodes_matched. update match_count=new_match_count*/
        orig_nodes_matched = v;
        mismatch_count = new_mismatch_count;
      }
    } //else keep iterating

  }//end of iterating orig_map
  if(orig_nodes_matched.size()>0){
    /*put in matching_map and matched_color_map*/
    for (auto &n: map_entry.second) {
      for (const auto &orig_node: orig_nodes_matched) {
        std::vector<Node::Compact_flat> tmpVec;
        if(matching_map.find(n) != matching_map.end()) {
          tmpVec.assign((matching_map[n]).begin() , (matching_map[n]).end() );
          tmpVec.emplace_back(orig_node);
        } else {
          tmpVec.emplace_back(orig_node);
        }
        matching_map[n]=tmpVec;
      }

      if (crit_flop_map.find(n)!=crit_flop_map.end()) {
        /*if synnode in crit_flop_map, color corresponding orig nodes with this synnode's color*/
        for (const auto & o_n: matching_map[n]) {
          matched_color_map[o_n]=crit_flop_map[n];
        }
      }
      /*if synNode in crit_flop_list, remove from crit_flop_list;*/
      for (auto cfl_it = crit_flop_list.begin(); cfl_it != crit_flop_list.end(); cfl_it++) {
        if(*cfl_it == n) {
          crit_flop_list.erase(cfl_it); 
          cfl_it--;
        }
      }
      
    }
    return true;
  } else return false;
}


