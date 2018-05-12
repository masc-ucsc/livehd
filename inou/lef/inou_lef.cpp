//
// Created by birdeclipse on 3/22/18.
//

#include <core/tech_library.hpp>
#include "inou_lef.hpp"

static int lef_macro_begin_cb(lefrCallbackType_e c, const char *macroName, lefiUserData ud);

static int lef_macro_cb(lefrCallbackType_e c, lefiMacro *fmacro, lefiUserData ud);

static int lef_pin_cb(lefrCallbackType_e c, lefiPin *fpin, lefiUserData ud);

static int lef_layer_cb(lefrCallbackType_e c, lefiLayer *flayer, lefiUserData ud);

static int lef_via_cb(lefrCallbackType_e c, lefiVia *fvia, lefiUserData ud);

Inou_lef_options_pack::Inou_lef_options_pack() {
	Options::get_desc()->add_options()
					("lef_file,l", boost::program_options::value(&lef_file),
					 "lef_file <filename> for graph");

	boost::program_options::variables_map vm;
	boost::program_options::store(
					boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(
									*Options::get_desc()).allow_unregistered().run(), vm);
	if (vm.count("lef_file")) {
		lef_file = vm["lef_file"].as<std::string>();
	}
	else {
		lef_file = "";
	}
	console->info("lef_input {}", lef_file);
}

Inou_lef::Inou_lef() {}

Inou_lef::~Inou_lef() {}


int lef_macro_begin_cb(lefrCallbackType_e c, const char *macroName, lefiUserData ud) {
	assert(c);
	auto *tlib = static_cast<Tech_library *>(ud);
	if (!tlib->include(macroName))
		tlib->create_cell_id(macroName);
	return 0;
}

int lef_macro_cb(lefrCallbackType_e c, lefiMacro *fmacro, lefiUserData ud) {
	assert(c);
	auto *tlib = static_cast<Tech_library *>(ud);
	auto &tmp_cell = tlib->get_vec_cell_types()->back();

	if (fmacro->hasSize()) {
    assert(false);
#if 0
    FIXME???
		tmp_cell.set_dimentions(static_cast<float>(fmacro->sizeX()), static_cast<float>(fmacro->sizeY()));
#endif
	}
	return 0;
}

int lef_pin_cb(lefrCallbackType_e c, lefiPin *fpin, lefiUserData ud) {

	assert(c);
	auto *tlib = static_cast<Tech_library *>(ud);

	int port_number = 0;
	bool is_input = false;
	if (fpin->hasDirection()) {
		if (strcmp(fpin->direction(), "INPUT") == 0)
			is_input = true;
		else if (strcmp(fpin->direction(), "OUTPUT") == 0)
			is_input = false;
		else if (strcmp(fpin->direction(), "INOUT") == 0)
			is_input = true;
	}

	auto &tmp_cell = tlib->get_vec_cell_types()->back();
	if (is_input) {
		tmp_cell.set_direction(tmp_cell.add_pin(fpin->name()), Tech_cell::Direction::input);
	}
	else {
		tmp_cell.set_direction(tmp_cell.add_pin(fpin->name()), Tech_cell::Direction::output);
	}

	Tech_cell::Pin &pin_info = tmp_cell.get_vec_pins()->back();

	if (fpin->hasUse())
		pin_info.use = fpin->use();

	for (port_number = 0; port_number < fpin->numPorts(); port_number++) {
		const lefiGeometries *geometry = fpin->port(port_number);

		for (int i = 0; i < geometry->numItems(); i++) {
			if (geometry->itemType(i) == lefiGeomRectE) {
				const lefiGeomRect *rect = geometry->getRect(i);
				Tech_cell::Physical_pin phy_info;
				std::string metal_name_str(geometry->lefiGeometries::getLayer(i - 1));
				if (strncmp(metal_name_str.c_str(), "Metal", 5) == 0)
					phy_info.metal_name = geometry->lefiGeometries::getLayer(i - 1);
				else
					phy_info.metal_name = geometry->lefiGeometries::getLayer(0);

				phy_info.xl = static_cast<float>(rect->xl);
				phy_info.yl = static_cast<float>(rect->yl);
				phy_info.xh = static_cast<float>(rect->xh);
				phy_info.yh = static_cast<float>(rect->yh);
				pin_info.phys.emplace_back(phy_info);
			}
		}
	}
	return 0;
}

