#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass/dfg/pass_dfg.hpp"

int main(int argc, const char **argv) {
  LGBench b;
  Options::setup(argc, argv);

  Pass_dfg dfg;

  Options::setup_lock();

  //dfg.test_const_conversion();
  dfg.transform();

  return 0;
}