
# Instructions to create/upload the Python package


The package contains binaries and libraries. To create a source distribution use:

   python setup.py sdist


TODO: A binary bdist_wheel must be created because of lgraph dependence

To upload the distribution to the pip package

   twine upload dist/*
