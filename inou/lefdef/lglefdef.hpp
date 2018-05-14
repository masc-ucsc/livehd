#ifndef GUARD_LGLEFDEF
#define GUARD_LGLEFDEF

#include "defrReader.hpp"
#include "lefrReader.hpp"
#include <core/tech_library.hpp>
#include <fstream>
#include <iostream>
#include <string>
//#include "lefiUtil.hpp"
//#include "lefdef_json.hpp"
#include "inou_def.hpp"

//***************************************************
//************LEF FILE PARSING START!****************
//***************************************************

// user supplied callback routines

//FIXME: remove any non-class method from the hpp file. They should be in the
//cpp file where they are used.

// collect macro lef information by using "three" user-defined call function, this usage first looked strange, this is due to cadence api architecture. no other way if you want to use cadence parser.
int lef_macro_begin_cb(lefrCallbackType_e c, const char *macroName, lefiUserData ud) {
  Tech_library *tlib = (Tech_library *)ud;

  //in create_cell_id(), it will create a new cell type for vector cell_types
  tlib->create_cell_id(macroName);
  return 0;
}

//fmacro means macro information from lef file
int lef_macro_cb(lefrCallbackType_e c, lefiMacro *fmacro, lefiUserData ud) {
  Tech_library *tlib = (Tech_library *)ud;

  //note!! you "have to" create a new cell type in "lef_macro_begin_cb" callback function, or the cell will not really create for the sub-sequence cb,e.g. macro_cb and pin_cb
  //tlib->create_cell_id(fmacro->name());

  //and then use tmp_cell as reference of Tech_library::cell_types.back() to retrieve out the lef
  Tech_cell &tmp_cell = tlib->get_vec_cell_types()->back();
  if(fmacro->hasSize()) {
    tmp_cell.set_cell_size(fmacro->sizeX(), fmacro->sizeY());
  }
  return 0;
}

int lef_pin_cb(lefrCallbackType_e c, lefiPin *fpin, lefiUserData ud) { //fpin means pin information from lef file

  Tech_library *tlib = (Tech_library *)ud;

  //if(strcmp(fpin->use(), "GROUND") == 0) return 0 ; //should we ignore POWER and GROUND pin?
  //if(strcmp(fpin->use(), "POWER") == 0) return 0 ;

  int pn; //variable for port number

  Tech_cell &tmp_cell = tlib->get_vec_cell_types()->back();

  tmp_cell.add_pin(fpin->name());
  Tech_cell::Pin &tmp_pin = tmp_cell.get_vec_pins()->back();

  if(fpin->hasDirection()) {
    if(strcmp(fpin->direction(), "INPUT") == 0)
      tmp_pin.dir = Tech_cell::Direction::input;
    else if(strcmp(fpin->direction(), "OUTPUT") == 0)
      tmp_pin.dir = Tech_cell::Direction::output;
    else if(strcmp(fpin->direction(), "INOUT") == 0) //we will set inout as input in lgraph
      tmp_pin.dir = Tech_cell::Direction::input;
  }

  if(fpin->hasUse())
    tmp_pin.use = fpin->use();

  for(pn = 0; pn < fpin->numPorts(); pn++) {
    const lefiGeometries *geometry = fpin->port(pn);

    for(int i = 0; i < geometry->numItems(); i++) {

      //if(geometry->itemType(i) == lefiGeomLayerE){
      //  tmp_phy.metal_name = geometry->lefiGeometries::getLayer(i);
      //}

      if(geometry->itemType(i) == lefiGeomRectE) {
        tmp_pin.phys.resize(tmp_pin.phys.size() + 1); //create a temporary object inline
        Tech_cell::Physical_pin &tmp_phy = tmp_pin.phys.back();
        const lefiGeomRect *     rect    = geometry->getRect(i);

        std::string metal_name_str(geometry->lefiGeometries::getLayer(i - 1));
        if(strncmp(metal_name_str.c_str(), "Metal", 5) == 0)
          tmp_phy.metal_name = geometry->lefiGeometries::getLayer(i - 1);
        else
          tmp_phy.metal_name = geometry->lefiGeometries::getLayer(0);
        //tmp_pin.phy.rects.resize(tmp_pin.phy.rects.size()+1);
        //Tech_cell::Rect& tmp_rect = tmp_pin.phy.rects.back();
        tmp_phy.xl = rect->xl;
        tmp_phy.yl = rect->yl;
        tmp_phy.xh = rect->xh;
        tmp_phy.yh = rect->yh;
      }
    } //end inner for
  }   //end outer for

  //pin ports parsing end
  return 0;
}

