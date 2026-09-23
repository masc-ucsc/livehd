// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "witness.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <stdexcept>

#include "hash_util.hpp"
#include "json_util.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::synth {
namespace {
struct Size_limit {};
class Json {
public:
  std::string text;
  explicit Json(uint64_t limit) : limit_(limit) {}
  void add(std::string_view value) {
    if (value.size() > limit_ - text.size()) {
      throw Size_limit{};
    }
    text.append(value);
  }
  void number(uint64_t n) { add(std::to_string(n)); }
  void quote(std::string_view s) {
    add("\"");
    add(json_util::escape(s));
    add("\"");
  }
  template <typename T, typename F>
  void array(const T& values, F write) {
    add("[");
    bool first = true;
    for (const auto& v : values) {
      if (!first) {
        add(",");
      }
      first = false;
      write(v);
    }
    add("]");
  }
  void ids(const std::vector<Id>& values) {
    array(values, [&](Id id) { number(id); });
  }
  void table(const Truth_table& t) {
    add("{\"inputs\":");
    number(t.inputs);
    add(",\"words\":");
    array(t.words, [&](uint64_t w) { quote(std::format("{:016x}", w)); });
    add("}");
  }

private:
  uint64_t limit_;
};

void search_json(Json& j, const Search_options& search) {
  j.add(",\"search\":{\"work\":");
  j.number(search.work);
  j.add(",\"cuts_per_node\":");
  j.number(search.cuts_per_node);
  j.add(",\"max_nodes\":");
  j.number(search.max_nodes);
  j.add(",\"recovery_rounds\":");
  j.number(search.recovery_rounds);
  j.add(",\"joint_limit\":");
  j.number(search.joint_limit);
  j.add(",\"joint_windows\":");
  j.number(search.joint_windows);
  j.add(",\"image_inputs\":");
  j.number(search.image_inputs);
  j.add(",\"divisor_limit\":");
  j.number(search.divisor_limit);
  j.add(",\"cover_limit\":");
  j.number(search.cover_limit);
  j.add(",\"symbolic_nodes\":");
  j.number(search.symbolic_nodes);
  j.add(",\"encoding_limit\":");
  j.number(search.encoding_limit);
  j.add(",\"encoding_code_limit\":");
  j.number(search.encoding_code_limit);
  j.add(",\"encoding_pair_limit\":");
  j.number(search.encoding_pair_limit);
  j.add(",\"reshape_limit\":");
  j.number(search.reshape_limit);
  j.add(",\"recipes\":");
  j.array(search.recipes, [&](const Recipe& r) {
    j.add("[");
    j.number(r.levels);
    j.add(",");
    j.number(r.support);
    j.add(",");
    j.number(r.literals);
    j.add(",");
    j.number(r.series);
    j.add("]");
  });
  j.add("}");
}

std::string source_json(const Logic_network& source, uint64_t limit) {
  Json j(limit);
  j.add("{\"nodes\":");
  j.array(source.nodes, [&](const Logic_node& n) {
    j.add(n.source ? "{\"source\":true,\"inputs\":" : "{\"source\":false,\"inputs\":");
    j.ids(n.inputs);
    j.add(",\"table\":");
    j.table(n.table);
    j.add("}");
  });
  j.add(",\"outputs\":");
  j.ids(source.outputs);
  j.add("}");
  return std::move(j.text);
}

std::string boundary_json(const Source_boundary& boundary, const Logic_network& source, uint64_t limit) {
  std::vector<Id> source_ids;
  for (Id i = 0; i < source.nodes.size(); ++i) {
    if (source.nodes[i].source) {
      source_ids.push_back(i);
    }
  }
  if (boundary.inputs.size() != source_ids.size() || boundary.outputs.size() != source.outputs.size()) {
    throw std::invalid_argument("source boundary port count mismatch");
  }
  Json j(limit);
  j.add(
      "{\"scope\":\"abc_ci_co_register_cut\",\"names_scope\":\"original_search_region\",\"clock_reset_semantics_verified\":false,"
      "\"inputs\":");
  const auto ports = [&](const auto& entries, const auto& bindings) {
    size_t index = 0;
    j.array(entries, [&](const Source_boundary::Port& port) {
      j.add("{\"name\":");
      j.quote(port.name);
      j.add(",\"kind\":");
      j.quote(port.kind);
      j.add(",\"state\":");
      j.add(std::to_string(port.state));
      j.add(",\"source_node\":");
      j.number(bindings[index++]);
      j.add("}");
    });
  };
  ports(boundary.inputs, source_ids);
  j.add(",\"outputs\":");
  ports(boundary.outputs, source.outputs);
  j.add(",\"states\":");
  j.array(boundary.states, [&](const Source_boundary::State& state) {
    j.add("{\"name\":");
    j.quote(state.name);
    j.add(",\"init\":");
    j.quote(state.init);
    j.add(",\"input\":");
    j.number(state.input);
    j.add(",\"output\":");
    j.number(state.output);
    j.add("}");
  });
  j.add("}");
  return std::move(j.text);
}

void network_json(Json& j, const Unate_network& network) {
  j.add("{\"encodings\":");
  j.array(network.encodings, [&](const Logic_node& n) {
    j.add("{\"inputs\":");
    j.ids(n.inputs);
    j.add(",\"table\":");
    j.table(n.table);
    j.add("}");
  });
  j.add(",\"nodes\":");
  j.array(network.nodes, [&](const Unate_node& n) {
    const auto kind = n.kind == Node_kind::source            ? "source"
                      : n.kind == Node_kind::source_inverter ? "source_inverter"
                      : n.kind == Node_kind::function        ? "function"
                                                             : "invalid";
    j.add("{\"kind\":");
    j.quote(kind);
    j.add(",\"origin\":");
    j.number(n.origin);
    j.add(n.negative ? ",\"negative\":true" : ",\"negative\":false");
    j.add(",\"ports\":");
    j.ids(n.ports);
    j.add(",\"terms\":");
    j.array(n.terms, [&](const auto& term) { j.ids(term); });
    j.add(",\"logical_inputs\":");
    j.ids(n.logical_inputs);
    j.add(",\"witness_inputs\":");
    j.ids(n.witness_inputs);
    j.add(",\"completion\":");
    j.table(n.completion);
    j.add(",\"care\":");
    if (n.care) {
      j.table(*n.care);
    } else {
      j.add("null");
    }
    j.add(",\"image_sources\":");
    j.ids(n.image_sources);
    j.add(n.functional ? ",\"functional\":true" : ",\"functional\":false");
    j.add(",\"level\":");
    j.number(n.level);
    j.add("}");
  });
  j.add(",\"outputs\":");
  j.ids(network.outputs);
  j.add(",\"depth\":");
  j.number(network.depth);
  j.add(",\"max_support\":");
  j.number(network.max_support);
  j.add(",\"max_literals\":");
  j.number(network.max_literals);
  j.add(",\"max_series\":");
  j.number(network.max_series);
  j.add(",\"source_inverters\":");
  j.number(network.source_inverters);
  j.add("}");
}
}  // namespace

