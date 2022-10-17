//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node_pin.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "fmt/format.h"
#include "lgraph.hpp"
#include "sub_node.hpp"
#include "lgraphbase.hpp"
#include "absl/container/flat_hash_map.h"
#include "lgedgeiter.hpp"

class SubGraph_color {
  public:

    std::string instance_id;
    float color_val;

    SubGraph_color() =default;
    void from_json(const rapidjson::Value &entry);
    void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer> &writer) const;
};

class Inou_attr : public Pass {
private:
  absl::flat_hash_map<std::string, double> node2color;
protected:
  static void set_color_to_lg(Eprp_var &var);
  static void get_color_from_lg(Eprp_var &var);

  void read_json(const std::string &filename, Lgraph *lg);
  void color_lg (Lgraph *lg);
public:
  Inou_attr(const Eprp_var &var);
  static void setup();
};
