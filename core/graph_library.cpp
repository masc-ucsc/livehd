//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>

#include <cassert>
#include <regex>
#include <fstream>
#include <set>

#include "fmt/format.h"

#include "pass.hpp"
#include "graph_library.hpp"

Graph_library::Global_instances   Graph_library::global_instances;
Graph_library::Global_name2lgraph Graph_library::global_name2lgraph;


Graph_library *Graph_library::instance(std::string_view path) {
#if 0
  auto it = Graph_library::global_instances.find(path);
  if(it != Graph_library::global_instances.end()) {
    return it->second;
  }
#endif

  std::string spath(path);

  char full_path[PATH_MAX+1];
  realpath(spath.c_str(), full_path);

  auto it = Graph_library::global_instances.find(full_path);
  if(it != Graph_library::global_instances.end()) {
    return it->second;
  }

  Graph_library *graph_library =  new Graph_library(full_path);
  Graph_library::global_instances.insert(std::make_pair(std::string(full_path), graph_library));

  return graph_library;
}

Lg_type_id Graph_library::reset_id(std::string_view name, std::string_view source) {
  const auto &it = name2id.find(name);
  if(it != name2id.end()) {
    graph_library_clean           = false;
    attribute[it->second].version = max_next_version.value++;
    if (attribute[it->second].source != source) {
      if (attribute[it->second].source == "-") {
        attribute[it->second].source = source;
        Pass::warn("overwrite lgraph:{} source from {} to {}",name, attribute[it->second].source, source); // LCOV_EXCL_LINE
      }else if (source == "-") {
        Pass::warn("keeping lgraph:{} source {}",name, attribute[it->second].source); // LCOV_EXCL_LINE
      }else{
        Pass::error("No overwrite lgraph:{} because it changed source from {} to {} (LGraph::delete first)",name, attribute[it->second].source, source); // LCOV_EXCL_LINE
      }
    }
    return it->second;
  }
  return add_name(name, source);
}

LGraph *Graph_library::try_find_lgraph(std::string_view path, std::string_view name) {

  Graph_library *lib = instance(path);
  return lib->try_find_lgraph(name);
}

bool Graph_library::exists(std::string_view path, std::string_view name) {

  const Graph_library *lib = instance(path);

  return lib->name2id.find(name) != lib->name2id.end();
}

LGraph *Graph_library::try_find_lgraph(std::string_view name) {

  if (global_name2lgraph.find(path) == global_name2lgraph.end()) {
    return nullptr; // Library exists, but not the instance for lgraph
  }

  if(global_name2lgraph[path].find(name) != global_name2lgraph[path].end()) {
    LGraph *lg = global_name2lgraph[path][name];
    assert(global_instances.find(path) != global_instances.end());
    assert(get_id(name)!=0);

    return lg;
  }

  return nullptr;
}

Lg_type_id Graph_library::add_name(std::string_view name, std::string_view source) {

  assert(source!="");

  Lg_type_id id = try_get_recycled_id();
  if (id==0) {
    id = attribute.size();
    attribute.emplace_back();
  }else{
    assert(id<attribute.size());
  }

  attribute[id].name    = name;
  attribute[id].source  = source;
  attribute[id].version = max_next_version.value++;

  graph_library_clean   = false;

  assert(name2id.find(name) == name2id.end());
  assert(id);
  name2id[name] = id;

  return id;
}

std::string Graph_library::get_lgraph_filename(std::string_view path, std::string_view name, std::string_view ext) {

  std::string f;

  f.append(path);
  f.append("/lgraph_");
  f.append(name);
  f.append("_");
  f.append(ext);

  return f;
}