Witness_archive::Witness_archive(const std::filesystem::path& path, std::string version, uint64_t max_bytes,
                                 const Search_options& search)
    : path_(path)
    , output_(path, std::ios::binary | std::ios::trunc)
    , version_(std::move(version))
    , search_(search)
    , max_bytes_(max_bytes) {
  if (!output_) {
    throw std::runtime_error("cannot create synthesis witness archive");
  }
}

Witness_record Witness_archive::append_source(std::string_view region, const Logic_network& source,
                                              const Source_boundary* boundary) {
  if (!max_bytes_) {
    return skip("disabled");
  }
  try {
    const auto source_text = source_json(source, max_bytes_ - bytes_);
    Json       j(max_bytes_ - bytes_);
    j.add(boundary ? "{\"schema_version\":2,\"kind\":\"unate_source\",\"record\":"
                   : "{\"schema_version\":1,\"kind\":\"unate_source\",\"record\":");
    j.number(records_);
    j.add(",\"producer_version\":");
    j.quote(version_);
    j.add(",\"region\":");
    j.quote(region);
    search_json(j, search_);
    j.add(",\"source_digest_fnv1a64\":");
    j.quote(std::format("{:016x}", hash_util::fnv1a64(source_text)));
    j.add(",\"source\":");
    j.add(source_text);
    if (boundary) {
      const auto text = boundary_json(*boundary, source, max_bytes_ - bytes_);
      j.add(",\"boundary\":");
      j.add(text);
      j.add(",\"boundary_digest_fnv1a64\":");
      j.quote(std::format("{:016x}", hash_util::fnv1a64(text)));
    }
    j.add("}\n");
    output_.write(j.text.data(), j.text.size());
    output_.flush();
    if (!output_) {
      throw std::runtime_error("cannot archive synthesis source");
    }
    bytes_ += j.text.size();
    return {static_cast<int64_t>(records_++), "archived"};
  } catch (const Size_limit&) {
    return skip("size_limit");
  }
}

