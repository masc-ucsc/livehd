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

Netl_hypertree Json_inou_parser::make_tree() const {
  I(d.HasMember("modules"));
  
  const rapidjson::Value& mods = d["modules"];
  I(mods.IsArray());
  
  // map to hold positions of known nodes
  // the map is local because it wouldn't speed up actual tree operations since we don't refer to things by name.
  std::unordered_map<std::string, pnode> m; 
  
  // loop over all modules in the json file, wiring them up and adding them to our map as we go
  for (const auto& mod : mods.GetArray()) {
    I(mod["name"].IsString());
    const std::string name = mod["name"].GetString();
    
    pnode n;
    
    const auto& existing_pair = m.find(name);
    if (existing_pair != m.end()) {
      // node was already created by something else, use that
      n = existing_pair->second;
    } else {
      // make a new node, put in the name, and load into map
      n = std::make_shared<Netl_node>();

      std::string new_name = mod["name"].GetString();
      n->name = new_name;
      
      auto new_pair = std::pair<std::string, pnode>(new_name, n);
      m.insert(new_pair);
    }

    // TODO: iassert prints default assert statement here?
    I(mod["area"].IsDouble());
    n->area = mod["area"].GetDouble();
    
    // find or make the parent node
    if (mod.HasMember("parent")) {
      I(mod["parent"].IsString());
      std::string parent_str = mod["parent"].GetString();

      const auto& parent = m.find(parent_str);
      if (parent != m.end()) {
        // parent has already been created, so tie child to parent and parent to child
        n->parent = parent->second;
        parent->second->children.push_back(n);
      } else {
        // parent does not exist, so make one.
        auto new_parent = std::make_shared<Netl_node>();
        new_parent->name = parent_str;
        n->parent = new_parent;
        new_parent->children.push_back(n);
        
        auto new_pair = std::pair<std::string, pnode>(parent_str, new_parent);
        m.insert(new_pair);
      }
    }
    
    // find or make all connections
    I(mod["connections"].IsArray());
    for (const auto& connection : mod["connections"].GetArray()) {
      I(connection.IsObject());
      I(connection["name"].IsString());
      I(connection["weight"].IsInt());

      std::string connection_str = connection["name"].GetString();
      const auto& other_conn = m.find(connection_str);
      if (other_conn != m.end()) {
        // other node already created and in map

        const auto& other_conn_list = other_conn->second->connect_list;
        auto find_other = [&n](const std::pair<pnode, unsigned int>& s) -> bool {
          return s.first->name == n->name;
        };
        
        if (std::find_if(other_conn_list.cbegin(), other_conn_list.cend(), find_other) == other_conn_list.cend()) {
          // nodes are supposed to be connected, but aren't yet
          n->connect_list.push_back(std::pair<pnode, unsigned int>(other_conn->second, connection["weight"].GetInt()));
          other_conn->second->connect_list.push_back(std::pair<pnode, unsigned int>(n, connection["weight"].GetInt()));
        }
      } else {
        // not in map, so create a connection
        auto new_conn = std::make_shared<Netl_node>();
        new_conn->name = connection_str;

        n->connect_list.push_back(std::pair<pnode, unsigned int>(new_conn, connection["weight"].GetInt()));
        new_conn->connect_list.push_back(std::pair<pnode, unsigned int>(n, connection["weight"].GetInt()));
        
        m.insert(std::pair<std::string, pnode>(connection_str, new_conn));
      }
    }
  }
  
  // make a tree out of the root node.
  return *(m.find("root")->second);
}

double Json_inou_parser::get_area() const {
  I(d.HasMember("area"));
  return d["area"].GetDouble();
}

double Json_inou_parser::get_aspect_ratio() const {
  I(d.HasMember("aspect_ratio"));
  return d["aspect_ratio"].GetDouble();
}
