//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "def2lg.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>

#include "inou_def.hpp"
#include "perf_tracing.hpp"

int def_net_cb(defrCallbackType_e type, defiNet *fnet, defiUserData ud) {
  (void)type;

  Def_info &dinfo = *((Def_info *)ud);
  dinfo.nets.resize(dinfo.nets.size() + 1);
  Def_net &tmp_net = dinfo.nets.back();
  tmp_net.name     = fnet->name();
  tmp_net.conns.resize(fnet->numConnections());
  for (int i = 0; i < fnet->numConnections(); i++) {
    Def_conn &tmp_conn  = tmp_net.conns[i];
    tmp_conn.pin_name   = fnet->pin(i);
    tmp_conn.compo_name = fnet->instance(i);
  }
  // cout << tmp_net.name << endl;
  // for(int i = 0 ; i < fnet->numConnections(); i++){
  //  cout << " ( " << tmp_net.conns[i].compo_name << " " << tmp_net.conns[i].pin_name << " ) ";
  //}
  // cout<<endl;
  return 0;
}

int def_component_cb(defrCallbackType_e type, defiComponent *fcompo, defiUserData ud) {
  (void)type;
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.compos.resize(dinfo.compos.size() + 1);
  Def_component &tmp_compo = dinfo.compos.back();

  tmp_compo.name        = fcompo->id();
  tmp_compo.macro_name  = fcompo->name();
  tmp_compo.posx        = fcompo->placementX();
  tmp_compo.posy        = fcompo->placementY();
  tmp_compo.orientation = fcompo->placementOrientStr();
  tmp_compo.is_fixed    = fcompo->isFixed();
  tmp_compo.is_placed   = fcompo->isPlaced();

  // if(tmp_compo.is_fixed)
  //  cout << " - " << tmp_compo.name << " " << tmp_compo.macro_name << " + " << "FIXED" << " ( " << tmp_compo.posx << " " <<
  //  tmp_compo.posy << " ) " << tmp_compo.orientation << endl;
  // if(tmp_compo.is_placed)
  //  cout << " - " << tmp_compo.name << " " << tmp_compo.macro_name << " + " << "PLACED" << " ( " << tmp_compo.posx << " " <<
  //  tmp_compo.posy << " ) " << tmp_compo.orientation << endl;

  return 0;
}

int def_io_cb(defrCallbackType_e type, defiPin *fpin, defiUserData ud) {
  (void)type;
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.ios.resize(dinfo.ios.size() + 1);
  Def_io &tmp_io = dinfo.ios.back();

  tmp_io.io_name  = fpin->pinName();
  tmp_io.net_name = fpin->netName();
  if (strcmp(fpin->direction(), "INPUT") == 0) {
    tmp_io.dir = Def_io::Direction::input;
  } else if (strcmp(fpin->direction(), "OUTPUT") == 0) {
    tmp_io.dir = Def_io::Direction::output;
  } else if (strcmp(fpin->direction(), "INOUT") == 0) {
    tmp_io.dir = Def_io::Direction::input;
  }

  tmp_io.posx = fpin->placementX();
  tmp_io.posy = fpin->placementY();
  if (fpin->hasLayer()) {
    tmp_io.phy.metal_name = fpin->layer(0);
    int xl, yl, xh, yh;
    fpin->bounds(0, &xl, &yl, &xh, &yh);
    tmp_io.phy.xl = xl;
    tmp_io.phy.xh = xh;
    tmp_io.phy.yl = yl;
    tmp_io.phy.yh = yh;
  }
  // cout << "io name = " << tmp_io.io_name << " direction = " << tmp_io.dir << endl;
  return 0;
}

int def_track_cb(defrCallbackType_e type, defiTrack *ftrack, defiUserData ud) {
  (void)type;
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.tracks.resize(dinfo.tracks.size() + 1);
  Def_track &tmp_track = dinfo.tracks.back();

  tmp_track.direction  = ftrack->macro();
  tmp_track.location   = ftrack->x();
  tmp_track.num_tracks = ftrack->xNum();
  tmp_track.space      = ftrack->xStep();
  tmp_track.layers.reserve(ftrack->numLayers());
  for (int i = 0; i < ftrack->numLayers(); i++) {
    tmp_track.layers.push_back(ftrack->layer(i));
  }

  // cout << "TRACKS" << tmp_track.direction << " " << tmp_track.location << " DO " << tmp_track.num_tracks << " STEP " <<
  // tmp_track.space <<" LAYER " << tmp_track.layers.back() << endl;

  return 0;
}

int def_design_cb(defrCallbackType_e type, const char *string, defiUserData ud) {
  (void)type;

  Def_info &dinfo = *((Def_info *)ud);
  dinfo.mod_name  = string;
  return 0;
}

int def_row_cb(defrCallbackType_e type, defiRow *frow, defiUserData ud) {
  (void)type;
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.rows.resize(dinfo.rows.size() + 1);
  Def_row &tmp_row = dinfo.rows.back();

  tmp_row.name   = frow->name();
  tmp_row.site   = frow->macro();
  tmp_row.origx  = frow->x();
  tmp_row.origy  = frow->y();
  tmp_row.orient = frow->orient();
  if (frow->hasDoStep()) {
    tmp_row.numx  = frow->xNum();
    tmp_row.numy  = frow->yNum();
    tmp_row.stepx = frow->xStep();
    tmp_row.stepy = frow->yStep();
  }
  // cout << "ROW " << tmp_row.name << " " << tmp_row.site << " " << tmp_row.origx
  //     << " "    << tmp_row.origy<< " " << tmp_row.orient << " DO " << tmp_row.numx
  //     << " BY "  << tmp_row.numy << " STEP " << tmp_row.stepx << " " << tmp_row.stepy << endl;
  return 0;
}

void def_parsing(Def_info &dinfo, std::string_view def_file_name) {
  std::string def_file{def_file_name};
  defrInit();  // initialize the reader, This routine must be called first

  FILE *fin = fopen(def_file.c_str(), "r");
  if (fin == NULL) {
    Pass::error("def_parsing: could not open def input file {}", def_file_name);
    return;
  }

  defrSetDesignCbk(def_design_cb);
  defrSetRowCbk(def_row_cb);
  defrSetTrackCbk(def_track_cb);
  defrSetComponentCbk(def_component_cb);
  defrSetPinCbk(def_io_cb);
  defrSetNetCbk(def_net_cb);
  defrReset();

  // Def_info object is your userData, pass pointer of it into defrRead(), and it will return as a argument in your user-defined
  // callback routine
  defrRead(fin, def_file.c_str(), (void *)&dinfo, 1);
  fclose(fin);
}