Witness_record Witness_archive::append(std::string_view region, size_t recipe_index, std::string_view variant,
                                       const Logic_network& source, const Unate_network& network, const Recipe& recipe) {
  if (!max_bytes_) {
    ++omitted_;
    return {-1, "disabled"};
  }
  try {
    const auto source_text = source_json(source, max_bytes_ - bytes_);
    Json       j(max_bytes_ - bytes_);
    const bool hierarchical
        = network.encodings.size() > 12 || std::any_of(network.encodings.begin(), network.encodings.end(), [&](const auto& n) {
            return std::any_of(n.inputs.begin(), n.inputs.end(), [&](Id id) { return id >= source.nodes.size(); });
          });
    j.add("{\"schema_version\":");
    j.number(hierarchical ? 5 : 4);
    j.add(",\"kind\":\"unate_witness\",\"record\":");
    j.number(records_);
    j.add(",\"producer_version\":");
    j.quote(version_);
    j.add(",\"region\":");
    j.quote(region);
    j.add(",\"recipe_index\":");
    j.number(recipe_index);
    j.add(",\"variant\":");
    j.quote(variant);
    search_json(j, search_);
    j.add(",\"recipe\":[");
    j.number(recipe.levels);
    j.add(",");
    j.number(recipe.support);
    j.add(",");
    j.number(recipe.literals);
    j.add(",");
    j.number(recipe.series);
    j.add("]");
    j.add(",\"source_digest_fnv1a64\":");
    j.quote(std::format("{:016x}", hash_util::fnv1a64(source_text)));
    j.add(",\"source\":");
    j.add(source_text);
    j.add(",\"network\":");
    network_json(j, network);
    j.add("}\n");
    output_.write(j.text.data(), static_cast<std::streamsize>(j.text.size()));
    output_.flush();  // preserve witnesses if a later mapping/proof reports a defect
    if (!output_) {
      throw std::runtime_error("cannot write synthesis witness archive");
    }
    bytes_ += j.text.size();
    return {static_cast<int64_t>(records_++), "archived"};
  } catch (const Size_limit&) {
    ++omitted_;
    return {-1, "size_limit"};
  }
}

void Witness_archive::close() {
  output_.close();
  if (!output_) {
    throw std::runtime_error("cannot close synthesis witness archive");
  }
}

namespace {
std::string encode(const rapidjson::Value& value) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return {buffer.GetString(), buffer.GetSize()};
}
rapidjson::Value string_value(std::string_view text, rapidjson::Document::AllocatorType& alloc) {
  return rapidjson::Value(text.data(), static_cast<rapidjson::SizeType>(text.size()), alloc);
}
bool has(const rapidjson::Value& value, const char* key) { return value.IsObject() && value.HasMember(key); }
bool text_is(const rapidjson::Value& value, const char* key, std::string_view expected) {
  return has(value, key) && value[key].IsString()
         && std::string_view(value[key].GetString(), value[key].GetStringLength()) == expected;
}
bool integer(const rapidjson::Value& value, const char* key) { return has(value, key) && value[key].IsInt64(); }

