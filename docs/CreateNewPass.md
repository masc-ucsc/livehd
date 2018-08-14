
# High level steps

1. Create a C++ directory inside inou or pass. Inou is for input/output from/to lgraph, and pass is to transform an existing lgraph (lgraph 2 lgraph).
2. Create a BUILD for the directory
3. Create a python interface
4. Create unit tests

## New XXX C++ Pass

## New XXX C++ Inou

## new BUILD

## new python interface

In your C++ directory for the inou/pass, create a pybind11 file called
py_inou_XXX.hpp or py_pass_XXX.hpp. See py_inou_rand.hpp or py_pass_dfg.hpp as
reference.

The pybind11 is the file that interface C++ with python. In the main C++ file (inou_rand.hpp) you must include the py_options.hpp because it handles the parameters and options from python. You must create a new XXX_options class that inherits from Py_options.

The best is to look at inou_rand.hpp (or other inou/pass) and see how it interfaces.

In the pyth/py_lgraph.cpp, add your new pybind11 file.


