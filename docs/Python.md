
# Python version

The current version of lgraph assumes a specific Python version. It needs python3, but
it works with 3.6m and 3.7m. To select the specific version, you must change these files:

pyth/BUILD
  The linkopts should point to the correct library -lpython3.7m or -lpythong3.?m

  change the linkso name to match the version. E.g:
    name = "lgraph.cpython-36m-x86_64-linux-gnu",
    name = "lgraph.cpython-37m-x86_64-linux-gnu",

  change the par_binary data to match the version too. E.g:
    data = ["lgraph.cpython-36m-x86_64-linux-gnu.so"],
    data = ["lgraph.cpython-37m-x86_64-linux-gnu.so"],

WORKSPACE
  new_local_repository pointing to python should use the correct path /usr/include/python3.7m 

# Instructions to run python console

 bazel build //...
 python bazel-bin/pyth/ptlgraph.py

 bazel build //...
 mkdir tmp
 cd tmp
 unzip ../bazel-bin/pyth/ptlgraph.par
 python __main__/pyth/ptlgraph.py

# Build a Python interface steps

1-Create a pybind11 class interface in your pass directory. It must be named py_XXX.hpp
where XXX is the pass name. Use a py_XXX.hpp as your starting point. E.g: py_inou_rand.hpp.

2-Use the python options. Must include py_options.hpp in the main XXX.hpp file for the pass. E.g: inou_rand.hpp

3-Create the constructor, py_set and py_generate methods as in the examples to get options from python.

4-In the BUILD inside the XXX directory, add a //pyth:py_base dependence

5-Add to pyth/py_lgraph.cpp your new include (py_XXX.hpp).

6-Add the dependence in pyth/BUILD (check //inou/rand:inou_rand)

7-Create a simple unit test that covers the basic functionality. The basic tests are named test_XXX.py, and they are located at pyth. If you have more extensive tests, you have more tests name them test_XXX1.py. If you have several input/output files for your tests, place them in XXX/tests where XXX is your pass directory. E.g: inou/yosys/tests/

# Instructions to create/upload the Python package (DEPRECATED)

The package contains binaries and libraries. To create a source distribution use:

   python setup.py sdist

TODO: A binary bdist_wheel must be created because of lgraph dependence

To upload the distribution to the pip package

   twine upload dist/*
