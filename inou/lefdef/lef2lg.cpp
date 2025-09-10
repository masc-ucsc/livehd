//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lef2lg.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>

#include "inou_def.hpp"
#include "perf_tracing.hpp"

int lef_macro_begin_cb(lefrCallbackType_e c, const char *macroName, lefiUserData ud) {
  Tech_library *tlib = (Tech_library *)ud;

  // in create_cell_id(), it will create a new cell type for vector cell_types
  tlib->create_cell_id(macroName);
  return 0;
}

// fmacro means macro information from lef file
int lef_macro_cb(lefrCallbackType_e c, lefiMacro *fmacro, lefiUserData ud) {
  Tech_library *tlib = (Tech_library *)ud;

  // note!! you "have to" create a new cell type in "lef_macro_begin_cb" callback function, or the cell will not really create for
  // the sub-sequence cb,e.g. macro_cb and pin_cb tlib->create_cell_id(fmacro->name());

  // and then use tmp_cell as reference of Tech_library::cell_types.back() to retrieve out the lef
  Tech_cell &tmp_cell = tlib->get_vec_cell_types()->back();
  if (fmacro->hasSize()) {
    tmp_cell.set_cell_size(fmacro->sizeX(), fmacro->sizeY());
  }
  return 0;
}

int lef_pin_cb(lefrCallbackType_e c, lefiPin *fpin, lefiUserData ud) {  // fpin means pin information from lef file

  Tech_library *tlib = (Tech_library *)ud;

  // if(strcmp(fpin->use(), "GROUND") == 0) return 0 ; //should we ignore POWER and GROUND pin?
  // if(strcmp(fpin->use(), "POWER") == 0) return 0 ;

  int pn;  // variable for port number

  Tech_cell &tmp_cell = tlib->get_vec_cell_types()->back();

  Tech_cell::Direction dir = Tech_cell::Direction::input;
  if (fpin->hasDirection()) {
    if (strcmp(fpin->direction(), "INPUT") == 0) {
      dir = Tech_cell::Direction::input;
    } else if (strcmp(fpin->direction(), "OUTPUT") == 0) {
      dir = Tech_cell::Direction::output;
    } else if (strcmp(fpin->direction(), "INOUT") == 0) {  // we will set inout as input in lgraph
      dir = Tech_cell::Direction::input;
    } else {
      assert(false);  // Unknown option
    }
  }
  tmp_cell.add_pin(fpin->name(), dir);
  Tech_cell::Pin &tmp_pin = tmp_cell.get_vec_pins()->back();

  if (fpin->hasUse()) {
    tmp_pin.use = fpin->use();
  }

  for (pn = 0; pn < fpin->numPorts(); pn++) {
    const lefiGeometries *geometry = fpin->port(pn);

    for (int i = 0; i < geometry->numItems(); i++) {
      // if(geometry->itemType(i) == lefiGeomLayerE){
      //  tmp_phy.metal_name = geometry->lefiGeometries::getLayer(i);
      //}

      if (geometry->itemType(i) == lefiGeomRectE) {
        tmp_pin.phys.resize(tmp_pin.phys.size() + 1);  // create a temporary object inline
        Tech_cell::Physical_pin &tmp_phy = tmp_pin.phys.back();
        const lefiGeomRect      *rect    = geometry->getRect(i);

        std::string metal_name_str(geometry->lefiGeometries::getLayer(i - 1));
        if (strncmp(metal_name_str.c_str(), "Metal", 5) == 0) {
          tmp_phy.metal_name = geometry->lefiGeometries::getLayer(i - 1);
        } else {
          tmp_phy.metal_name = geometry->lefiGeometries::getLayer(0);
        }
        // tmp_pin.phy.rects.resize(tmp_pin.phy.rects.size()+1);
        // Tech_cell::Rect& tmp_rect = tmp_pin.phy.rects.back();
        tmp_phy.xl = rect->xl;
        tmp_phy.yl = rect->yl;
        tmp_phy.xh = rect->xh;
        tmp_phy.yh = rect->yh;
      }
    }  // end inner for
  }  // end outer for

  // pin ports parsing end
  return 0;
}

