__path__ = __import__('pkgutil').extend_path(__path__, __name__)
name = "lgraph"

from python_example2 import *
s = speak()
x = call_hello(s)
print(x)
