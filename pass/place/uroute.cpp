// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "uroute.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <stdexcept>

#include "dreamplace/native/placement.hpp"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace {
namespace dp = DreamPlace::native;
namespace fs = std::filesystem;
using Json   = rapidjson::Value;
// Input size limits, mirrored by the offline tools.
constexpr std::uintmax_t map_limit = 8ULL << 20, manifest_limit = 64ULL << 20, weights_limit = 256ULL << 20, lef_limit = 1ULL << 30;
std::string digest(llvm::SHA256& hash) {
  constexpr char hex[] = "0123456789abcdef";
  std::string    out;
  for (auto byte : hash.final()) {
    out += hex[byte >> 4];
    out += hex[byte & 15];
  }
  return out;
}
std::string sha(std::string_view text) {
  llvm::SHA256 hash;
  hash.update(llvm::StringRef(text.data(), text.size()));
  return digest(hash);
}
std::string file_sha(const fs::path& path, std::uintmax_t limit) {
  if (!fs::is_regular_file(path) || fs::file_size(path) > limit) {
    throw std::runtime_error("missing or oversized URoute file: " + path.string());
  }
  std::ifstream           in(path, std::ios::binary);
  llvm::SHA256            hash;
  std::array<char, 65536> buffer;
  while (in.read(buffer.data(), buffer.size()) || in.gcount()) {
    hash.update(llvm::StringRef(buffer.data(), in.gcount()));
  }
  if (!in.eof()) {
    throw std::runtime_error("cannot read URoute file: " + path.string());
  }
  return digest(hash);
}
const Json& field(const Json& object, const char* key) {
  if (!object.IsObject() || !object.HasMember(key)) {
    throw std::runtime_error(std::string("missing URoute manifest field: ") + key);
  }
  return object[key];
}
std::string text(const Json& object, const char* key) {
  const auto& v = field(object, key);
  if (!v.IsString() || std::string_view(v.GetString(), v.GetStringLength()).find('\0') != std::string_view::npos) {
    throw std::runtime_error(std::string("invalid URoute string: ") + key);
  }
  return {v.GetString(), v.GetStringLength()};
}
double number(const Json& object, const char* key) {
  const auto& v = field(object, key);
  if (!v.IsNumber() || !std::isfinite(v.GetDouble())) {
    throw std::runtime_error(std::string("invalid URoute number: ") + key);
  }
  return v.GetDouble();
}
void unique_members(const Json& root) {
  std::vector<const Json*> pending{&root};
  while (!pending.empty()) {
    const auto& value = *pending.back();
    pending.pop_back();
    if (value.IsObject()) {
      std::set<std::string> names;
      for (const auto& member : value.GetObject()) {
        if (!names.emplace(member.name.GetString(), member.name.GetStringLength()).second) {
          throw std::runtime_error("duplicate URoute manifest field");
        }
        pending.push_back(&member.value);
      }
    } else if (value.IsArray()) {
      for (const auto& element : value.GetArray()) {
        pending.push_back(&element);
      }
    }
  }
}
bool versions(const Json& object) {
  for (const auto* name : {"feature_version", "label_version", "arch_version"}) {
    const auto& v = field(object, name);
    if (!v.IsUint() || v.GetUint() != 1) {
      return false;
    }
  }
  return true;
}
fs::path inside(const fs::path& root, const std::string& relative) {
  const fs::path part(relative);
  if (part.empty() || part.is_absolute()) {
    throw std::runtime_error("URoute paths must be relative to the data directory");
  }
  const auto base = fs::weakly_canonical(root), path = fs::weakly_canonical(root / part);
  auto       a = base.begin(), b = path.begin();
  for (; a != base.end(); ++a, ++b) {
    if (b == path.end() || *a != *b) {
      throw std::runtime_error("URoute path escapes the data directory");
    }
  }
  if (b == path.end()) {
    throw std::runtime_error("URoute file path names the data directory");
  }
  return path;
}
Uroute_identity identity(const Json& record) {
  Uroute_identity out;
  out.top             = text(record, "top");
  out.technology      = text(record, "technology");
  const auto& fp      = field(record, "floorplan");
  out.die_width       = number(fp, "die_width");
  out.die_height      = number(fp, "die_height");
  out.utilization     = number(fp, "utilization");
  out.macro_signature = text(fp, "macro_signature");
  if (out.top.empty() || out.technology.empty() || out.die_width <= 0 || out.die_height <= 0 || out.utilization < 0
      || out.utilization > 1) {
    throw std::runtime_error("invalid URoute sample identity");
  }
  return out;
}
double floorplan_distance(const Uroute_identity& a, const Uroute_identity& b) {
  return std::abs(a.die_width - b.die_width) / std::max(a.die_width, 1e-20)
         + std::abs(a.die_height - b.die_height) / std::max(a.die_height, 1e-20) + std::abs(a.utilization - b.utilization)
         + (a.macro_signature == b.macro_signature ? 0 : 1);
}
void check_features(const dp::UrouteMaps& m) {
  if (m.channels != 4 || !m.normalized || m.bins_x != 256 || m.bins_y != 256 || m.values.size() != 4 * 256 * 256) {
    throw std::runtime_error("URoute requires normalized four-channel 256x256 features");
  }
  for (float v : m.values) {
    if (!std::isfinite(v) || v < 0 || v > 1) {
      throw std::runtime_error("invalid URoute feature value");
    }
  }
}
}  // namespace
struct Uroute_pool::Data {
  struct Sample {
    std::string     id;
    Uroute_identity identity;
    std::uint64_t   created = 0;
    fs::path        features, labels;
  };
  Uroute_options                           options;
  std::string                              identity, skipped, manifest_sha256, weights_sha256;
  std::shared_ptr<const dp::UrouteWeights> weights;
  std::vector<Sample>                      samples;
};
std::string uroute_digest(std::string_view value) { return sha(value); }
std::string uroute_technology(const std::vector<std::string>& lefs, std::string_view generated_lef) {
  std::vector<std::string> hashes;
  for (const auto& path : lefs) {
    hashes.push_back(file_sha(path, lef_limit));
  }
  if (!generated_lef.empty()) {
    hashes.push_back(sha(generated_lef));
  }
  std::sort(hashes.begin(), hashes.end());
  std::string identity;
  for (const auto& hash : hashes) {
    if (!identity.empty()) {
      identity += ':';
    }
    identity += hash;
  }
  return "lef:" + sha(identity);
}
void validate_uroute_options(const Uroute_options& o) {
  if (o.shots < 1 || o.shots > 16 || o.min_shots < 1 || o.min_shots > o.shots || (o.fallback != "nearest" && o.fallback != "skip")
      || !std::isfinite(o.drc_level) || o.drc_level < 0) {
    throw std::invalid_argument("URoute expects shots 1..16, min_shots 1..shots, fallback nearest/skip and nonnegative drc_level");
  }
}
Uroute_pool::Uroute_pool(Uroute_options options) {
  validate_uroute_options(options);
  auto d        = std::make_shared<Data>();
  d->options    = std::move(options);
  data_         = d;
  const auto& o = d->options;
  d->identity   = sha("uroute-v1:" + std::to_string(o.shots) + ":" + std::to_string(o.min_shots) + ":" + o.fallback + ":"
                      + std::to_string(std::bit_cast<std::uint64_t>(o.drc_level)));
  if (o.dir.empty()) {
    d->skipped = "disabled (empty uroute.dir)";
    return;
  }
  try {
    const fs::path root(o.dir), manifest = root / "manifest.json";
    if (!fs::is_regular_file(manifest)) {
      d->skipped = "manifest.json is missing";
      return;
    }
    if (fs::file_size(manifest) > manifest_limit) {
      throw std::runtime_error("oversized URoute manifest");
    }
    std::ifstream     in(manifest, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.eof() && !in) {
      throw std::runtime_error("cannot read URoute manifest");
    }
    d->manifest_sha256 = sha(bytes);
    d->identity        = sha(d->identity + ":" + d->manifest_sha256);
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(bytes.data(), bytes.size());
    if (doc.HasParseError() || !doc.IsObject()) {
      throw std::runtime_error("invalid URoute manifest JSON");
    }
    unique_members(doc);
    if (!versions(doc)) {
      throw std::runtime_error("unsupported URoute feature/label/architecture version");
    }
    const auto& samples = field(doc, "samples");
    if (!samples.IsArray() || samples.Size() > 100000) {
      throw std::runtime_error("invalid or oversized URoute sample pool");
    }
    std::set<std::string> ids;
    for (const auto& record : samples.GetArray()) {
      const auto id = text(record, "id");
      if (id.empty() || !ids.insert(id).second) {
        throw std::runtime_error("empty/duplicate URoute sample id");
      }
      if (!versions(record) || text(record, "status") != "routed") {
        continue;
      }
      Data::Sample sample;
      sample.id           = id;
      sample.identity     = identity(record);
      const auto& created = field(record, "created_at");
      if (!created.IsUint64()) {
        throw std::runtime_error("URoute created_at must be an integer timestamp");
      }
      sample.created       = created.GetUint64();
      const auto directory = inside(root, text(record, "directory"));
      const auto features = inside(directory, "features.bin"), labels = inside(directory, "labels.bin");
      const auto fh = file_sha(features, map_limit), lh = file_sha(labels, map_limit);
      if (fh != text(record, "features_sha256") || lh != text(record, "labels_sha256")) {
        throw std::runtime_error("URoute sample digest mismatch: " + id);
      }
      sample.features = features;
      sample.labels   = labels;
      check_features(dp::read_uroute_maps(features));
      const auto label_maps = dp::read_uroute_maps(labels);
      if (label_maps.channels != 3 || label_maps.normalized || label_maps.bins_x != 256 || label_maps.bins_y != 256) {
        throw std::runtime_error("URoute labels must contain three raw 256x256 maps");
      }
      d->samples.push_back(std::move(sample));
    }
    if (d->samples.empty()) {
      d->skipped = "no compatible routed support samples";
      return;
    }
    if (o.fallback == "skip" && size_t(o.shots) > d->samples.size()) {
      d->skipped = "shots exceeds the compatible pool with fallback=skip";
      return;
    }
    const auto& weights = field(doc, "weights");
    if (weights.IsNull()) {
      d->skipped = "manifest has no trained weights";
      return;
    }
    const auto path   = inside(root, text(weights, "file"));
    const auto hash   = file_sha(path, weights_limit);
    d->weights_sha256 = hash;
    d->identity       = sha(d->identity + ":" + hash);
    if (hash != text(weights, "sha256")) {
      throw std::runtime_error("URoute weight digest mismatch");
    }
    d->weights = std::make_shared<const dp::UrouteWeights>(path);
  } catch (const std::exception& e) {
    d->skipped = e.what();
    d->weights.reset();
    d->samples.clear();
  }
}
void Uroute_selection::apply(dp::Options& options) const {
  options.uroute          = context;
  options.uroute_identity = cache_identity;
  options.uroute_skipped  = skipped;
}
const std::string& Uroute_pool::cache_identity() const { return data_->identity; }
const std::string& Uroute_pool::skipped() const { return data_->skipped; }
const Uroute_options& Uroute_pool::options() const { return data_->options; }
Uroute_selection   Uroute_pool::select(const dp::UrouteMaps& query, const Uroute_identity& id) const {
  const auto&      d = *data_;
  Uroute_selection result;
  result.skipped         = d.skipped;
  result.manifest_sha256 = d.manifest_sha256;
  result.weights_sha256  = d.weights_sha256;
  result.cache_identity  = d.identity;
  if (!result.skipped.empty()) {
    return result;
  }
  try {
    check_features(query);
    std::vector<size_t> selected;
    for (size_t i = 0; i < d.samples.size(); ++i) {
      if (d.samples[i].identity.top == id.top && d.samples[i].identity.technology == id.technology) {
        selected.push_back(i);
      }
    }
    std::sort(selected.begin(), selected.end(), [&](size_t a, size_t b) {
      const auto& x = d.samples[a];
      const auto& y = d.samples[b];
      if (x.created != y.created) {
        return x.created > y.created;
      }
      const auto xd = floorplan_distance(id, x.identity), yd = floorplan_distance(id, y.identity);
      return xd != yd ? xd < yd : x.id < y.id;
    });
    if (selected.size() < size_t(d.options.min_shots)) {
      if (d.options.fallback == "skip") {
        result.skipped = "insufficient same-design support samples";
        return result;
      }
      struct Distance {
        size_t index;
        bool   other_tech;
        double distance;
      };
      std::vector<Distance> nearest;
      for (size_t j = 0; j < d.samples.size(); ++j) {
        double     distance  = 0;
        const auto candidate = dp::read_uroute_maps(d.samples[j].features);
        for (size_t i = 0; i < query.values.size(); ++i) {
          // Round each square before accumulating, as the binary64 offline
          // evaluator does; FMA contraction can reorder near-tied samples.
          const double          delta   = double(query.values[i]) - candidate.values[i];
          const volatile double square  = delta * delta;
          distance                     += square;
        }
        nearest.push_back({j, d.samples[j].identity.technology != id.technology, distance});
      }
      std::sort(nearest.begin(), nearest.end(), [&](const auto& a, const auto& b) {
        if (a.other_tech != b.other_tech) {
          return !a.other_tech;
        }
        if (a.distance != b.distance) {
          return a.distance < b.distance;
        }
        return d.samples[a.index].id < d.samples[b.index].id;
      });
      selected.clear();
      for (const auto& candidate : nearest) {
        selected.push_back(candidate.index);
      }
    }
    selected.resize(std::min(selected.size(), size_t(d.options.shots)));
    std::vector<dp::UrouteSupport> support;
    std::string                    selection_hash = d.identity;
    for (size_t i : selected) {
      const auto& sample = d.samples[i];
      support.push_back({sample.id, dp::read_uroute_maps(sample.features), dp::read_uroute_maps(sample.labels)});
      result.sample_ids.push_back(sample.id);
      selection_hash += ":" + sample.id;
    }
    result.cache_identity = sha(selection_hash);
    result.context        = std::make_shared<const dp::UrouteContext>(d.weights, std::move(support), d.options.drc_level);
  } catch (const std::exception& e) {
    result.skipped = e.what();
    result.context.reset();
    result.sample_ids.clear();
  }
  return result;
}