int lef_layer_cb(lefrCallbackType_e c, lefiLayer *flayer, lefiUserData ud) { //flayer means layer information from lef file
  Tech_library *tlib = (Tech_library *)ud;                                               //convert void* into Tech_library*
  int i, j, k;
  lefiSpacingTable *spTable;
  lefiParallel *parallel;

  if(strcmp(flayer->name(), "OVERLAP") == 0) {
    return 0; //do nothing when layer name = OVERLAP
  }

  tlib->increase_vec_layers_size(); //whenever user-defined routine(layerCb) is called, the data member Tech_library::layers will increase size by one to be ready to contain new info.

  Tech_layer &tmp_layer = tlib->get_vec_layers()->back(); //and then use tmp_layer as reference of Tech_library::layers.back() to retrieve out the lef data the callback routine returned

  tmp_layer.name = flayer->name();

  if(flayer->hasDirection()) {
    if(strcmp(flayer->direction(), "HORIZONTAL") == 0)
      tmp_layer.horizontal = true;
    else
      tmp_layer.horizontal = false;
  }

  if(flayer->hasMinwidth()) {
    tmp_layer.minwidth = flayer->minwidth();
  }

  if(flayer->hasArea()) {
    tmp_layer.area = flayer->area();
  }

  if(flayer->hasXYPitch()) {
    tmp_layer.pitches.push_back(flayer->pitchX());
    tmp_layer.pitches.push_back(flayer->pitchY());
  }

  tmp_layer.width = flayer->width(); //if (flayer->hasWidth()) //bugy with Layer Via width detection, so remove condition judgement

  if(flayer->hasSpacingNumber()) {
    for(i = 0; i < flayer->numSpacing(); i++) {
      if(i == 0)
        tmp_layer.spacing = flayer->spacing(i);
      else if(flayer->hasSpacingEndOfLine(i)) {
        tmp_layer.spacing_eol.push_back(flayer->spacing(i));
        tmp_layer.spacing_eol.push_back(flayer->spacingEolWidth(i));
        tmp_layer.spacing_eol.push_back(flayer->spacingEolWithin(i));
      }
    } //end for
  }

  for(i = 0; i < flayer->numSpacingTable(); i++) {
    spTable = flayer->spacingTable(i);
    if(spTable->lefiSpacingTable::isParallel()) {
      parallel = spTable->lefiSpacingTable::parallel();
      for(j = 0; j < parallel->lefiParallel::numLength(); j++)
        tmp_layer.spctb_prl = parallel->lefiParallel::length(j);

      for(j = 0; j < parallel->lefiParallel::numWidth(); j++) {
        tmp_layer.spctb_width.push_back(parallel->lefiParallel::width(j));
        for(k = 0; k < parallel->lefiParallel::numLength(); k++)
          tmp_layer.spctb_spacing.push_back(parallel->lefiParallel::widthSpacing(j, k));
      }
    } //end if
  }
  return 0;
}

