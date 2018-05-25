#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "inou/cfg/inou_cfg.hpp"
#include <vector>

int main(int argc, const char **argv) {

  LGBench b;
  Options::setup(argc, argv);

  Inou_cfg cfg;

  Options::setup_lock();

  std::string str = "-128";
  //std::string str        = "0xFFFFFFFFs";
  bool v_signed          = false;
  uint32_t explicit_bits = 0;
  uint32_t val           = 0;


  for(int i = 0; i<1 ; i++){
    prp_get_value (str, v_signed, explicit_bits, val);
    fmt::print("signed:{}\n",v_signed);
    fmt::print("value:{}\n",val);
    fmt::print("explicit_bits:{}\n",explicit_bits);
  }
  std::vector<LGraph *> rvec = cfg.generate();

  //for (auto &g:rvec) {
  //	cfg.generate(g);
  //}
}
