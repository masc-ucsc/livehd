#include <pybind11/pybind11.h>
#include <iostream>
#include <string>
using namespace std;



//add function
int add(int i, int j) {
    return i + j;
}

// hello world function
string hello(void){
    return "Hello, World!";
}

// hello world virtual class
class virtual_hello{
  public:
    virtual ~virtual_hello(){ }
    virtual std::string say_hello(int n_times) = 0;
};

// class called speak which extends virtual_hello
class speak : public virtual_hello{
  public:
    std::string say_hello(int n_times) override{
      std::string result;
      for (int i=0; i<n_times; ++i)
        result += "Hello, World!";
      return result;
    }
};


// function that uses the class
std::string call_hello(virtual_hello *example){
  return example->say_hello(1);
}



//python class which extends virtual_hello
class PyHello : public virtual_hello {
  public:
    //Inherit the constructors
    using virtual_hello::virtual_hello;

  //trampoline (need one for each virtual funcion)

    std::string say_hello(int n_times) override {
      PYBIND11_OVERLOAD_PURE(
          std::string,    //return
          virtual_hello,  //parent class
          say_hello,      //function
          n_times         //arguments
      );
    }
};

namespace py = pybind11;

//pybind module
PYBIND11_MODULE(python_example2, m) {
  py::class_<virtual_hello, PyHello> (m, "virtual_hello")
      .def(py::init<>())
      .def("say_hello", &virtual_hello::say_hello);

  py::class_<speak, virtual_hello>(m, "speak")
      .def(py::init<>());
    
  m.def("call_hello", &call_hello);
  
  
  
  m.doc() = R"pbdoc(
        Pybind11 example2 plugin
        -----------------------

        .. currentmodule:: python_example2

        .. autosummary::
           :toctree: _generate

           add
           subtract
	   hello
	   helloclass
    )pbdoc";


    m.def("add", &add, R"pbdoc(
        Add two numbers

        Some other explanation about the add function.
    )pbdoc");

    m.def("subtract", [](int i, int j) { return i - j; }, R"pbdoc(
        Subtract two numbers

        Some other explanation about the subtract function.
    )pbdoc");

    m.def("hello", &hello, R"pbdoc(
	Display hello world
    )pbdoc");

    m.def("helloclass",&hello, R"pbdoc(
	Display hello world using a class
    )pbdoc");

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}