int lef_via_cb(lefrCallbackType_e c, lefiVia *fvia, lefiUserData ud) { //fvia means via information from lef file
  Tech_library *tlib = (Tech_library *)ud;                                         //convert void* into Tech_library*

  tlib->increase_vec_vias_size();           //whenever user-defined routine(layerCb) is called, the data member Tech_library::vias will increase size by one to be ready to contain new info.
  Tech_via &tmp_via = tlib->get_vec_vias()->back(); //and then use tmp_via as reference of Tech_library::vias.back() to retrieve out the lef data the callback routine returned

  tmp_via.name = fvia->name();

  tmp_via.vlayers.resize(fvia->numLayers());
  for(int i = 0; i < fvia->numLayers(); i++) {
    Tech_via_layer &tmp_vlayer = tmp_via.vlayers[i];
    tmp_vlayer.layer_name      = fvia->layerName(i);
    for(int j = 0; j < fvia->numRects(i); j++) {
      tmp_vlayer.rect.xl = fvia->xl(i, j);
      tmp_vlayer.rect.yl = fvia->yl(i, j);
      tmp_vlayer.rect.xh = fvia->xh(i, j);
      tmp_vlayer.rect.yh = fvia->yh(i, j);
    }
  }
  return 0;
}
void lef_parsing(Tech_library *tlib, std::string lef_file_name) {

  tlib->clear_tech_lib();

  const char *lef_file = lef_file_name.c_str();
  int         res;
  FILE *      fin;
  //char* userData = NULL; //this is an example of userData, but I will not use this, in my case, the userData will be a data structure for lef information hierarchy
  lefrInit(); //initialize the reader, This routine must be called first
  fin = fopen(lef_file, "r");
  if(fin == NULL)
    std::cout << "Couldn't open input file" << std::endl;
  lefrSetMacroBeginCbk(lef_macro_begin_cb);
  lefrSetMacroCbk(lef_macro_cb);
  lefrSetPinCbk(lef_pin_cb);
  lefrSetLayerCbk(lef_layer_cb);
  lefrSetViaCbk(lef_via_cb);
  lefrReset();
  res = lefrRead(fin, lef_file, (void *)tlib); //Tech_file object is your userData, pass pointer of it into lefrRead(), and it will return as a argument in your user-defined callback routine
  fclose(fin);
}

//***************************************************
//************DEF FILE PARSING START!****************
//***************************************************

int def_net_cb(defrCallbackType_e type, defiNet *fnet, defiUserData ud) {
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.nets.resize(dinfo.nets.size() + 1);
  Def_net &tmp_net = dinfo.nets.back();
  tmp_net.name     = fnet->name();
  tmp_net.conns.resize(fnet->numConnections());
  for(int i = 0; i < fnet->numConnections(); i++) {
    Def_conn &tmp_conn  = tmp_net.conns[i];
    tmp_conn.pin_name   = fnet->pin(i);
    tmp_conn.compo_name = fnet->instance(i);
  }
  //cout << tmp_net.name << endl;
  //for(int i = 0 ; i < fnet->numConnections(); i++){
  //  cout << " ( " << tmp_net.conns[i].compo_name << " " << tmp_net.conns[i].pin_name << " ) ";
  //}
  //cout<<endl;
  return 0;
}

int def_component_cb(defrCallbackType_e type, defiComponent *fcompo, defiUserData ud) {
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

  //if(tmp_compo.is_fixed)
  //  cout << " - " << tmp_compo.name << " " << tmp_compo.macro_name << " + " << "FIXED" << " ( " << tmp_compo.posx << " " << tmp_compo.posy << " ) " << tmp_compo.orientation << endl;
  //if(tmp_compo.is_placed)
  //  cout << " - " << tmp_compo.name << " " << tmp_compo.macro_name << " + " << "PLACED" << " ( " << tmp_compo.posx << " " << tmp_compo.posy << " ) " << tmp_compo.orientation << endl;

  return 0;
}

