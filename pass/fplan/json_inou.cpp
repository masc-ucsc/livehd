#include "json_inou.hpp"
Json_inou_parser::Json_inou_parser(const std::string& path) : d() {
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

// TODO: this is global right now because I have no idea how to move maps.
graph::Bi_adjacency_list g;

auto g_name_map = g.vert_map<std::string>();
auto g_area_map = g.vert_map<double>();

auto edge_weights = g.edge_map<unsigned int>();

void Json_inou_parser::make_tree() const {
  I(d.HasMember("modules"));
  
  const rapidjson::Value& mods = d["modules"];
  I(mods.IsArray());
  const auto mod_arr = mods.GetArray();
  
  // TODO: this is here because idk if accessing maps at known bad locations is ok...
  auto created_verts = g.vert_set();
  
  // loop over all modules in the json file, wiring them up and adding them to our map as we go
  for (const auto& mod : mod_arr) {
    I(mod["name"].IsString());
    const std::string name = mod["name"].GetString();
    
    // TODO: iassert prints default assert statement here?
    I(mod["area"].IsDouble());
    const double area = mod["area"].GetDouble();
    
    auto v = g.null_vert();
    for (const auto& vert : created_verts) {
      if (g_name_map[vert] == name) {
        v = vert;
      }
    }

    if (g.is_null(v)) {
      // vertex does not exist, so create it
      // TODO: I _think_ the maps get preserved when new verts get added...
      auto new_v = g.insert_vert();
      g_name_map[new_v] = name;
      
      created_verts.insert(new_v);

      v = new_v;
    }

    g_area_map[v] = area;
    
    // look for connections, and create them if they don't exist
    I(mod["connections"].IsArray());
    for (const auto& connection : mod["connections"].GetArray()) {
      I(connection.IsObject());
      I(connection["name"].IsString());
      I(connection["weight"].IsInt());

      std::string other_name = connection["name"].GetString();
      auto other_v = g.null_vert();
      for (const auto& vert : created_verts) {
        if (g_name_map[vert] == other_name) {
          other_v = vert;
        }
      }

      if (other_v != g.null_vert()) {
        // other node is already in map
        
        bool new_connection = true;
        for (const auto& edge : g.out_edges(v)) {
          if (g.head(edge) == other_v) {
            new_connection = false;
          }
        }

        if (new_connection) {
          // connection does not exist yet
          auto new_e = g.insert_edge(v, other_v); // construct an edge from v -> other_v
          edge_weights[new_e] = connection["weight"].GetInt();
        }
        
        // other_v -> v edge gets created later.

      } else {
        // other node is not in the map, so create and wire it.
        
        auto new_v = g.insert_vert();
        g_name_map[new_v] = other_name;
        created_verts.insert(new_v);
        
        auto new_e = g.insert_edge(v, other_v);
        edge_weights[new_e] = connection["weight"].GetInt();
      }
    }
  }

#ifndef NDEBUG
  // print out the graph in DOT format, with relevant information attached.
  using namespace graph::attributes;
  std::cout << g.dot_format("name"_of_vert = g_name_map, "weight"_of_edge = edge_weights) << std::endl;
#endif
} 

double Json_inou_parser::get_area() const {
  I(d.HasMember("area"));
  return d["area"].GetDouble();
}

double Json_inou_parser::get_aspect_ratio() const {
  I(d.HasMember("aspect_ratio"));
  return d["aspect_ratio"].GetDouble();
}
