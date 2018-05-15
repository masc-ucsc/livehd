
#include <string>
#include <time.h>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_cse.hpp"
#include <iterator>

Pass_cse::Pass_cse()
    : Pass("cse") {
}
enum comp_result {
  none,
  equalset,
  subset,  // this is subset of that
  superset // this is superset of that
};
//haha

class and_node_for_comp {
  //  Node_Type_Op node_type_op;
public:
  int idx;
  //int next_avail_pid;
  bool                              need_modify;
  typedef std::pair<Node_Pin, int>  NPPair;       // <the node pin, to be replaced by idx>
  typedef std::map<int, int>        CoverByNPMap; // key (idx) is covered by value (idx)
  CoverByNPMap                      cover_by_np_map;
  typedef std::vector<NPPair>       NPPairVec;
  typedef std::vector<int>          CoveredPos;    //covered node pin positions in the np_pair_vec>
  typedef std::map<int, CoveredPos> CoveredPosMap; // <idx, covered node pin positions in the np_pair_vec> key covers value
  CoveredPosMap                     covered_pos_map;
  NPPairVec                         np_pair_vec; // <Node Pins: int != -1 means to be removed, it's the new idx which replaces it>

  and_node_for_comp() {
    need_modify = 0;
  }

  comp_result comp_dp(and_node_for_comp that_node) {
    // longest common substring algorithm
    NPPairVec                             A = this->np_pair_vec;
    NPPairVec                             B = that_node.np_pair_vec;
    typedef std::vector<std::vector<int>> Matrix;
    //typedef vector<int> Row;

    Matrix LCS;
    //    int **LCS = new int[A.size() + 1][B.size() + 1];
    // if A is null then LCS of A, B =0
    int size_a = std::distance(begin(A), end(A));
    for(int i = 0; i < size_a; i++) {
      LCS[0][i] = 0;
    }

    // if B is null then LCS of A, B =0
    int size_b = std::distance(begin(B), end(B));
    for(int i = 0; i < size_b; i++) {
      LCS[i][0] = 0;
    }

    int i = 1, j = 1;
    for(const auto &a : A) {
      for(const auto &b : B) {
        if(a.first.get_nid() == b.first.get_nid() &&
           a.first.get_pid() == b.first.get_pid()) {
          LCS[i][j] = LCS[i - 1][j - 1] + 1;
        } else {
          LCS[i][j] = std::max(LCS[i - 1][j], LCS[i][j - 1]);
        }
        j++;
      }
      i++;
    }
    if(LCS[A.size()][B.size()] == A.size() && LCS[A.size()][B.size()] == B.size()) {
      fmt::print("A is equalset of B\n");
      return (equalset);
    } else if(LCS[A.size()][B.size()] >= A.size()) {
      fmt::print("A is superset of B\n");
      return (superset);
    } else if(LCS[A.size()][B.size()] >= B.size()) {
      fmt::print("A is subset of B\n");
      return (subset);
    }

    throw std::runtime_error("what is this case?");
  }

