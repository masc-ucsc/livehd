//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_library.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <climits>
#include <cstdlib>
#include <memory>

#ifdef __APPLE__
#include <copyfile.h>
#else
#include <sys/sendfile.h>
#endif

#include <cassert>
#include <fstream>
#include <regex>
#include <set>

#include "fmt/format.h"
#include "lgraph.hpp"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"

Graph_library::Global_instances Graph_library::global_instances;

class Cleanup_graph_library {
public:
  Cleanup_graph_library() = default;
  ~Cleanup_graph_library() {
    Graph_library::sync_all();
    Graph_library::shutdown();
  };
};

static Cleanup_graph_library private_instance;

void Graph_library::shutdown_int() {
  // global_instances has many inserts with full and short path
  absl::flat_hash_set<Graph_library *> deleted;

  for (const auto &it : global_instances) {
    if (deleted.contains(it.second)) {
      continue;
    }
    deleted.insert(it.second);
    delete it.second;
  }
  global_instances.clear();
}

void Graph_library::sync_all_int() {
  for (auto &it : global_instances) {
    it.second->clean_library_int();
  }
}

void Graph_library::clean_library_int() {
#if 0
  // Possible to call sub_nodes directly and miss this update
  if (graph_library_clean)
    return;
#endif

  rapidjson::StringBuffer                          s;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);

  writer.StartObject();

  writer.Key("Lgraph");
  writer.StartArray();
  // NOTE: insert in reverse order to reduce number of resizes when loading
  for (size_t i = attributes.size() - 1; i >= 1; --i) {  // Not position zero
    const auto &it = attributes[i];
    writer.StartObject();

    writer.Key("version");
    writer.Uint64(it.version);

    writer.Key("source");
    writer.String(it.source.c_str());

    sub_nodes[i]->to_json(writer);

    writer.EndObject();
  }
  writer.EndArray();

  writer.Key("liberty");
  writer.StartArray();
  for (const auto &lib : liberty_list) {
    writer.StartObject();

    writer.Key("file");
    writer.String(lib.c_str());

    writer.EndObject();
  }
  writer.EndArray();

  writer.Key("sdc");
  writer.StartArray();
  for (const auto &lib : sdc_list) {
    writer.StartObject();

    writer.Key("file");
    writer.String(lib.c_str());

    writer.EndObject();
  }
  writer.EndArray();

  writer.Key("spef");
  writer.StartArray();
  for (const auto &lib : spef_list) {
    writer.StartObject();

    writer.Key("file");
    writer.String(lib.c_str());

    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();

  {
    std::ofstream fs;

    fs.open(library_file, std::ios::out | std::ios::trunc);
    if (!fs.is_open()) {
      Lgraph::error("graph_library::clean_library could not open graph_library file {}", library_file);
      return;
    }
    fs << s.GetString() << std::endl;
    fs.close();
  }

  graph_library_clean = true;
}

Graph_library *Graph_library::instance_int(std::string_view path) {
  auto it1 = Graph_library::global_instances.find(path);
  if (it1 != Graph_library::global_instances.end()) {
    return it1->second;
  }

  std::string spath(path);

  char  full_path_char[PATH_MAX + 1];
  char *ptr = realpath(spath.c_str(), full_path_char);
  if (ptr == nullptr) {
    int ok = mkdir(spath.c_str(), 0755);  // At least make sure directory exists for future
    if (ok < 0) {                         // no access
      Lgraph::error("could not open lgdb:{} path\n", spath);
      return nullptr;
    }
    ptr = realpath(spath.c_str(), full_path_char);
    I(ptr);
  }

  std::string full_path(full_path_char, strnlen(full_path_char, PATH_MAX));

  auto it = global_instances.find(full_path);
  if (it != global_instances.end()) {
    return it->second;
  }

  if (access(full_path_char, W_OK) == -1) {
    Lgraph::error("could not open lgdb:{} path\n", full_path_char);
    return nullptr;
  }

  auto *graph_library = new Graph_library(full_path);
  global_instances.insert(std::make_pair(full_path, graph_library));
  global_instances.insert(std::make_pair(spath, graph_library));

  return graph_library;
}

