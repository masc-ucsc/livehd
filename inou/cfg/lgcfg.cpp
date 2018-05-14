
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "inou/cfg/inou_cfg.hpp"

int main(int argc, const char **argv) {

  LGBench b;
  Options::setup(argc, argv);

  Inou_cfg cfg;

  Options::setup_lock();
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
