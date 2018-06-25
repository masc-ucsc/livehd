//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifdef PY_LGRAPH_H
#error "python lgraph modules can be included only once in pyth/graph.cpp"
#endif
#define PY_LGRAPH_H

  m.def("find_lgraph", &LGraph::find_lgraph, "Returns if exists or creates a new lgraph");
  m.def("open_lgraph", &LGraph::open_lgraph, "Open if exists or creates a new lgraph");

  py::class_<LGraph>(m, "LGraph")
    .def(py::init<const std::string &>())
    .def("lg_id", &LGraph::lg_id)
    .def("dump",  &LGraph::dump, "Dumps to screen the graph. It can be lots of data!!");