Lg_type_id Graph_library::reset_id_int(std::string_view name, std::string_view source) {
  graph_library_clean = false;

  const auto &it = name2id.find(name);
  if (it != name2id.end()) {
    // Maybe it was a sub before, or reloaded, or the ID got recycled
    attributes[it->second].version = max_next_version++;
    if (attributes[it->second].source != source) {
      if (source == "-") {
        // Lgraph::warn("keeping Lgraph:{} source {}", name, attributes[it->second].source);  // LCOV_EXCL_LINE
      } else if (attributes[it->second].source.empty()) {
        // Blackbox with a newly populated. OK
        attributes[it->second].source = source;
      } else if (attributes[it->second].source == "-") {
        // Lgraph::warn("overwrite Lgraph:{} source from {} to {}", name, attributes[it->second].source, source);  // LCOV_EXCL_LINE
        attributes[it->second].source = source;
      } else {
        auto src = attributes[it->second].source;
        Lgraph::error("No overwrite Lgraph:{} because it changed source from {} to {} (Lgraph::delete first)",
                      name,
                      src,
                      source);  // LCOV_EXCL_LINE
      }
    }
    attributes[it->second].tried_to_load = false;
    return it->second;
  }
  return add_name_int(name, source);
}

bool Graph_library::exists_int(std::string_view name) const { return name2id.find(name) != name2id.end(); }

bool Graph_library::exists_int(Lg_type_id lgid) const {
  if (sub_nodes.size() <= lgid || lgid.is_invalid()) {
    return false;
  }
  I(attributes.size() > lgid);

  return sub_nodes[lgid]->get_lgid() == lgid;
}

Lgraph *Graph_library::try_ref_lgraph_int(std::string_view name) const {
  const auto it = name2id.find(name);
  if (it == name2id.end()) {
    return nullptr;
  }

  return attributes[it->second].lg;
}

Lgraph *Graph_library::try_ref_lgraph_int(const Lg_type_id lgid) const {
  if (lgid >= attributes.size()) {
    return nullptr;
  }

  return attributes[lgid].lg;
}

Sub_node *Graph_library::create_sub_int(std::string_view name, std::string_view source) {
  graph_library_clean = false;

  Lg_type_id lgid = get_lgid_int(name);
  if (lgid) {
    if (attributes[lgid].source != source) {
      // Lgraph::info("module {} changed source changed from {} to {}\n", name, attributes[lgid].source, source);
      attributes[lgid].source = source;
    }
    auto sub = sub_nodes[lgid];
    sub->reset_pins();

    return sub;
  }

  lgid = add_name_int(name, source);
  I(lgid);
  return sub_nodes[lgid];
}

Sub_node *Graph_library::ref_or_create_sub(std::string_view name, std::string_view source) {
  Lg_type_id lgid;

  {
    absl::ReaderMutexLock guard(&lgs_mutex);
    lgid = get_lgid_int(name);
    if (lgid) {
      return sub_nodes[lgid];
    }
  }
  lgid = add_name(name, source);  // This acquires a WriterMutexLock

  I(lgid);
  return sub_nodes[lgid];
}

Sub_node *Graph_library::ref_sub_int(Lg_type_id lgid) {
  graph_library_clean = false;
  I(lgid > 0);  // 0 is invalid lgid
  I(attributes.size() > lgid);
  I(sub_nodes.size() > lgid);
  I(sub_nodes[lgid]->get_lgid() == lgid);
  return sub_nodes[lgid];
}

const Sub_node &Graph_library::get_sub_int(Lg_type_id lgid) const {
  I(lgid > 0);  // 0 is invalid lgid
  I(attributes.size() > lgid);
  I(sub_nodes.size() > lgid);
  I(sub_nodes[lgid]->get_lgid() == lgid);
  return *sub_nodes[lgid];
}

Lg_type_id Graph_library::add_name_int(std::string_view name, std::string_view source) {
  I(source != "");

  Lg_type_id id = try_get_recycled_id_int();
  if (id == 0) {
    id = attributes.size();
    attributes.emplace_back();
    sub_nodes.emplace_back(new Sub_node());
  }

  I(id < attributes.size());
  I(id < sub_nodes.size());
  sub_nodes[id]->reset(name, id);
  attributes[id].source  = source;
  attributes[id].version = max_next_version++;

  graph_library_clean = false;

  I(name2id.find(name) == name2id.end());
  I(id);
  name2id[name] = id;

  return id;
}

