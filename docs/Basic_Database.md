
TODO: Explain the basic LGraph constructs/database used by people building passes

Node:

  constructor:
    LGraph::create_node
  destructor:
    LGraph::del_node
  accessor:
    LGraph::get_node

Node_pin:

  It is a pair coupling Node and Port. It can be either an driver (output) or an sink (input) pin.

Edge:

  An unidirectional link between two Node_pin.

  constructor:
    LGraph::add_edge
  destructor:
    LGraph::del_edge
  accessor:
    LGraph::inp_edges
    LGraph::out_edges

Source (src):

Dest (dst): (do we rename to sink?)

Wirename:

  accessors:
    has_wirename
    get_node_wirename

  deprecated:
    get_wid (use has_wirename)