int lef_layer_cb(lefrCallbackType_e c, lefiLayer *flayer, lefiUserData ud) {
	assert(c);
	auto *tlib = static_cast<Tech_library *>(ud);

	int i, j, k;
	lefiSpacingTable *spTable;
	lefiParallel *parallel;

	if (strcmp(flayer->name(), "OVERLAP") == 0) {
		return 0; //do nothing when layer name = OVERLAP
	}

	tlib->increase_vec_layers_size();
	auto &tmp_layer = tlib->get_vec_layers()->back();
	tmp_layer.name = flayer->name();

	if (flayer->hasDirection()) {
		if (strcmp(flayer->direction(), "HORIZONTAL") == 0)
			tmp_layer.horizontal = true;
		else
			tmp_layer.horizontal = false;
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

	tmp_layer.width = flayer->width();

	if (flayer->hasSpacingNumber()) {
		for (i = 0; i < flayer->numSpacing(); i++) {
			if (i == 0)
				tmp_layer.spacing = flayer->spacing(i);
			else if (flayer->hasSpacingEndOfLine(i)) {
				tmp_layer.spacing_eol.push_back(flayer->spacing(i));
				tmp_layer.spacing_eol.push_back(flayer->spacingEolWidth(i));
				tmp_layer.spacing_eol.push_back(flayer->spacingEolWithin(i));

			}
		}
	}

	for (i = 0; i < flayer->numSpacingTable(); i++) {
		spTable = flayer->spacingTable(i);
		if (spTable->lefiSpacingTable::isParallel()) {
			parallel = spTable->lefiSpacingTable::parallel();
			for (j = 0; j < parallel->lefiParallel::numLength(); j++)
				tmp_layer.spctb_prl = parallel->lefiParallel::length(j);

			for (j = 0; j < parallel->lefiParallel::numWidth(); j++) {
				tmp_layer.spctb_width.push_back(parallel->lefiParallel::width(j));
				for (k = 0; k < parallel->lefiParallel::numLength(); k++)
					tmp_layer.spctb_spacing.push_back(parallel->lefiParallel::widthSpacing(j, k));
			}
		}
	}
	return 0;
}

int lef_via_cb(lefrCallbackType_e c, lefiVia *fvia, lefiUserData ud) {
	assert(c);
	auto *tlib = static_cast<Tech_library *>(ud);

	tlib->increase_vec_vias_size();
	Tech_via &tmp_via = tlib->get_vec_vias()->back();

	tmp_via.name = fvia->name();

	for (int i = 0; i < fvia->numLayers(); i++) {
		Tech_via_layer tmp_via_layer;
		tmp_via_layer.layer_name = fvia->layerName(i);
		for (int j = 0; j < fvia->numRects(i); j++) {
			tmp_via_layer.rect.xl = fvia->xl(i, j);
			tmp_via_layer.rect.yl = fvia->yl(i, j);
			tmp_via_layer.rect.xh = fvia->xh(i, j);
			tmp_via_layer.rect.yh = fvia->yh(i, j);
		}
		tmp_via.vlayers.emplace_back(tmp_via_layer);
	}

	return 0;
}

void Inou_lef::lef_parsing(Tech_library *tlib, std::string &lef_file_name) {
	fmt::print("lefile is {}", lef_file_name);
	FILE *fin;
	lefrInit();
	fin = fopen(lef_file_name.c_str(), "rb");
	if (fin == nullptr) {
		console->error("could not open lef_file {}", lef_file_name);
		exit(-4);
	}
	lefrSetMacroBeginCbk(lef_macro_begin_cb);
	lefrSetMacroCbk(lef_macro_cb);
	lefrSetPinCbk(lef_pin_cb);
	lefrSetLayerCbk(lef_layer_cb);
	lefrSetViaCbk(lef_via_cb);
	lefrReset();
	lefrRead(fin, lef_file_name.c_str(), static_cast<lefiUserData>(tlib));
	fclose(fin);
}

std::vector<LGraph *> Inou_lef::generate() {
	std::vector<LGraph *> lgs;
	if (opack.graph_name != "") {
		lgs.push_back(new LGraph(opack.lgdb_path, opack.graph_name, false));
		lef_parsing(lgs[0]->get_tech_library(), opack.lef_file);
		lgs[0]->sync();
	}
	else {
		console->error("please specify the graph name!\n");
		exit(-4);
	}
	return lgs;
}

void Inou_lef::generate(std::vector<const LGraph *>& out) {
	if (out.size() != 0) {
		fmt::print("lef file synced into tech library!\n");
	}
}