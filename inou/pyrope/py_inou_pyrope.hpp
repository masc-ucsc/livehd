//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifdef PY_INOU_PYROPE_H
#error "python lgraph modules can be included only once in pyth/lgraph.cpp"
#endif
#define INOU_PYROPE_H

  py::class_<Inou_pyrope>(m, "Inou_pyrope")
    .def(py::init<>())
    .def(py::init<const py::dict &>())
    .def("generate", &Inou_pyrope::py_generate, "Generate set of random graphs")
    .def("set", &Inou_pyrope::py_set, "set plugin options");

