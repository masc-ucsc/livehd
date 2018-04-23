//
// Created by birdeclipse on 1/30/18.
//
#include "inou_abc.hpp"
#include "lgbench.hpp"

int main(int argc, const char **argv) {
	LGBench b;
	Options::setup(argc, argv);
	Inou_abc abc;
	Options::setup_lock();
	std::vector<LGraph *> rvec = abc.generate();
	for (auto &g : rvec) {
		abc.generate(g);
	}
	b.sample("abc");
}