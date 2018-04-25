//
// Created by sheng hong  on 4/14/18.
//

#ifndef LGRAPH_MY_TEST_H
#define LGRAPH_MY_TEST_H


#include <string>
#include "inou.hpp"
#include "options.hpp"


class Inou_cfg_options_pack : public Options_pack {
public:
	Inou_cfg_options_pack();

	std::string cfg_output;
	std::string cfg_input;
};

class Inou_cfg : public Inou {
private:
  static bool space(char c)    {return  isspace(c);}
  static bool not_space(char c){return !isspace(c);}
  std::vector<std::string> split(const std::string& str);
  //old
	//void cfg_2_lgraph(std::ifstream&, LGraph*);
  void cfg_2_lgraph(char**, LGraph*);
protected:
	Inou_cfg_options_pack opack;

public:
	Inou_cfg();

	virtual ~Inou_cfg();

	std::vector<LGraph *> generate() override final;

	using Inou::generate;

	void generate(std::vector<const LGraph *> out) override final;

};




#endif //LGRAPH_MY_TEST_H
