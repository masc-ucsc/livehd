#include "json_inou.hpp"

Json_inou_parser::Json_inou_parser(const std::string& path) : d() {
  if (!std::filesystem::exists(path)) {
    std::cerr << "Could not find input file " << path << "!" << std::endl;
    throw std::invalid_argument("bad file path");
  }

  std::ifstream json_file(path);
  I(json_file);
  
  json_file.seekg(0, std::ios::end);
  size_t len = json_file.tellg();
  json_file.seekg(0, std::ios::beg);

  std::string json_str;
  json_str.reserve(len);
  
  // read the whole string in one go
  json_str.assign((std::istreambuf_iterator<char>(json_file)), std::istreambuf_iterator<char>());
  
  d.Parse(json_str.c_str());

  json_file.close();
}

Graph_info Json_inou_parser::make_tree() const {

  graph::Bi_adjacency_list g;

  auto g_name_map = g.vert_map<std::string>();
  auto g_area_map = g.vert_map<double>();
  auto edge_weights = g.edge_map<unsigned int>();

  I(d.HasMember("modules"));
  
  const rapidjson::Value& mods = d["modules"];
  I(mods.IsArray());
  const auto mod_arr = mods.GetArray();
  
  // loop over all modules in the json file, wiring them up and adding them to our map as we go
  for (const auto& mod : mod_arr) {
    I(mod["name"].IsString());
    const std::string name = mod["name"].GetString();
    
    // TODO: iassert prints default assert statement here?
    I(mod["area"].IsDouble());
    const double area = mod["area"].GetDouble();
    
    auto v = g.null_vert();
    for (const auto& vert : g.verts()) {
      if (g_name_map[vert] == name) {
        v = vert;
      }
    }

    if (g.is_null(v)) {
      // vertex does not exist, so create it
      auto new_v = g.insert_vert();
      g_name_map[new_v] = name;
      
      v = new_v;
    }

    g_area_map[v] = area;

    auto existing_edges = g.edge_set();
    
    // look for connections, and create them if they don't exist
    I(mod["connections"].IsArray());
    for (const auto& connection : mod["connections"].GetArray()) {
      I(connection.IsObject());
      I(connection["name"].IsString());
      I(connection["weight"].IsInt());
      I(connection["weight"].GetInt() != 0); // weight has to be non-zero for ker-lin algorithm to work

      std::string other_name = connection["name"].GetString();
      auto other_v = g.null_vert();
      for (const auto& vert : g.verts()) {
        if (g_name_map[vert] == other_name) {
          other_v = vert;
        }
      }

      if (other_v == g.null_vert()) {
        // other node is not in the map, so create it.
        other_v = g.insert_vert();
        g_name_map[other_v] = other_name;
      }

      auto new_e = g.insert_edge(v, other_v);
      edge_weights[new_e] = connection["weight"].GetInt();
      existing_edges.insert(new_e);
    }
  }
  
  // if the graph is not fully connected, ker-lin fails to work.
  // TODO: eventually replace this with an adjacency matrix, since it's significantly cheaper than this.
  for (const auto& v : g.verts()) {
    for (const auto& other_v : g.verts()) {
      bool found = false;
      for (const auto& e : g.out_edges(v)) {
        if (g.head(e) == other_v && g.tail(e) == v) {
          found = true;
          break;
        }
      }
      if (!found) {
        auto temp_e = g.insert_edge(v, other_v);
        edge_weights[temp_e] = 0;
      }
    }
  }
  
  Graph_info info(std::move(g), std::move(g_name_map), std::move(g_area_map), std::move(edge_weights));
  
#ifndef NDEBUG
  info.print();
#endif
  
  return info;
} 

double Json_inou_parser::get_area() const {
  I(d.HasMember("area"));
  return d["area"].GetDouble();
}

double Json_inou_parser::get_aspect_ratio() const {
  I(d.HasMember("aspect_ratio"));
  return d["aspect_ratio"].GetDouble();
}
