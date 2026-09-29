// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "evidence.hpp"

#include <stdexcept>

#include "json_util.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::usyn {
namespace {
constexpr size_t kMaxEvidenceBytes = 64ULL << 20;

std::string encode(const rapidjson::Value& value) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return {buffer.GetString(), buffer.GetSize()};
}

// The decision object with its region, or null.
const rapidjson::Value* decision_of(const rapidjson::Document& doc, std::string_view expected_region) {
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("schema_version") || !doc["schema_version"].IsInt()
      || doc["schema_version"].GetInt() != 2 || !doc.HasMember("decision")) {
    return nullptr;
  }
  const auto& decision = doc["decision"];
  if (!decision.IsObject() || !decision.HasMember("region") || !decision["region"].IsString()) {
    return nullptr;
  }
  if (!expected_region.empty()
      && std::string_view{decision["region"].GetString(), decision["region"].GetStringLength()} != expected_region) {
    return nullptr;
  }
  return &decision;
}
}  // namespace

std::string pack_evidence(std::string_view decision) {
  auto text = "{\"schema_version\":2,\"decision\":" + std::string(decision) + "}";
  if (text.size() > kMaxEvidenceBytes) {
    return {};  // bounded cache attachment admission; the current report survives
  }
  if (!valid_evidence(text)) {
    throw std::runtime_error("inconsistent synthesis decision evidence");
  }
  return text;
}

bool valid_evidence(std::string_view evidence, std::string_view expected_region) {
  if (evidence.size() > kMaxEvidenceBytes) {
    return false;
  }
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseFullPrecisionFlag>(evidence.data(), evidence.size());
  return decision_of(doc, expected_region) != nullptr;
}

std::string replay_evidence(std::string_view region, std::string_view cached_region, std::string_view evidence) {
  rapidjson::Document doc;
  doc.Parse<rapidjson::kParseFullPrecisionFlag>(evidence.data(), evidence.size());
  const auto* decision = decision_of(doc, cached_region);
  if (decision == nullptr) {
    throw std::runtime_error("invalid synthesis decision evidence");
  }
  return "{\"region\":\"" + json_util::escape(region) + "\",\"cached_region\":\"" + json_util::escape(cached_region)
         + "\",\"metrics_scope\":\"historical_search\",\"decision\":" + encode(*decision) + "}";
}
}  // namespace livehd::usyn
