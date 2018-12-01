
TODO: Explain the basic LGraph constructs/database used by people building passes

Node:

  constructor:
    LGraph::create_node
  destructor:
    LGraph::del_node
  accessor:
    LGraph::get_node

Node_Pin:

  It is a pair coupling Node and Port

Edge:

  An unidirectional link between two Node_Pin.

  constructor:
    LGraph::add_edge
  destructor:
    LGraph::del_edge
  accessor:
    LGraph::inp_edges
    LGraph::out_edges

  TO DEPRECATE:
    find_idx_from_pid
    (slow iterator, use edge.get_idx)

Source (src):

Dest (dst): (do we rename to sink?)

Wirename:

  accessor:
    get_wid
    get_node_wirename

