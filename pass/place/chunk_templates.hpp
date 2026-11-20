// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "chunk_geometry.hpp"

namespace livehd::placement {
using Chunk_point = std::array<double, 2>;
// One entry for every home touched by this crossing hypernet. Role is local to the logical partition.
struct Interface_contact {
  int         home;
  std::string role;
  Chunk_point xy{};
  size_t      copy_source_net = SIZE_MAX;
};
struct Interface_net {
  std::string                    name;
  size_t                         index;
  std::optional<Chunk_point>     terminal;
  std::vector<Interface_contact> contacts;
  std::optional<Chunk_box>       terminal_bounds;
};
struct Template_form {
  std::string                                   name;
  double                                        aspect = 0;
  std::map<std::string, std::pair<int, double>> pins;
};
struct Template_rule {
  std::string                          mode = "auto";
  std::map<std::string, std::string>   instances;
  std::map<std::string, Template_form> forms;
};
using Template_rules = std::map<std::string, Template_rule>;
struct Interface_copy_rule {
  std::string source, target, orientation;
};
std::vector<Interface_copy_rule> read_interface_copies(const metadata::Bundle*, const std::string& design);
using Contact_keys = decltype(Chunk_model::contact_keys);
Template_rules read_template_rules(const metadata::Bundle*, const std::string& design);
void measure_chunk_ports(Chunk_plan&, const std::vector<Interface_net>&, Chunk_box);
void           refine_templates(Chunk_plan&, std::vector<Interface_net>&, const std::vector<Chunk_object>&, const Template_rules&,
                                Chunk_box core, Chunk_point pitch, bool move_boxes, const Chunk_edits& edits,
                                const std::vector<Interface_copy_rule>& copies = {}, const Contact_keys& keys = {});
}  // namespace livehd::placement