bool validate(const rapidjson::Document& doc) {
  if (doc.HasParseError() || !integer(doc, "schema_version") || doc["schema_version"].GetInt64() != 1 || !has(doc, "decision")
      || !has(doc, "witnesses") || !doc["witnesses"].IsArray()) {
    return false;
  }
  const auto& decision = doc["decision"];
  if (!has(decision, "region") || !decision["region"].IsString() || !has(decision, "attempts") || !decision["attempts"].IsArray()) {
    return false;
  }
  std::map<int64_t, const rapidjson::Value*> records;
  for (const auto& record : doc["witnesses"].GetArray()) {
    const bool source = text_is(record, "kind", "unate_source");
    if (!integer(record, "record") || record["record"].GetInt64() < 0 || !integer(record, "schema_version")
        || (record["schema_version"].GetInt64() < 1 || record["schema_version"].GetInt64() > 5)
        || (!source && !text_is(record, "kind", "unate_witness")) || (source && record["schema_version"].GetInt64() > 2)
        || (source && record["schema_version"].GetInt64() == 2
            && (!has(record, "boundary") || !record["boundary"].IsObject() || !has(record, "boundary_digest_fnv1a64")
                || !record["boundary_digest_fnv1a64"].IsString()))
        || !text_is(record, "region", decision["region"].GetString()) || !has(record, "source") || !record["source"].IsObject()
        || (!source && (!has(record, "network") || !record["network"].IsObject()))
        || !records.emplace(record["record"].GetInt64(), &record).second) {
      return false;
    }
  }
  std::set<int64_t> used;
  if (has(decision, "source_witness")) {
    const auto& w = decision["source_witness"];
    if (!integer(w, "record") || !has(w, "status") || !w["status"].IsString()) {
      return false;
    }
    const auto id = w["record"].GetInt64();
    if (text_is(w, "status", "archived")) {
      const auto found = records.find(id);
      if (found == records.end() || !text_is(*found->second, "kind", "unate_source") || !used.insert(id).second) {
        return false;
      }
    } else if (id != -1) {
      return false;
    }
  }
  for (const auto& attempt : decision["attempts"].GetArray()) {
    if (!has(attempt, "witness") || !integer(attempt["witness"], "record") || !has(attempt["witness"], "status")
        || !attempt["witness"]["status"].IsString()) {
      return false;
    }
    const auto& w  = attempt["witness"];
    const auto  id = w["record"].GetInt64();
    if (text_is(w, "status", "archived")) {
      const auto found = records.find(id);
      if (found == records.end() || !text_is(*found->second, "kind", "unate_witness") || !used.insert(id).second
          || !integer(attempt, "recipe_index") || !integer(*found->second, "recipe_index")
          || attempt["recipe_index"] != (*found->second)["recipe_index"] || !has(attempt, "variant")
          || !attempt["variant"].IsString() || !text_is(*found->second, "variant", attempt["variant"].GetString())) {
        return false;
      }
    } else if (id != -1) {
      return false;
    }
  }
  return used.size() == records.size();
}
}  // namespace

std::string Witness_archive::capture_since(uint64_t begin) const {
  if (begin > bytes_) {
    throw std::runtime_error("invalid synthesis witness checkpoint");
  }
  std::ifstream file(path_, std::ios::binary);
  file.seekg(static_cast<std::streamoff>(begin));
  std::string text(bytes_ - begin, '\0');
  file.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!file) {
    throw std::runtime_error("cannot capture synthesis witnesses");
  }
  return text;
}