bool Graph_library::rename_name_int(std::string_view orig, std::string_view dest) {
  auto it = name2id.find(orig);
  if (it == name2id.end()) {
    Lgraph::error("graph_library: file to rename {} does not exit", orig);
    return false;
  }

  Lg_type_id id = it->second;

  auto dest_it = name2id.find(dest);
  if (dest_it != name2id.end()) {
    expunge_int(dest);
  }

  name2id.erase(it);
  I(name2id.find(orig) == name2id.end());

  graph_library_clean = false;
  sub_nodes[id]->rename(dest);
  I(sub_nodes[id]->get_lgid() == id);

  name2id[dest] = id;

  clean_library_int();
  return true;
}

void Graph_library::update_int(Lg_type_id lgid) {
  I(lgid < attributes.size());

  if (attributes[lgid].version == (max_next_version - 1)) {
    return;
  }

  graph_library_clean      = false;
  attributes[lgid].version = max_next_version++;
}

void Graph_library::reload_int() {
  I(graph_library_clean);

  max_next_version = 1;
  std::ifstream graph_list;

  // FIXME: BEGIN DELETE THIS and replace with json reload

  liberty_list.emplace_back("fake_bad.lib");  // FIXME
  sdc_list.emplace_back("fake_bad.sdc");      // FIXME
  spef_list.emplace_back("fake_bad.spef");    // FIXME
  {
    name2id.clear();
    attributes.resize(1);                 // 0 is not a valid ID
    sub_nodes.resize(1, new Sub_node());  // 0 is not a valid ID
  }
  if (access(library_file.c_str(), F_OK) == -1) {
    mkdir(path.c_str(), 0755);  // At least make sure directory exists for future
    return;
  }
  FILE *pFile = fopen(library_file.c_str(), "rb");
  if (pFile == 0) {
    Lgraph::error("graph_library::reload could not open graph {} file", library_file);
    return;
  }
  char                      buffer[65536];
  rapidjson::FileReadStream is(pFile, buffer, sizeof(buffer));
  rapidjson::Document       document;
  document.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(is);

  if (document.HasParseError()) {
    Lgraph::error("graph_library::reload {} Error(offset {}): {}",
                  library_file,
                  static_cast<unsigned>(document.GetErrorOffset()),
                  rapidjson::GetParseError_En(document.GetParseError()));
    return;
  }

  I(document.HasMember("Lgraph"));
  const rapidjson::Value &Lgraph_array = document["Lgraph"];
  I(Lgraph_array.IsArray());

  for (const auto &lg_entry : Lgraph_array.GetArray()) {
    I(lg_entry.IsObject());

    I(lg_entry.HasMember("lgid"));
    I(lg_entry.HasMember("version"));

    uint64_t id = lg_entry["lgid"].GetUint64();

    if (id >= attributes.size()) {
      attributes.resize(id + 1);

      // FIXME->sh: wiered bug that the two pointer, sub_nodes[1] and sub_nodes[2], will pollute each other when using resize (size,
      // initial value) ??
      //           to avoid such bug, I create Sub_node() pointers and emplace_back them one by one.
      // sub_nodes.resize(id + 1, new Sub_node());
      auto increase_size = id - sub_nodes.size() + 1;
      if (increase_size > 0) {
        for (std::string::size_type i = 0; i < increase_size; i++) {
          auto ptr = new Sub_node();
          sub_nodes.emplace_back(ptr);
        }
      }
    }

    auto version = lg_entry["version"].GetUint64();
    if (version != 0) {
      if (max_next_version < version) {
        max_next_version = version;
      }

      I(lg_entry.HasMember("source"));
      attributes[id].source  = lg_entry["source"].GetString();
      attributes[id].version = version;

      sub_nodes[id]->from_json(lg_entry);
      // fmt::print("DEBUG21, sub_nodes size:{}, sub_nodes[{}]->get_lgid():{}, name:{}\n\n", sub_nodes.size(), id,
      // sub_nodes[id]->get_lgid(), sub_nodes[id]->get_name());

      // NOTE: must use attributes to keep the string in memory
      name2id[sub_nodes[id]->get_name()] = id;
      I(sub_nodes[id]->get_lgid() == id);  // for consistency
    } else {
      recycled_id.insert(id);
    }
  }
}