std::function<void(const dp::Design&, int, int)> uroute_probe_exporter(fs::path                         directory,
                                                                       std::function<Uroute_identity()> identity, double capacity_h,
                                                                       double capacity_v) {
  return [directory = std::move(directory), identity = std::move(identity), capacity_h, capacity_v](const dp::Design& physical,
                                                                                                    int               nx,
                                                                                                    int               ny) {
    export_uroute(directory, physical, identity(), nx, ny, capacity_h, capacity_v);
  };
}

void add_uroute_status(rapidjson::Value& object, rapidjson::Document::AllocatorType& allocator, const std::string& skipped,
                       const std::vector<std::string>& sample_ids) {
  object.AddMember("uroute_skipped", rapidjson::Value(skipped.c_str(), allocator), allocator);
  rapidjson::Value ids(rapidjson::kArrayType);
  for (const auto& id : sample_ids) {
    ids.PushBack(rapidjson::Value(id.c_str(), allocator), allocator);
  }
  object.AddMember("uroute_sample_ids", ids, allocator);
}

void export_uroute(const fs::path& directory, const dp::Design& design, const Uroute_identity& id, int nx, int ny,
                   double capacity_h, double capacity_v) {
  if (!std::isfinite(capacity_h) || !std::isfinite(capacity_v) || capacity_h < 0 || capacity_v < 0
      || !std::isfinite(id.dbu_per_micron) || id.dbu_per_micron <= 0) {
    throw std::invalid_argument("invalid URoute export units/capacity");
  }
  const auto maps = dp::uroute_features(design, nx, ny);
  fs::create_directories(directory);
  const auto map_path = directory / "features.bin";
  dp::write_uroute_maps(directory / "features.bin.tmp", maps);
  fs::rename(directory / "features.bin.tmp", map_path);
  llvm::SHA256 placement;
  auto         word = [&](std::uint64_t value) {
    std::array<char, 8> bytes;
    for (size_t j = 0; j < 8; ++j) {
      bytes[j] = static_cast<char>((value >> (8 * j)) & 255);
    }
    placement.update(llvm::StringRef(bytes.data(), bytes.size()));
  };
  for (double v : {design.layout.xl, design.layout.yl, design.layout.xh, design.layout.yh}) {
    word(std::bit_cast<std::uint64_t>(v));
  }
  word(design.node_x.size());
  for (size_t i = 0; i < design.node_x.size(); ++i) {
    for (double v : {design.node_x[i], design.node_y[i], design.node_size_x[i], design.node_size_y[i]}) {
      word(std::bit_cast<std::uint64_t>(v));
    }
    word(design.node_macro.empty() ? 0 : design.node_macro[i]);
    word(static_cast<unsigned>(design.placement_mask(i)));
  }
  for (size_t n = 0; n < design.net_weights.size(); ++n) {
    word(design.net_mask[n]);
    word(design.flat_net2pin_start[n + 1] - design.flat_net2pin_start[n]);
    for (int j = design.flat_net2pin_start[n]; j < design.flat_net2pin_start[n + 1]; ++j) {
      const auto p = design.flat_net2pin[j], node = design.pin2node[p];
      word(std::bit_cast<std::uint64_t>(design.node_x[node] + design.pin_offset_x[p]));
      word(std::bit_cast<std::uint64_t>(design.node_y[node] + design.pin_offset_y[p]));
    }
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
  out.StartObject();
  for (const auto* key : {"feature_version", "label_version", "arch_version"}) {
    out.Key(key);
    out.Int(1);
  }
  out.Key("top");
  out.String(id.top.c_str());
  out.Key("technology");
  out.String(id.technology.c_str());
  out.Key("placement_identity");
  out.String(digest(placement).c_str());
  out.Key("features_sha256");
  out.String(file_sha(map_path, map_limit).c_str());
  out.Key("core");
  out.StartArray();
  for (double v : {design.layout.xl, design.layout.yl, design.layout.xh, design.layout.yh}) {
    out.Double(v);
  }
  out.EndArray();
  out.Key("route_grid");
  out.StartArray();
  out.Int(nx);
  out.Int(ny);
  out.EndArray();
  out.Key("network_grid");
  out.StartArray();
  out.Int(256);
  out.Int(256);
  out.EndArray();
  out.Key("capacity_h");
  out.Double(capacity_h);
  out.Key("capacity_v");
  out.Double(capacity_v);
  out.Key("dbu_per_micron");
  out.Double(id.dbu_per_micron);
  out.Key("floorplan");
  out.StartObject();
  out.Key("die_width");
  out.Double(id.die_width);
  out.Key("die_height");
  out.Double(id.die_height);
  out.Key("utilization");
  out.Double(id.utilization);
  out.Key("macro_signature");
  out.String(id.macro_signature.c_str());
  out.EndObject();
  out.EndObject();
  std::ofstream metadata(directory / "metadata.json.tmp", std::ios::binary);
  metadata << buffer.GetString() << '\n';
  metadata.close();
  if (!metadata) {
    throw std::runtime_error("cannot write URoute export metadata");
  }
  fs::rename(directory / "metadata.json.tmp", directory / "metadata.json");
}
}  // namespace livehd::placement