bool Graph_library::rename_name(std::string_view orig, std::string_view dest) {

  if (orig == "sub_method1") {
    fmt::print("orig:{} dest:{}\n",orig,dest);
    I(false);
  }

  auto it = name2id.find(orig);
  if(it == name2id.end())
    return false;
  Lg_type_id id = it->second;

  // The dest name should not exist. Call expunge_lgraph/delete if it does
  assert(name2id.find(dest) == name2id.end());

  // The orig name should exist, BUT the lgraph should be in CLOSE state
  assert(global_name2lgraph[path].find(orig) == global_name2lgraph[path].end()); // No orig open
  assert(global_name2lgraph[path].find(dest) == global_name2lgraph[path].end()); // No dest open

  name2id.erase(it); // Erase orig

  graph_library_clean   = false;
  if (dest == "sub_method1") {
    fmt::print("rename_dest {}\n",dest);
    I(false);
  }
  attribute[id].name    = dest;
  attribute[id].version = max_next_version.value++;
  assert(attribute[id].source != ""); // Keep source

  name2id[dest] = id;

  DIR *dir = opendir(path.c_str());
  assert(dir);

  struct dirent *dent;
  while((dent = readdir(dir)) != nullptr) {
    if(dent->d_type != DT_REG) // Only regular files
      continue;

    if(strncmp(dent->d_name, "lgraph_", 7) != 0) // only if starts with lgraph_
      continue;

    const std::string name(dent->d_name + 7);
    auto              pos = name.find(std::string(orig) + "_");
    if(pos == std::string_view::npos)
      continue;
    const std::string ext(dent->d_name + 7 + orig.size() + 1);

    const std::string dest_file = get_lgraph_filename(path, dest, ext);
    const std::string orig_file = get_lgraph_filename(path, orig, ext);

    fmt::print("renaming {} to {}\n", orig_file, dest_file);
    int s = rename(orig_file.c_str(), dest_file.c_str());
    assert(s >= 0);
  }
  closedir(dir);

  clean_library();

  return true;
}

void Graph_library::update(Lg_type_id lgid) {
  assert(lgid < attribute.size());

  if(attribute[lgid].version == (max_next_version - 1))
    return;

  graph_library_clean     = false;
  attribute[lgid].version = max_next_version.value++;
}

void Graph_library::update_nentries(Lg_type_id lgid, uint64_t nentries) {
  assert(lgid < attribute.size());

  if(attribute[lgid].nentries != nentries) {
    graph_library_clean      = false;
    attribute[lgid].nentries = nentries;
  }
}

void Graph_library::reload() {

  assert(graph_library_clean);

  max_next_version = 1;
  std::ifstream graph_list;

  graph_list.open(path + "/" + library_file);

  name2id.clear();
  attribute.resize(1); // 0 is not a valid ID

  if(!graph_list.is_open()) { // No reload, just empty
    mkdir(path.c_str(), 0755); // At least make sure directory exists for future
    return;
  }

  size_t n_graphs = 0;
  graph_list >> n_graphs;
  attribute.resize(n_graphs + 1);

  for(size_t idx = 0; idx < n_graphs; ++idx) {
    std::string name;
    std::string source;
    Lg_type_id    graph_version;
    size_t      graph_id;
    size_t      nentries;

    graph_list >> name >> graph_id >> graph_version.value >> nentries;
    graph_list >> source;

    if(graph_version >= max_next_version)
      max_next_version = graph_version.value + 1;

    // this is only true in case where we skip graph ids
    if(attribute.size() <= graph_id)
      attribute.resize(graph_id + 1);

    if (name == "sub_method1") {
      fmt::print("reload {}\n",name);
      I(false);
    }
    attribute[graph_id].name     = name;
    attribute[graph_id].source   = source;
    attribute[graph_id].version  = graph_version;
    attribute[graph_id].nentries = nentries;

    // NOTE: must use attribute to keep the string in memory
    name2id[name] = graph_id;
  }

  graph_list.close();

  DIR *dir = opendir(path.c_str());
  if(!dir) {
    Pass::error("graph_library.reload: could not open {} directory",path);
    return;
  }
  struct dirent *dent;

  std::set<std::string> lg_found;

  while((dent = readdir(dir)) != nullptr) {
    if(dent->d_type != DT_REG) // Only regular files
      continue;
    if(strncmp(dent->d_name, "lgraph_", 7) != 0) // only if starts with lgraph_
      continue;
    int len = strlen(dent->d_name);
    if (len <= (7+6))
      continue;
    if(strcmp(dent->d_name + len - 6, "_nodes") != 0) // and finish with _nodes
      continue;

    std::string name(&dent->d_name[7],len-7-6);
    assert(lg_found.find(name) == lg_found.end());
    lg_found.insert(name);

    if (name2id.find(name) == name2id.end()) {
      Pass::warn("graph_library does not have {} but {} directory has it", name, path);
    }
  }
  closedir(dir);

  for(const auto &[name, id]:name2id) {
    std::string s(name);
    assert(name.size() == strlen(s.c_str()));
    if (lg_found.find(s) == lg_found.end()) {
      for( auto contents:lg_found) {
        if (contents == s)
          fmt::print("   0lg_found has [{}] [{}]\n",contents, s);
        else
          fmt::print("   1lg_found has [{}] [{}] s:{} s:{} l:{} l:{}\n",contents, s, contents.size(), s.size(), contents.length(), s.length());
      }
      Pass::error("graph_library has {} but the {} directory does not have it", s, path);
    }
  }
}

