//
// Created by birdeclipse on 3/22/18.
//

#ifndef LGRAPH_INOU_LEF_HPP
#define LGRAPH_INOU_LEF_HPP

#include "inou.hpp"
#include "options.hpp"
#include "lgraphbase.hpp"
#include "lgedgeiter.hpp"
#include "tech_library.hpp"
#include "lefrReader.hpp"
#include "lefiDefs.hpp"


class Inou_lef_options_pack : public Options_pack {
public:
	Inou_lef_options_pack();

	std::string lef_file;
};

class Inou_lef : public Inou {
private:

protected:
	Inou_lef_options_pack opack;
public:
	Inou_lef();

	virtual ~Inou_lef();

	std::vector<LGraph *> generate() override final;

	using Inou::generate;

	void generate(std::vector<const LGraph *> out) override final;

	static void lef_parsing(Tech_library *tlib, std::string &lef_file_name);

};


#endif //LGRAPH_INOU_LEF_HPP
