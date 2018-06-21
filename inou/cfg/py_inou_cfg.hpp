#ifdef PY_INOU_CFG_H
#error "python lgraph modules can be included only once in pyth/lgraph.cpp"
#endif
#define INOU_CFG_H

  py::class_<Inou_cfg>(m, "Inou_cfg")
    .def(py::init<>())
    .def(py::init<const py::dict &>())
    .def("generate", &Inou_cfg::py_generate, "Generate set of control flow graphs")
    .def("set", &Inou_cfg::py_set, "set plugin options");

