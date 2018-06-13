
#include "lgraph.hpp"
#include "inou_rand.hpp"
#include "inou_abc.hpp"
//#include "pass_dfg.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(std::vector<LGraph *>);

PYBIND11_MODULE(lgraph, m) {
    // optional module docstring
    m.doc() = "pybind11 lgraph base plugin";
    py::bind_vector<std::vector<LGraph *>>(m, "VectorLGraph");

#include "py_lgraph.hpp"
#include "py_inou_rand.hpp"
#include "py_inou_abc.hpp"
//#include "py_pass_dfg.hpp"

}

