#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <random>
#include <core/tech_library.hpp>

#include "lglefdef.hpp"
#include "lgbench.hpp"
#include "inou_def.hpp"

using namespace std;

int main(int argc, const char **argv) {
	LGBench b;
	Options::setup(argc, argv);

  Inou_def def;
  Options::setup_lock();
  b.sample("setup");

	Tech_library *tlib = Tech_library::instance(def.get_opack().lgdb_path);
	lef_parsing(tlib, def.get_opack().lef_file); //call global function lef_parsing(), which uses cadence api to parse lef information.

	b.sample("lef-done");

	Def_info dinfo;
	def_parsing(dinfo, def.get_opack().def_file);

	def.set_def_info(dinfo);

	b.sample("def-parse");

	//Temp!! test idea of a giant node with chip_frame type
	tlib->create_cell_id("chip_frame");
	Tech_cell &tmp_cell = tlib->get_vec_cell_types()->back();

	for (auto iter_io = dinfo.ios.begin(); iter_io != dinfo.ios.end(); ++iter_io) {
		tmp_cell.add_pin(iter_io->io_name);
		Tech_cell::Pin &tmp_pin = tmp_cell.get_vec_pins()->back();

		if (iter_io->dir == 0)
			tmp_pin.dir = Tech_cell::Direction::input;
		else if (iter_io->dir == 1)
			tmp_pin.dir = Tech_cell::Direction::output;

		tmp_pin.phys.resize(tmp_pin.phys.size() + 1);
		Tech_cell::Physical_pin &tmp_phy = tmp_pin.phys.back();
		tmp_phy.metal_name = iter_io->phy.metal_name;
		tmp_phy.xh = iter_io->phy.xh;
		tmp_phy.xl = iter_io->phy.xl;
		tmp_phy.yh = iter_io->phy.yh;
		tmp_phy.yl = iter_io->phy.yl;
	}//end for


	b.sample("chip-level");

	auto vgen = def.generate();
	assert(vgen.size() == 1);

	b.sample("def-done");

	/*r(auto &g:vgen){
		g->print_stats();
		json.generate(g);
	}*/

	return 0;
}

