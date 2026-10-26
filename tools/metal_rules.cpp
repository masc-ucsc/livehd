// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Audit rectangular metal primitives, not complete-net legality.
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "inou/lefdef/routing_rules.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

int main(int argc, char** argv) {
  try {
    std::vector<std::string>             lefs;
    std::vector<livehd::physical::Shape> shapes;
    std::string                          def;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (++i == argc) {
        throw std::invalid_argument("missing argument value");
      }
      const std::string value = argv[i];
      if (key == "--lef") {
        lefs.push_back(value);
      } else if (key == "--def") {
        def = value;
      } else if (key == "--shape") {
        livehd::physical::Shape shape;
        std::istringstream      input(value);
        std::string             trailing;
        if (!(input >> shape.layer >> shape.rect.xl >> shape.rect.yl >> shape.rect.xh >> shape.rect.yh) || input >> trailing) {
          throw std::invalid_argument("--shape needs 'layer xl yl xh yh' in DEF DBU");
        }
        shapes.push_back(shape);
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (lefs.empty() || def.empty() || shapes.empty()) {
      throw std::invalid_argument("metal_rules requires --lef, --def and --shape");
    }
    const auto                                 tech = livehd::physical::read_lef(lefs);
    const auto                                 fp   = livehd::physical::read_def(def);
    const livehd::physical::Signal_metal_rules rules(tech, fp);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
    const auto                                 result = [&](const livehd::physical::Rule_check& check) {
      out.StartObject();
      out.Key("accepted");
      out.Bool(check.legal);
      out.Key("reason");
      out.String(check.reason.c_str());
      out.EndObject();
    };
    out.StartObject();
    out.Key("scope");
    out.String("rectangular metal primitives only; not connectivity, via or complete-net certification");
    out.Key("rectangles");
    out.StartArray();
    for (const auto& shape : shapes) {
      result(rules.rectangle(shape));
    }
    out.EndArray();
    out.Key("pairs");
    out.StartArray();
    for (size_t i = 0; i < shapes.size(); ++i) {
      for (size_t j = i + 1; j < shapes.size(); ++j) {
        out.StartObject();
        out.Key("first");
        out.Uint64(i);
        out.Key("second");
        out.Uint64(j);
        out.Key("spacing");
        result(rules.spacing(shapes[i], shapes[j]));
        out.EndObject();
      }
    }
    out.EndArray();
    out.EndObject();
    std::cout << buffer.GetString() << '\n';
  } catch (const std::exception& e) {
    std::cerr << "metal_rules: " << e.what() << '\n';
    return 1;
  }
}
