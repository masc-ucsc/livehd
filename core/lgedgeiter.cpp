
#include <assert.h>

#include "sparsehash/dense_hash_map"

#include <fstream>
#include <iostream>
#include <set>

#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

google::dense_hash_map<int64_t, uint32_t> nid2inputs;

Edge_iterator::CPod_iterator Edge_iterator::CPod_iterator::operator++() {
  CPod_iterator i(ptr, e, inputs);
  const auto &  node = Node_Internal::get(ptr);

  if((inputs && !ptr->is_last_input()) || (!inputs && !ptr->is_last_output())) {
    ptr += ptr->next_node_inc();
    assert(&node == &Node_Internal::get(ptr));
    return i;
  }

  if(node.is_last_state()) {
    ptr = e;
    return i;
  }

  const Edge *ptr2 = ptr;
  while(true) {

    const auto &         root_page = Node_Internal_Page::get(ptr2);
    const Node_Internal *root      = (const Node_Internal *)&root_page;

    Index_ID idx   = Node_Internal::get(ptr2).get_next();
    Index_ID delta = idx - root_page.get_idx();

    assert(node.get_master_root_nid() == root[delta].get_master_root_nid());

    if(inputs) {
      ptr2 = root[delta].get_input_begin();
      if(root[delta].has_local_inputs()) {
        ptr = ptr2;
        break;
      }
    } else {
      ptr2 = root[delta].get_output_begin();
      if(root[delta].has_local_outputs()) {
        ptr = ptr2;
        break;
      }
    }

    if(root[delta].is_last_state()) {
      ptr = e;
      // if ((root[delta].has_local_outputs() && !inputs)
      //   ||(root[delta].has_local_inputs() && inputs))
      assert(&node == &Node_Internal::get(ptr));
      break; // No more in this iterator
    }
  }

  return i;
}
