//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifdef PY_INOU_ABC_H
#error "python lgraph modules can be included only once in pyth/lgraph.cpp"
#endif
#define INOU_ABC_H

#include <pybind11/pybind11.h>
namespace py = pybind11;

  py::class_<Inou_abc>(m, "Inou_abc")
    .def(py::init<>())
    .def(py::init<const py::dict &>())
    .def("generate", &Inou_abc::py_generate, "Perform techmapping using abc")
    .def("set", &Inou_abc::py_set, "set plugin options");

