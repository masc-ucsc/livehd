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


  //char str[] = "0b10001s8";
  ////char str[] = "0xFF_f_fs32";
  //bool v_signed;
  //uint32_t bits;
  //uint32_t explicit_bits;
  //uint32_t val;

  //prp_get_value(str, v_signed, bits, explicit_bits, val);

  std::vector<LGraph *> rvec = cfg.generate();

  int ctr = 0;
  for(auto *lg : rvec) {
    const std::string fname = "cfg" + std::to_string(ctr++) + ".dot";
    cfg.cfg_2_dot(lg, fname);
  }

  //for (auto &g:rvec) {
  //	cfg.generate(g);
  //}
}