Graph_library::Graph_library(std::string_view _path)
    : path(_path)
    , library_file("graph_library") {

  graph_library_clean = true;
  reload();
}

Lg_type_id Graph_library::try_get_recycled_id() {
  if(recycled_id.none())
    return 0;

  Lg_type_id lgid = recycled_id.get_first();
  assert(lgid<=attribute.size());
  recycled_id.clear(lgid);

  return lgid;
}

void Graph_library::recycle_id(Lg_type_id lgid) {

  if (lgid < attribute.size()) {
    recycled_id.set_bit(lgid);
    return;
  }

  size_t end_pos = attribute.size();
  while(attribute.back().version==0) {
    attribute.pop_back();
    if (attribute.empty())
      break;
  }

  recycled_id.set_range(attribute.size(), end_pos, false);
}

bool Graph_library::expunge_lgraph(std::string_view name, const LGraph *lg) {
  I(name != "sub_method1");

  if(global_name2lgraph[path][name] != lg) {
    Pass::warn("graph_library::expunge_lgraph({}) for a wrong graph??? path:{}", name, path);
    return true;
  }
  global_name2lgraph[path].erase(global_name2lgraph[path].find(name));
  auto it = name2id.find(name);
  assert(it!=name2id.end());
  Lg_type_id id   = it->second;
  name2id.erase(it);

  if(attribute[id].nopen == 0) {
    attribute[id].clear();
    recycle_id(id);
    return true;
  }

  // WARNING: Do not recycle attribute (multiple ::create, and overwrite existing name)
  return false;
}

Lg_type_id Graph_library::register_lgraph(std::string_view name, std::string_view source, LGraph *lg) {

  global_name2lgraph[path][name] = lg;

  Lg_type_id id = reset_id(name, source);

  const auto &it = name2id.find(name);
  assert(it != name2id.end());
  attribute[id].nopen++;
  I(attribute[id].name == name);

  return id;
}

bool Graph_library::unregister_lgraph(std::string_view name, Lg_type_id lgid, const LGraph *lg) {
  assert(attribute.size() > (size_t)lgid);

  // WARNING: NOT ALWAYS, it can ::create multiple times before calling unregister
  // assert(name2id[name] == lgid);
  // assert(global_name2lgraph[path][name] == lg);

  if(attribute[lgid].nopen == 0)
    return true;

  attribute[lgid].nopen--;
  if(attribute[lgid].nopen == 0) {
    //expunge_lgraph(name,lg);
    return true;
  }

  return false;
}

void Graph_library::sync_all() {
  assert(false); // FIXME: to implement, once we migrate lgraph.cpp to core
#if 0
  for(auto &it:global_name2lgraph) {
    for(auto &it2:it.second) {
      it2.second->sync();
    }
  }
#endif
}

void Graph_library::clean_library() {
  if(graph_library_clean)
    return;

  std::ofstream graph_list;

  graph_list.open(path + "/" + library_file);
  graph_list << (attribute.size() - 1) << std::endl;
  for(size_t id = 1; id < attribute.size(); ++id) {
    const auto &it = attribute[id];
    if (it.version==0) // Clear/delete sets version to zero
      continue;
    graph_list << it.name << " " << id << " " << it.version << " " << it.nentries << " " << it.source << std::endl;
  }

  graph_list.close();

  graph_library_clean = true;
}

void Graph_library::each_type(std::function<void(Lg_type_id, std::string_view)> f1) const {

  for(const auto [name,id] : name2id) {
    f1(id, name);
  }

}
void Graph_library::each_type(std::function<bool(Lg_type_id, std::string_view)> f1) const {

  for(const auto [name,id] : name2id) {
    bool cont = f1(id,name);
    if (!cont)
      break;
  }

}

void Graph_library::each_type(std::string_view match, std::function<void(Lg_type_id, std::string_view)> f1) const {

  const std::string string_match(match); // NOTE: regex does not support string_view, c++20 may fix this missing feature
  const std::regex txt_regex(string_match);

  for(const auto [name,id] : name2id) {
    const std::string line(name);
    if (!std::regex_search(line, txt_regex))
      continue;

    f1(id, name);
  }

}

void Graph_library::each_type(std::string_view match, std::function<bool(Lg_type_id, std::string_view)> f1) const {

  const std::string string_match(match); // NOTE: regex does not support string_view, c++20 may fix this missing feature
  const std::regex txt_regex(string_match);

  for(const auto [name,id] : name2id) {
    const std::string line(name);
    if (!std::regex_search(line, txt_regex))
      continue;

    bool cont = f1(id, name);
    if (!cont)
      break;
  }

}
