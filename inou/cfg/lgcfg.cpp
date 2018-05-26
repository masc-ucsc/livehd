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

  //std::string str_in = "-128";
  //std::string str_in       = "0b0001111_1111_1111_1111_1111_1111_1111_1111s";//buggy underscore
  //std::string str_in       = "0b00011111111111111111111111111111111s";
  //std::string str_in = "-2147483647";//buggy
  std::string str_in = "-2147483647";
  std::string str_out;
  bool v_signed          = false;
  uint32_t explicit_bits = 0;
  uint32_t val           = 0;
  bool within_32b;

  for(int i = 0; i<1 ; i++){
    within_32b = prp_get_value (str_in,str_out, v_signed, explicit_bits, val);
    fmt::print("out of range: {}\n",!within_32b);
    fmt::print("signed: {}\n",v_signed);
    fmt::print("value: {}\n",val);
    fmt::print("explicit_bits: {}\n",explicit_bits);
    fmt::print("const string: {}\n",str_out);
  }
  std::vector<LGraph *> rvec = cfg.generate();

  //for (auto &g:rvec) {
  //	cfg.generate(g);
  //}
}
