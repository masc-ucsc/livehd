#ifdef PY_INOU_RAND_H
#error "python lgraph modules can be included only once in pyth/lgraph.cpp"
#endif
#define INOU_RAND_H

  py::class_<Inou_rand>(m, "Inou_rand")
    .def(py::init<>())
    .def(py::init<const py::dict &>())
    .def("generate", &Inou_rand::py_generate, "Generate set of random graphs")
    .def("set", &Inou_rand::py_set, "set plugin options");