Witness_record Witness_archive::replay(std::string_view record, std::string_view region) {
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseFullPrecisionFlag>(record.data(), record.size());
  if (doc.HasParseError() || !has(doc, "record") || !has(doc, "region") || !doc["region"].IsString()) {
    throw std::runtime_error("invalid cached synthesis witness");
  }
  auto&            alloc = doc.GetAllocator();
  // Preserve the originating region separately from this invocation's name.
  rapidjson::Value provenance(rapidjson::kObjectType);
  provenance.AddMember("region", rapidjson::Value(doc["region"], alloc), alloc);
  provenance.AddMember("record", rapidjson::Value(doc["record"], alloc), alloc);
  doc.AddMember("reused_from", provenance, alloc);
  doc["record"].SetUint64(records_);
  doc["region"]   = string_value(region, alloc);
  const auto text = encode(doc) + "\n";
  if (!max_bytes_ || text.size() > max_bytes_ - bytes_) {
    return skip(max_bytes_ ? "size_limit" : "disabled");
  }
  output_.write(text.data(), static_cast<std::streamsize>(text.size()));
  output_.flush();
  if (!output_) {
    throw std::runtime_error("cannot replay synthesis witness");
  }
  bytes_ += text.size();
  return {static_cast<int64_t>(records_++), "archived"};
}

std::string pack_evidence(std::string_view decision, std::string_view witnesses) {
  std::string text  = "{\"schema_version\":1,\"decision\":" + std::string(decision) + ",\"witnesses\":[";
  bool        first = true;
  while (!witnesses.empty()) {
    const auto end = witnesses.find('\n');
    if (end == std::string_view::npos || end == 0) {
      throw std::runtime_error("incomplete synthesis witness capture");
    }
    if (!first) {
      text += ',';
    }
    first  = false;
    text  += witnesses.substr(0, end);
    witnesses.remove_prefix(end + 1);
  }
  text += "]}";
  if (text.size() > (64ULL << 20)) {
    return {};  // bounded cache attachment admission; current report survives
  }
  if (!valid_evidence(text)) {
    throw std::runtime_error("inconsistent synthesis decision evidence");
  }
  return text;
}

bool valid_evidence(std::string_view evidence, std::string_view expected_region) {
  if (evidence.size() > (64ULL << 20)) {
    return false;
  }
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseFullPrecisionFlag>(evidence.data(), evidence.size());
  return validate(doc) && (expected_region.empty() || text_is(doc["decision"], "region", expected_region));
}

std::string replay_evidence(Witness_archive& archive, std::string_view region, std::string_view cached_region,
                            std::string_view evidence) {
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseFullPrecisionFlag>(evidence.data(), evidence.size());
  if (!validate(doc) || !text_is(doc["decision"], "region", cached_region)) {
    throw std::runtime_error("invalid synthesis decision evidence");
  }
  auto&                             alloc = doc.GetAllocator();
  std::map<int64_t, Witness_record> remap;
  for (const auto& record : doc["witnesses"].GetArray()) {
    remap.emplace(record["record"].GetInt64(), archive.replay(encode(record), region));
  }
  if (has(doc["decision"], "source_witness")) {
    auto& w = doc["decision"]["source_witness"];
    if (text_is(w, "status", "archived")) {
      const auto& mapped = remap.at(w["record"].GetInt64());
      w["record"].SetInt64(mapped.record);
      w["status"] = string_value(mapped.status, alloc);
    } else if (!text_is(w, "status", "not_constructed")) {
      archive.skip(w["status"].GetString());
    }
  }
  for (auto& attempt : doc["decision"]["attempts"].GetArray()) {
    auto& w = attempt["witness"];
    if (text_is(w, "status", "archived")) {
      const auto& new_record = remap.at(w["record"].GetInt64());
      w["record"].SetInt64(new_record.record);
      w["status"] = string_value(new_record.status, alloc);
    } else if (!text_is(w, "status", "not_constructed")) {
      archive.skip(w["status"].GetString());
    }
  }
  return "{\"region\":\"" + json_util::escape(region) + "\",\"cached_region\":\"" + json_util::escape(cached_region)
         + "\",\"metrics_scope\":\"historical_search\",\"decision\":" + encode(doc["decision"]) + "}";
}
}  // namespace livehd::synth
