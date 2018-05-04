
#include "lgraph.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"

#include "inou/cfg/inou_cfg.hpp"

int main(int argc, const char **argv) {

	LGBench b;
	Options::setup(argc, argv);

	Inou_cfg cfg;

	Options::setup_lock();
	std::vector<LGraph *> rvec = cfg.generate();

	//for (auto &g:rvec) {
	//	cfg.generate(g);
	//}
}