  comp_result comp(and_node_for_comp that_node) {
    //    std::vector<pair<Node_Pin(), int>>::const_iterator j;  // existing nodes
    //  std::vector<pair<Node_Pin(), int>>::const_iterator i; // new challenger
    auto j     = this->np_pair_vec.begin();
    int  int_i = 0;
    //int start = -1;
    //int end = 0;
    int              cover_count = 0;
    int              rep_count   = 0;
    int              overlap     = 0; // # of overlap with previous replacing node
    Node_Pin         dummy_node  = Node_Pin(1, 1, 0);
    CoveredPos       new_covered_pos;
    std::vector<int> conflict_idx_vec;
    NPPair           last_pair = std::make_pair(dummy_node, -1);
    for(auto i = that_node.np_pair_vec.begin(); i != that_node.np_pair_vec.end(); ++i) {
      fmt::print("j.nid:{}, i.nid:{}, j.pid:{}, i.pid:{}\n", j->first.get_nid(), i->first.get_nid(), j->first.get_pid(), i->first.get_pid());
      if(j->first.get_nid() == i->first.get_nid() && j->first.get_pid() == i->first.get_pid()) {
        // Replace could happen
        cover_count++;
        new_covered_pos.push_back(int_i);
        if(j->second > 0) {
          overlap += covered_pos_map[j->second].size();
          if(overlap > that_node.np_pair_vec.size()) {
            return (none);
          }
          if(conflict_idx_vec.back() != j->second) {
            // new conflicted idx
            conflict_idx_vec.push_back(j->second);
          }
        }
        if(int_i > 0 &&
           j->first.get_nid() == last_pair.first.get_nid() &&
           j->first.get_pid() == last_pair.first.get_pid()) { // consecutive repetitive nodes
          rep_count++;
        } else {
          rep_count = 0;
        }
        j++;
      } else { // j i dont match
        if(cover_count > 0) {
          if(cover_count == rep_count && int_i > 0 &&
             j->first.get_nid() == last_pair.first.get_nid() && j->first.get_pid() == last_pair.first.get_pid()) {
            // repeating from the beginning; dont inc any count, just continue
            // continue
          } else {
            break;
          }
        }
        if(cover_count < 0) {
          // nothing special
          // continue
        }
      }
      int_i++;
      last_pair.first  = j->first;
      last_pair.second = j->second;
    } // end of for loop

    fmt::print("idx:{}, cover:{}, this:{}, that:{}\n", idx, cover_count, this->np_pair_vec.size(), that_node.np_pair_vec.size());

    //haha
    if(overlap > 0) {
      if(cover_count < overlap && cover_count > 0) {
        return (none);
      } else if(cover_count >= overlap) {
        // remove old pairs,
        for(auto k = conflict_idx_vec.begin(); k != conflict_idx_vec.end(); ++k) {
          // CoveredPosMap[*k] is old covered pos
          for(auto ii = covered_pos_map[*k].begin(); ii != covered_pos_map[*k].end(); ++ii) {
            np_pair_vec[*ii].second = -1; // reset
            cover_by_np_map.erase(np_pair_vec[*ii].first.get_nid());
          }
          // remove old covered_np_map entries
          covered_pos_map.erase(*k);
        }
      }
      // add new pairs
      for(auto jj = new_covered_pos.begin(); jj != new_covered_pos.end(); ++jj) {
        np_pair_vec[*jj].second                           = that_node.idx;
        cover_by_np_map[np_pair_vec[*jj].first.get_nid()] = that_node.idx;
      }
      // add new covered_np_map entry
      covered_pos_map[that_node.idx] = new_covered_pos;
    } else if(cover_count == that_node.np_pair_vec.size() && (this->np_pair_vec.size()) == that_node.np_pair_vec.size()) {
      // add new pairs
      for(auto jj = new_covered_pos.begin(); jj != new_covered_pos.end(); ++jj) {
        np_pair_vec[*jj].second                           = that_node.idx;
        cover_by_np_map[np_pair_vec[*jj].first.get_nid()] = that_node.idx;
      }
      // add new covered_np_map entry
      covered_pos_map[that_node.idx] = new_covered_pos;
      need_modify                    = 1;
      fmt::print("A is equalset of B\n");
      return (equalset);
    } else if(cover_count == this->np_pair_vec.size()) {
      // add new pairs
      for(auto jj = new_covered_pos.begin(); jj != new_covered_pos.end(); ++jj) {
        np_pair_vec[*jj].second                           = that_node.idx;
        cover_by_np_map[np_pair_vec[*jj].first.get_nid()] = that_node.idx;
      }
      // add new covered_np_map entry
      covered_pos_map[that_node.idx] = new_covered_pos;
      need_modify                    = 1;
      fmt::print("A is subset of B\n");
      return (subset);
    }
    // TODO: delete
    else {
      fmt::print("A is superset of B\n");
      return (superset);
    }
    /*
    i=that_node.np_pair.begin();
    int int_j = 0;
    start = -1;
    end = 0;
    for(j=this->np_pair.begin(); j!=this->np_pair.end(); ++j){
      if (*j == *i) {
        if (start==-1) {
          start = int_j;
        }
        else { // started
          end = int_j;
        }
      i++;
      }
      else {
        if (start > -1) { //started
          break;
        }
      }
      int_j++;
    }
    if ((end - start + 1) == that_node.inp_idx.size() ) {
      return(superset);
    }
    */

    throw std::runtime_error("what is this case?");
  }
  //  node_for_comp () {};
};
//haha/

