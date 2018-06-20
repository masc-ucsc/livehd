
#include "lgraph_base_core.hpp"
#include "lgedgeiter.hpp"

Fast_edge_iterator Lgraph_base_core::fast() const {
  if(node_internal.empty())
    return Fast_edge_iterator(0, this);

  return Fast_edge_iterator(1, this);
}
int               Console_init::_static_initializer = initialize_logger();
