#include "Adjacency_list.hpp"
#include "range/v3/all.hpp"

#include <iostream>
#include <string>
using namespace ranges;
using namespace graph;
using namespace std;

int main() {
  Bi_adjacency_list g;
  auto v1 = g.insert_vert();
  auto v2 = g.insert_vert();
  auto v3 = g.insert_vert();
  
  auto names = g.vert_map<string>();
  names[v1] = "v1";
  names[v2] = "v2";
  names[v3] = "v3";

  auto smap = g.vert_map<int>();
  smap[v1] = 0;
  smap[v2] = 0;
  smap[v3] = 0;

  int valid_set = 0;
  
  auto &smapref = smap;
  
  auto not_in_set = [&smapref, valid_set](decltype(g.insert_vert()) v) -> bool {
    return smapref(v) != valid_set;
  };

  auto vset = g.verts() | view::remove_if(not_in_set);
  
  for (auto v : vset) {
    cout << names[v] << endl;
  }
  
  static int set_counter = 1;
  unsigned int set_inc = 0;
  for (auto v : vset) {
    smapref[v] = set_counter + set_inc;
    
    if (set_inc == 1) {
      set_inc = 0;
    } else {
      set_inc = 1;
    }
  }
  
  for (auto v : vset) {
    cout << names[v] << endl;
  }
}
