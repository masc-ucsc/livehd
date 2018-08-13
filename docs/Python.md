
# Python version

The current version of lgraph assumes a specific Python version. It needs python3, but
it works with 3.6m and 3.7m. To select the specific version, you must change these files:

pyth/BUILD
  The linkopts should point to the correct library -lpython3.7m or -lpythong3.?m

  change the linkso name to match the version. E.g:
    name = "lgraph.cpython-36m-x86_64-linux-gnu",
    name = "lgraph.cpython-37m-x86_64-linux-gnu",

WORKSPACE
  new_local_repository pointing to python should use the correct path /usr/include/python3.7m 




# Instructions to create/upload the Python package


The package contains binaries and libraries. To create a source distribution use:

   python setup.py sdist


TODO: A binary bdist_wheel must be created because of lgraph dependence

To upload the distribution to the pip package

   twine upload dist/*
