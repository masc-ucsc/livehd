
#include <sys/stat.h>
#include <sys/types.h>

#include <vector>

#include "py_options.hpp"

#include "lglog.hpp"

void Py_options::set_val(const std::string &key, const py::handle &handle) {

  if ( is_opt(key,"lgdb") ) {
    const auto &val = handle.cast<std::string>();
    lgdb = val;
    mkdir(lgdb.c_str(), 0755); // boost works, but trying to remove dependency
  }else if ( is_opt(key,"graph_name") ) {
    const auto &val = handle.cast<std::string>();
    graph_name = val;
  }else{
    fmt::print("WARNING: key {} is not recognized\n",key);
  }
}