int lef_layer_cb(lefrCallbackType_e c, lefiLayer *flayer, lefiUserData ud) {  // flayer means layer information from lef file
  auto *tlib = (Tech_library *)ud;                                            // convert void* into Tech_library*
  inti, j, k;
  lefiSpacingTable *spTable;
  lefiParallel     *parallel;

  if (strcmp(flayer->name(), "OVERLAP") == 0) {
    return 0;  // do nothing when layer name = OVERLAP
  }

  tlib->increase_vec_layers_size();  // whenever user-defined routine(layerCb) is called, the data member Tech_library::layers will
                                     // increase size by one to be ready to contain new info.

  Tech_layer &tmp_layer = tlib->get_vec_layers()->back();  // and then use tmp_layer as reference of Tech_library::layers.back() to
                                                           // retrieve out the lef data the callback routine returned

  tmp_layer.name = flayer->name();

  if (flayer->hasDirection()) {
    if (strcmp(flayer->direction(), "HORIZONTAL") == 0) {
      tmp_layer.horizontal = true;
    } else {
      tmp_layer.horizontal = false;
    }
  }

  if (flayer->hasMinwidth()) {
    tmp_layer.minwidth = flayer->minwidth();
  }

  if (flayer->hasArea()) {
    tmp_layer.area = flayer->area();
  }

  if (flayer->hasXYPitch()) {
    tmp_layer.pitches.push_back(flayer->pitchX());
    tmp_layer.pitches.push_back(flayer->pitchY());
  }

  tmp_layer.width
      = flayer->width();  // if (flayer->hasWidth()) //bugy with Layer Via width detection, so remove condition judgement

  if (flayer->hasSpacingNumber()) {
    for (i = 0; i < flayer->numSpacing(); i++) {
      if (i == 0) {
        tmp_layer.spacing_eol.push_back(flayer->spacing(i));
      } else if (flayer->hasSpacingEndOfLine(i)) {
        tmp_layer.spacing_eol.push_back(flayer->spacing(i));
        tmp_layer.spacing_eol.push_back(flayer->spacingEolWidth(i));
        tmp_layer.spacing_eol.push_back(flayer->spacingEolWithin(i));
      }
    }  // end for
  }

  for (i = 0; i < flayer->numSpacingTable(); i++) {
    spTable = flayer->spacingTable(i);
    if (spTable->lefiSpacingTable::isParallel()) {
      parallel = spTable->lefiSpacingTable::parallel();
      for (j = 0; j < parallel->lefiParallel::numLength(); j++) {
        tmp_layer.spctb_prl = parallel->lefiParallel::length(j);
      }

      for (j = 0; j < parallel->lefiParallel::numWidth(); j++) {
        tmp_layer.spctb_width.push_back(parallel->lefiParallel::width(j));
        for (k = 0; k < parallel->lefiParallel::numLength(); k++) {
          tmp_layer.spctb_spacing.push_back(parallel->lefiParallel::widthSpacing(j, k));
        }
      }
    }  // end if
  }
  return 0;
}

int lef_via_cb(lefrCallbackType_e c, lefiVia *fvia, lefiUserData ud) {  // fvia means via information from lef file
  Tech_library *tlib = (Tech_library *)ud;                              // convert void* into Tech_library*

  tlib->increase_vec_vias_size();  // whenever user-defined routine(layerCb) is called, the data member Tech_library::vias will
                                   // increase size by one to be ready to contain new info.
  Tech_via &tmp_via = tlib->get_vec_vias()->back();  // and then use tmp_via as reference of Tech_library::vias.back() to retrieve
                                                     // out the lef data the callback routine returned

  tmp_via.name = fvia->name();

  tmp_via.vlayers.resize(fvia->numLayers());
  for (int i = 0; i < fvia->numLayers(); i++) {
    Tech_via_layer &tmp_vlayer = tmp_via.vlayers[i];
    tmp_vlayer.layer_name      = fvia->layerName(i);
    for (int j = 0; j < fvia->numRects(i); j++) {
      tmp_vlayer.rect.xl = fvia->xl(i, j);
      tmp_vlayer.rect.yl = fvia->yl(i, j);
      tmp_vlayer.rect.xh = fvia->xh(i, j);
      tmp_vlayer.rect.yh = fvia->yh(i, j);
    }
  }
  return 0;
}

void lef_parsing(Tech_library *tlib, const std::string &lef_file_name) {
  tlib->clear_tech_lib();

  const char *lef_file = lef_file_name.c_str();
  lefrInit();  // initialize the reader, This routine must be called first

  FILE *fin = fopen(lef_file, "r");
  if (fin == NULL) {
    Pass::error("lglefdef: could not open lef input file {}", lef_file_name);
    return;
  }

  lefrSetMacroBeginCbk(lef_macro_begin_cb);
  lefrSetMacroCbk(lef_macro_cb);
  lefrSetPinCbk(lef_pin_cb);
  lefrSetLayerCbk(lef_layer_cb);
  lefrSetViaCbk(lef_via_cb);
  lefrReset();

  lefrRead(fin, lef_file, (void *)tlib);  // Tech_file object is your userData, pass pointer of it into lefrRead(), and it will
                                          // return as a argument in your user-defined callback routine
  fclose(fin);
}
