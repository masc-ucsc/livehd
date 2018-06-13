#ifdef PY_LGRAPH_H
#error "python lgraph modules can be included only once in pyth/graph.cpp"
#endif
#define PY_LGRAPH_H

  m.def("find_graph", &LGraph::find_graph, "Returns if exists or creates a new lgraph");
  m.def("open_lgraph", &LGraph::open_lgraph, "Open if exists or creates a new lgraph");

  py::class_<LGraph>(m, "LGraph")
    .def(py::init<const std::string &>())
    .def("lg_id", &LGraph::lg_id);

