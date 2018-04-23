#ifndef INOU_YAML_H
#define INOU_YAML_H

#include <string>
#include <yaml-cpp/yaml.h>

#include "inou.hpp"
#include "options.hpp"


class Inou_yaml_options_pack : public Options_pack {
public:
  Inou_yaml_options_pack();

  std::string yaml_output;
  std::string yaml_input;
};

class Inou_yaml : public Inou {
private:
protected:
  Inou_yaml_options_pack opack;

  std::map<Index_ID, Index_ID> yaml_remap;
  void from_yaml_bits(LGraph *g, Index_ID nid, const YAML::Node node);
  void from_yaml_outputs(LGraph *g, Index_ID nid, const YAML::Node nlist);

  void from_yaml_input_name(LGraph *g, Index_ID nid, const YAML::Node node);
  void from_yaml_output_name(LGraph *g, Index_ID nid, const YAML::Node node);
  void from_yaml_wirename(LGraph *g, Index_ID nid, const YAML::Node node);
  void from_yaml_op(LGraph *g, Index_ID nid, const YAML::Node node);
  void from_yaml_delay(LGraph *g, Index_ID nid, const YAML::Node node);

  void to_yaml(const LGraph *g, const std::string filename) const;
  void from_yaml(LGraph *g, const std::string filename);

  bool is_int(std::string);
  bool is_const(std::string);

public:
  Inou_yaml();

  std::vector<LGraph *> generate() override final;
  using Inou::generate;
  void generate(std::vector<const LGraph *> out) override final;
};


#endif

