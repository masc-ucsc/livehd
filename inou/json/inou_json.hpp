//
// Created by birdeclipse on 12/18/17.
//

#ifndef LGRAPH_MY_TEST_H
#define LGRAPH_MY_TEST_H


#include <string>
#include "inou.hpp"
#include "options.hpp"
#include "rapidjson/document.h"


class Inou_json_options_pack : public Options_pack {
public:
	Inou_json_options_pack();

	std::string json_output;
	std::string json_input;
};

class Inou_json : public Inou {
private:
protected:
	Inou_json_options_pack opack;

	std::map<Index_ID, Index_ID> json_remap;

	bool is_const_op(std::string s);

	bool is_int(std::string s);

	void from_json(LGraph *g, rapidjson::Document &document);

	void to_json(const LGraph *g, const std::string filename) const;

public:
	Inou_json();

	virtual ~Inou_json();

	std::vector<LGraph *> generate() override final;

	using Inou::generate;

	void generate(std::vector<const LGraph *>& out) override final;

};

#endif //LGRAPH_MY_TEST_H