int def_io_cb(defrCallbackType_e type, defiPin *fpin, defiUserData ud) {
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.ios.resize(dinfo.ios.size() + 1);
  Def_io &tmp_io = dinfo.ios.back();

  tmp_io.io_name  = fpin->pinName();
  tmp_io.net_name = fpin->netName();
  if(strcmp(fpin->direction(), "INPUT") == 0)
    tmp_io.dir = Def_io::Direction::input;
  else if(strcmp(fpin->direction(), "OUTPUT") == 0)
    tmp_io.dir = Def_io::Direction::output;
  else if(strcmp(fpin->direction(), "INOUT") == 0)
    tmp_io.dir = Def_io::Direction::input;

  tmp_io.posx = fpin->placementX();
  tmp_io.posy = fpin->placementY();
  if(fpin->hasLayer()) {
    tmp_io.phy.metal_name = fpin->layer(0);
    int xl, yl, xh, yh;
    fpin->bounds(0, &xl, &yl, &xh, &yh);
    tmp_io.phy.xl = xl;
    tmp_io.phy.xh = xh;
    tmp_io.phy.yl = yl;
    tmp_io.phy.yh = yh;
  }
  //cout << "io name = " << tmp_io.io_name << " direction = " << tmp_io.dir << endl;
  return 0;
}

int def_track_cb(defrCallbackType_e type, defiTrack *ftrack, defiUserData ud) {
  Def_info &dinfo = *((Def_info *)ud);
  dinfo.tracks.resize(dinfo.tracks.size() + 1);
  Def_track &tmp_track = dinfo.tracks.back();

  tmp_track.direction  = ftrack->macro();
  tmp_track.location   = ftrack->x();
  tmp_track.num_tracks = ftrack->xNum();
  tmp_track.space      = ftrack->xStep();
  tmp_track.layers.reserve(ftrack->numLayers());
  for(int i = 0; i < ftrack->numLayers(); i++)
    tmp_track.layers.push_back(ftrack->layer(i));

  //cout << "TRACKS" << tmp_track.direction << " " << tmp_track.location << " DO " << tmp_track.num_tracks << " STEP " << tmp_track.space <<" LAYER " << tmp_track.layers.back() << endl;

  return 0;
}

int def_row_cb(defrCallbackType_e type, defiRow *frow, defiUserData ud) {

  Def_info &dinfo = *((Def_info *)ud);
  dinfo.rows.resize(dinfo.rows.size() + 1);
  Def_row &tmp_row = dinfo.rows.back();

  tmp_row.name   = frow->name();
  tmp_row.site   = frow->macro();
  tmp_row.origx  = frow->x();
  tmp_row.origy  = frow->y();
  tmp_row.orient = frow->orient();
  if(frow->hasDoStep()) {
    tmp_row.numx  = frow->xNum();
    tmp_row.numy  = frow->yNum();
    tmp_row.stepx = frow->xStep();
    tmp_row.stepy = frow->yStep();
  }
  //cout << "ROW " << tmp_row.name << " " << tmp_row.site << " " << tmp_row.origx
  //     << " "    << tmp_row.origy<< " " << tmp_row.orient << " DO " << tmp_row.numx
  //     << " BY "  << tmp_row.numy << " STEP " << tmp_row.stepx << " " << tmp_row.stepy << endl;
  return 0;
}

void def_parsing(Def_info &dinfo, std::string def_file_name) {
  const char *def_file = def_file_name.c_str();
  int         res;
  FILE *      fin;
  //char* userData = NULL; //this is an example of userData, but I will not use this, in my case, the userData will be a data structure for def information hierarchy
  defrInit(); //initialize the reader, This routine must be called first
  fin = fopen(def_file, "r");
  if(fin == NULL)
    std::cout << "Couldn't open input file" << std::endl;

  defrSetRowCbk(def_row_cb);
  defrSetTrackCbk(def_track_cb);
  defrSetComponentCbk(def_component_cb);
  defrSetPinCbk(def_io_cb);
  defrSetNetCbk(def_net_cb);
  defrReset();
  res = defrRead(fin, def_file, (void *)&dinfo,
                 1); //Def_info object is your userData, pass pointer of it into defrRead(), and it will return as a argument in your user-defined callback routine
  fclose(fin);
}

#endif
