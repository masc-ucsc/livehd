
#ifndef GRAPHLIBRARY_H
#define GRAPHLIBRARY_H

#include <map>
#include <vector>
#include <cassert>

#include <iostream>
#include <fstream>

#include <unordered_map>

class LGraph;

class Graph_library {

  protected:
    std::vector<std::string> id2name;
    std::map<std::string, int> name2id;
    bool graph_library_clean;
    std::string path;

    std::string library_file;

    Graph_library() {
    }

    Graph_library(std::string& _path) {
      path = _path;
      std::ifstream graph_list;
      graph_library_clean = true;
      library_file = "graph_library";

      graph_list.open(path + "/" + library_file);
      if(graph_list.is_open()) {
        uint32_t n_graphs = 0;
        graph_list >> n_graphs;
        id2name.resize(n_graphs);

        for(int idx = 0; idx < n_graphs; ++idx){
          std::string name;
          int graph_id = 0;
          graph_list >> name >> graph_id;

          // this is only true in case where we skip graph ids
          if(id2name.size() <= graph_id)
            id2name.resize(graph_id+1);
          id2name[graph_id] = name;
          name2id[name] = graph_id;
        }
      }

      graph_list.close();
    }

    ~Graph_library() {
      if(!graph_library_clean) {
        std::ofstream graph_list;

        graph_list.open(path + "/" + library_file);
        graph_list << name2id.size() << std::endl;
        for(auto nameid : name2id) {
          graph_list << nameid.first << " " << nameid.second << std::endl;
        }

        graph_list.close();
      }
    }

    static std::unordered_map<std::string,Graph_library*> instances;

  public:
    int add_name(std::string& name) {
      assert(name2id.find(name) == name2id.end());

      int id = id2name.size();

      graph_library_clean = false;
      name2id[name] = id;
      id2name.push_back(name);

      assert(id2name[id] == name);

      return id;
    }

    std::string get_name(int id) const {
      return id2name[id];
    }

    LGraph* get_graph(int id) const;

    int lgraph_count() const {
      return id2name.size();
    }

    //adds the name if it doesn't exist yet
    int get_id(std::string name) {
      if(name2id.find(name) != name2id.end()) {
        return name2id[name];
      }
      return add_name(name);
    }

    int get_id_const(std::string name) const {
      if(name2id.find(name) != name2id.end()) {
        return name2id.at(name);
      }
      return -1;
    }

    //FIXME: replace this by a find / end when the iterator is working
    bool include(std::string name) const {
      return name2id.find(name) != name2id.end();
    }

    static Graph_library* instance(std::string path = "lgdb") {
      if(Graph_library::instances.find(path) == Graph_library::instances.end()) {
        Graph_library::instances.insert(std::make_pair(path, new Graph_library(path)));
      }
      return Graph_library::instances[path];
    }

    void sync() {
      if(!graph_library_clean) {
        std::ofstream graph_list;

        graph_list.open(path + "/" + library_file);
        graph_list << name2id.size() << std::endl;
        for(auto nameid : name2id) {
          graph_list << nameid.first << " " << nameid.second << std::endl;
        }

        graph_list.close();
        graph_library_clean = true;
      }
    }

};

//FIXME: come up with a C++14 friendly solution
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++17-extensions"
inline std::unordered_map<std::string,Graph_library*> Graph_library::instances;
#pragma clang diagnostic pop

#endif

