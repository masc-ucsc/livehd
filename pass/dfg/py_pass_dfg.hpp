//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifdef PY_PASS_DFG_H
#error "python lgraph modules can be included only once in pyth/lgraph.cpp"
#endif
#define PY_PASS_DFG_H

  py::class_<Pass_dfg>(m, "Pass_dfg")
    .def(py::init<>())
    .def(py::init<const py::dict &>())
    .def("generate", &Pass_dfg::py_generate, "Generate a Dataflow graph from a Control flow graph")
    .def("set", &Pass_dfg::py_set, "set plugin options");

