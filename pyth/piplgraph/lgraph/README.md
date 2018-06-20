# Python-Wrapper Pip3 package.

This README shows how to make lgraph/ into a pip3 package that is uploaded on pypi.

## Prerequisites

Have lgraph/ cloned into your directory. 

## Getting Started
Make sure you have latest setuptools and wheel installed:
```
$ python3 -m pip install --user --upgrade setuptools wheel
```
Execute this command from same directory where setup.py is located:
```
$ python3 setup.py sdist bdist_wheel
```
IMPORTANT: This will create dist/ with:
```
dist/
  lgraph-0.0.1-cp36-cp36m-linux_x86_64.whl
  lgraph-0.0.1.tar.gz 
```

Change name .whl file to be the following:
```
dist/
  lgraph-0.0.1-cp36-cp36m-manylinux1_x86_64.whl
  lgraph-0.0.1.tar.gz
```

Execute:
```
$ python3 -m pip install --user --upgrade twine
$ twine upload --repository-url https://test.pypi.org/legacy/ dist/*
$ (Enter User/Pass) - Email Tanvir for this.
```

To install new uploaded package onto local machine:
```
python3 -m pip install --index-url https://test.pypi.org/simple/ lgraph
```

### Testing 

Reference: https://packaging.python.org/tutorials/packaging-projects/

```
$ python3
$ >> from lgraph import *
$ >> s = speak()
$ >> call_hello(s)

OUTPUT: "Hello, World!"

```