Lgraph *Graph_library::ref_or_create_lgraph_int(std::string_view name, std::string_view source) {
  auto *lg = try_ref_lgraph_int(name);
  if (lg) {
    return lg;
  }

  auto lgid = reset_id_int(name, source);

  lg = new Lgraph(path, name, lgid, this);

  lg->clear_int();

  attributes[lgid].lg            = lg;    // It could be already set if there was a copy
  attributes[lgid].tried_to_load = true;  // already present

#ifndef NDEBUG
  const auto &it = name2id.find(name);
  I(it != name2id.end());
  I(sub_nodes[lgid]->get_name() == name);
#endif

  return lg;
}

Lgraph *Graph_library::create_lgraph_int(std::string_view name, std::string_view source) {
  auto *lg = ref_or_create_lgraph_int(name, source);
  lg->clear_int();
  clear_int(lg->get_lgid());

  graph_library_clean      = false;
  attributes[lg->get_lgid()].version = max_next_version++;

  return lg;
}

Lgraph *Graph_library::do_pending_load_int(Lg_id_t lgid) {
  auto name = get_name_int(lgid);

  auto &attr = attributes[lgid];

  std::shared_ptr<Hif_read> hif;
  if (name.substr(0, 2) != "__") {
    hif = Hif_read::open(absl::StrCat(path, "/", name));
  }
  if (hif == nullptr) {
    attr.lg            = nullptr;
    attr.tried_to_load = true;
    return nullptr;
  }

  auto *lg = new Lgraph(path, name, lgid, this);

  lg->load(hif);

  attr.tried_to_load = true;
  attr.lg            = lg;

  return lg;
}

Lgraph *Graph_library::open_or_create_lgraph(std::string_view name, std::string_view source) {
  Lgraph *lg = nullptr;
  bool    pending_load;
  {
    absl::WriterMutexLock guard(&lgs_mutex);
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    std::tie(lg, pending_load) = open_or_create_lgraph_int(name, source);
  }
  if (pending_load) {  // out of the lock because it can be slow and call graph library
    I(lg == nullptr);
    auto lgid = get_lgid_int(name);
    return do_pending_load_int(lgid);
  }
  return lg;
}

Lgraph *Graph_library::open_lgraph(std::string_view name, std::string_view source) {
  Lgraph *lg = nullptr;
  bool    pending_load;
  {
    absl::WriterMutexLock guard(&lgs_mutex);
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    std::tie(lg, pending_load) = open_lgraph_int(name, source);
  }
  if (pending_load) {  // out of the lock because it can be slow and call graph library
    I(lg == nullptr);
    auto lgid = get_lgid_int(name);
    return do_pending_load_int(lgid);
  }
  return lg;
}

Lgraph *Graph_library::open_lgraph(Lg_id_t lgid) {
  Lgraph *lg = nullptr;
  bool    pending_load;
  {
    absl::WriterMutexLock guard(&lgs_mutex);
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    std::tie(lg, pending_load) = open_lgraph_int(lgid);
  }
  if (pending_load) {  // out of the lock because it can be slow and call graph library
    I(lg == nullptr);
    return do_pending_load_int(lgid);
  }
  return lg;
}

std::pair<Lgraph *, bool> Graph_library::open_lgraph_int(Lg_id_t lgid) {
  I(attributes.size() > lgid);

  const auto &attr = attributes[lgid];
  if (attr.is_blackbox()) {
    return {nullptr, false};  // already tried to open and failed
  }
  if (attr.lg) {
    return {attr.lg, false};
  }

  return {nullptr, true};
}

std::pair<Lgraph *, bool> Graph_library::open_lgraph_int(std::string_view name, std::string_view source) {
  auto lgid = get_lgid_int(name);
  if (!lgid) {                          // no lgid exists
    lgid = reset_id_int(name, source);  // get new ID
  }

  return open_lgraph_int(lgid);
}

std::pair<Lgraph *, bool> Graph_library::open_or_create_lgraph_int(std::string_view name, std::string_view source) {
  auto [lg, pending_load] = open_lgraph_int(name, source);
  if (lg) {
    return {lg, pending_load};
  }
  I(!pending_load);

  return {create_lgraph_int(name, source), false};
}

Graph_library::Graph_library(std::string_view _path) : path(_path), library_file(absl::StrCat(path, "/", "graph_library.json")) {
  graph_library_clean = true;
  reload_int();
}