void Pass_cse::traverse(LGraph *g, std::map<int, and_node_for_comp> &and_node_for_comp_map, int round) {
  for(auto idx : g->forward()) {
    fmt::print("{} round visited idx:{}\n", round, idx);
    std::string temp_key;
    switch(g->node_type_get(idx).op) {
    case Invalid_Op:
      fmt::print("Invalid_Op.\n");
      break;
    case Sum_Op:
      fmt::print("Sum_Op.\n");
      break;
    case Mult_Op:
      fmt::print("Mult_Op.\n");
      break;
    case Not_Op:
      fmt::print("Not_Op.\n");
      break;
    case Join_Op:
      fmt::print("Join_Op.\n");
      break;
    case And_Op: {
      fmt::print("And_Op.\n");
      if(round == 1) {
        //haha
        comp_result       result;
        and_node_for_comp new_node_for_comp;
        new_node_for_comp.idx = idx;
        for(const auto &in_edge : g->inp_edges(idx)) {
          std::pair<Node_Pin, int> new_pair = std::make_pair(in_edge.get_out_pin(), -1);
          new_node_for_comp.np_pair_vec.push_back(new_pair);
        }
        for(auto i : and_node_for_comp_map) {
          fmt::print("Before comp.\n");
          result = new_node_for_comp.comp(i.second);
          std::string to_print;
          switch(result) {
          case equalset:
            to_print = "equalset";
            break;
          case subset:
            to_print = "subset";
            break;
          case superset:
            to_print = "superset";
            break;
          default:
            to_print = "WEIRD?";
            break;
          }
          fmt::print("result is {}.\n", to_print);
        }
        and_node_for_comp_map.insert(std::make_pair(idx, new_node_for_comp));
        //haha//
      } else if(round == 2) {
        for(const auto &in_edge : g->inp_edges(idx)) {
          //haha
          and_node_for_comp   new_node_for_comp = and_node_for_comp_map[idx];
          Node_Pin            out_pin           = in_edge.get_out_pin();
          std::map<int, bool> local_map; // <idx, exist>
          auto                it = (new_node_for_comp.cover_by_np_map).find(out_pin.get_nid());
          if(it != new_node_for_comp.cover_by_np_map.end()) {
            // add new input edge
            if(local_map.find(it->second) == local_map.end()) {
              Node_Pin dst = Node_Pin(idx, 0, 1);
              Node_Pin src = Node_Pin(it->second, 0, 0);
              g->add_edge(src, dst);
              local_map.insert(std::make_pair(it->second, 1));
              fmt::print("Adding edge from {} to {}.\n", it->second, idx);
            }
            // rm old edges
            new_node_for_comp.cover_by_np_map.erase(it);
            fmt::print("Removing edge from {} to {}.\n", in_edge.get_idx(), idx);
            fmt::print("IN EDGE idx:{}, in_pid:{}, out_pid:{}\n", in_edge.get_idx(), in_edge.get_inp_pin().get_pid(), in_edge.get_out_pin().get_pid());
            g->del_edge(in_edge);
            fmt::print("Edge deleted\n");
          }
          //haha*/
        }
      }
    } break;
    case Or_Op:
      fmt::print("Or_Op.\n");
      break;
    case Xor_Op:
      fmt::print("Xor_Op.\n");
      break;
    case Flop_Op:
      fmt::print("Flop_Op.\n");
      break;
    case LessThan_Op:
      fmt::print("LessThan_Op.\n");
      break;
    case GreaterThan_Op:
      fmt::print("GreaterThan_Op.\n");
      break;
    case GraphIO_Op:
      fmt::print("GraphIO_Op.\n");
      break;
    case SubGraph_Op:
      fmt::print("SubGraph_Op.\n");
      break;
    case U32Const_Op:
      fmt::print("U32Const_Op.\n");
      break;
    case StrConst_Op:
      fmt::print("StrConst_Op.\n");
      break;
    default:
      fmt::print("Undefined OP.\n");
    } // end of switch
    for(const auto &in_edge : g->inp_edges(idx)) {
      fmt::print("IN EDGE idx:{}, in_pid:{}, out_pid:{}\n", in_edge.get_idx(), in_edge.get_inp_pin().get_pid(), in_edge.get_out_pin().get_pid());
    }
  } // end of for loop: for(auto idx:g->forward())
}

void Pass_cse::transform(LGraph *g) {
  std::map<int, and_node_for_comp> and_node_for_comp_map; // idx, node
                                                          //  std::map<std::string, int> idx_map; // <op_sort_pid, idx>
  fmt::print("trying a graph with {} size\n", g->size());

  traverse(g, and_node_for_comp_map, 1);
  traverse(g, and_node_for_comp_map, 2);
  traverse(g, and_node_for_comp_map, 3);
}
