//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lglefdef.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <iostream>
#include <random>

#include "inou_def.hpp"
#include "lgbench.hpp"
#include "tech_library.hpp"

int main(int argc, const char **argv) {
  LGBench b;
  Options::setup(argc, argv);

  Inou_def def;
  Options::setup_lock();
  b.sample("setup");

  Tech_library *tlib = Tech_library::instance(def.get_opack().lgdb_path);
  // call global function lef_parsing(), which uses cadence api to parse lef information.
  lef_parsing(tlib, def.get_opack().lef_file);

  b.sample("lef-done");

  Def_info dinfo;
  def_parsing(dinfo, def.get_opack().def_file);

  def.set_def_info(dinfo);

  b.sample("def-parse");

  // Temp!! test idea of a giant node with chip_frame type
  tlib->create_cell_id("chip_frame");
  Tech_cell &tmp_cell = tlib->get_vec_cell_types()->back();

  for (auto iter_io = dinfo.ios.begin(); iter_io != dinfo.ios.end(); ++iter_io) {
    Tech_cell::Direction dir = Tech_cell::Direction::input;
    if (iter_io->dir == 0)
      dir = Tech_cell::Direction::input;
    else if (iter_io->dir == 1)
      dir = Tech_cell::Direction::output;
    else
      assert(false);  // Unhandled direction option

    tmp_cell.add_pin(iter_io->io_name, dir);
    Tech_cell::Pin &tmp_pin = tmp_cell.get_vec_pins()->back();

    tmp_pin.phys.resize(tmp_pin.phys.size() + 1);
    Tech_cell::Physical_pin &tmp_phy = tmp_pin.phys.back();
    tmp_phy.metal_name               = iter_io->phy.metal_name;
    tmp_phy.xh                       = iter_io->phy.xh;
    tmp_phy.xl                       = iter_io->phy.xl;
    tmp_phy.yh                       = iter_io->phy.yh;
    tmp_phy.yl                       = iter_io->phy.yl;
  }  // end for

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