Lg_type_id Graph_library::try_get_recycled_id_int() {
  if (recycled_id.empty()) {
    return 0;
  }

  auto       it   = recycled_id.begin();
  Lg_type_id lgid = *it;
  I(lgid <= attributes.size());
  I(lgid <= sub_nodes.size());
  recycled_id.erase(it);

  return lgid;
}

void Graph_library::recycle_id_int(Lg_type_id lgid) { recycled_id.insert(lgid); }

void Graph_library::expunge_int(std::string_view name) {
  auto it2 = name2id.find(name);
  if (it2 == name2id.end()) {
    return;  // already gone
  }

  auto id = it2->second;

  attributes[id].expunge();
  recycle_id_int(id);

  DIR *dr = opendir(path.c_str());
  if (dr == NULL) {
    Lgraph::error("graph_library: unable to access path {}", path);
    return;
  }

  struct dirent *de;  // Pointer for directory entry
  std::string    match = absl::StrCat("Lgraph_", std::to_string(id));
  while ((de = readdir(dr)) != NULL) {
    std::string chop_name(de->d_name, match.size());
    if (chop_name == match) {
      std::string file = absl::StrCat(path, "/", de->d_name);
      fmt::print("deleting... {}\n", file);
      unlink(file.c_str());
    }
  }
  closedir(dr);

  sub_nodes[id]->expunge();  // Nuke IO and contents, but keep around lgid
}

void Graph_library::clear_int(Lg_type_id lgid) {
  I(lgid < attributes.size());

  sub_nodes[lgid]->reset_pins();
}

Lg_type_id Graph_library::copy_lgraph_int(std::string_view name, std::string_view new_name) {
  I(false);
  graph_library_clean = false;
  const auto &it      = name2id.find(name);
  I(it != name2id.end());
  auto id_orig = it->second;
  I(sub_nodes[id_orig]->get_name() == name);

  Lg_type_id id_new = reset_id_int(new_name, attributes[id_orig].source);

  attributes[id_new] = attributes[id_orig];
  sub_nodes[id_new]->copy_from(new_name, id_new, *sub_nodes[id_orig]);

  DIR *dr = opendir(path.c_str());
  if (dr == NULL) {
    Lgraph::error("graph_library: unable to access path {}", path);
    return false;
  }

  struct dirent *de;  // Pointer for directory entry
  std::string    match   = absl::StrCat("lg_", std::to_string(id_orig));
  std::string    rematch = absl::StrCat("lg_", std::to_string(id_new));

  while ((de = readdir(dr)) != NULL) {
    std::string chop_name(de->d_name, match.size());
    if (chop_name == match) {
      std::string_view dname(de->d_name);
      std::string      file      = absl::StrCat(path, "/", dname);
      std::string_view extension = dname.substr(match.size());

      auto new_file = absl::StrCat(path, "/", rematch, extension);

      int source = open(file.c_str(), O_RDONLY, 0);
      int dest   = open(new_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

#ifdef __APPLE__
      fcopyfile(dest, source, nullptr, COPYFILE_DATA);
#else
      // struct required, rationale: function stat() exists also
      struct stat stat_source;
      fstat(source, &stat_source);

      sendfile(dest, source, nullptr, stat_source.st_size);
#endif

      close(source);
      close(dest);
    }
  }

  closedir(dr);

  clean_library_int();

  return id_new;
}

void Graph_library::unregister_int(Lgraph *lg) {
  auto lgid = lg->get_lgid();
  I(attributes.size() > (size_t)lgid);
  attributes[lgid].lg            = nullptr;
  attributes[lgid].tried_to_load = false;

  if (sub_nodes[lgid]->is_invalid()) {
    expunge_int(lg->get_name());
  }
}

void Graph_library::each_lgraph(const std::function<void(Lg_type_id lgid, std::string_view name)> &f1) const {
  for (const auto &[name, id] : name2id) {
    f1(id, name);
  }
}

void Graph_library::each_lgraph(std::string_view                                                   match,
                                const std::function<void(Lg_type_id lgid, std::string_view name)> &f1) const {
  const std::string string_match(match);  // NOTE: regex does not support string_view, c++20 may fix this missing feature
  const std::regex  txt_regex(string_match);

  for (const auto &[name, id] : name2id) {
    const std::string line(name);
    if (!std::regex_search(line, txt_regex)) {
      continue;
    }

    f1(id, name);
  }
}
