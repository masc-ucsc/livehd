#include <pybind11/pybind11.h>
#include <iostream>
#include <string>
using namespace std;

int add(int i, int j) {
    return i + j;
}

string hello(void){
    return "Hello, World!";
}

class hellohello{
    private:
    	string hello_world;
    public:
    	void setHello(){
	    hello_world = "Hello, World!\n";
	}
	string getHello(){
	    return hello_world;
	}
};

int main(){
    hellohello example;
    example.setHello();
}

namespace py = pybind11;

PYBIND11_MODULE(python_example2, m) {
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
