// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "place.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "memory_proxy.hpp"
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "attrs.hpp"
#include "dreamplace/native/placement.hpp"
#include "hash_util.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
#include "pass/place/place_salt.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rules_cc/cc/runfiles/runfiles.h"
#include "thirdparty/flute/flute.hpp"

namespace livehd::placement {
namespace {
namespace ph      = physical;
namespace gu      = graph_util;
namespace dp      = DreamPlace::native;
namespace fs      = std::filesystem;
using Node        = hhds::Node_class;
using Pin         = hhds::Pin_class;
using Orientation = ph::Orientation;
using Status      = ph::Status;
// Fully assigned combinational Verilog regs can retain a Latch declaration
// after mapping. Like cgen's always_comb emission, an always-open latch with
// no reset is just its data value. Normalize only the private physical graph.
void remove_transparent_latches(hhds::Graph& graph) {
  std::vector<Node> aliases;
  for (auto node : graph.body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Latch || !gu::get_driver_of_sink_name(node, "reset_pin").is_invalid()
        || !gu::get_driver_of_sink_name(node, "clock_pin").is_invalid()) {
      continue;
    }
    const auto enable = gu::get_driver_of_sink_name(node, "enable");
    if (!enable.is_invalid()) {
      const auto polarity = gu::get_driver_of_sink_name(node, "posclk");
      if (!enable.is_const() || gu::const_of(enable).has_unknowns() || (!polarity.is_invalid() && !polarity.is_const())
          || (polarity.is_const() && gu::const_of(polarity).has_unknowns())) {
        continue;
      }
      const bool active_low = polarity.is_known_false();
      if (active_low != gu::const_of(enable).is_known_zero()) {
        continue;
      }
    }
    const auto din = gu::get_driver_of_sink_name(node, "din");
    const auto q   = node.try_get_driver_pin(0);
    if (!din.is_invalid() && !q.is_invalid() && din != q && gu::bits_of(din) == gu::bits_of(q)) {
      aliases.push_back(node);
    }
  }
  for (auto node : aliases) {
    const auto din = gu::get_driver_of_sink_name(node, "din");
    const auto q   = node.get_driver_pin(0);
    if (din == q) {
      throw std::runtime_error("cycle in always-transparent latch aliases");
    }
    std::vector<Pin> sinks;
    for (const auto& edge : q.out_edges()) {
      sinks.push_back(edge.sink);
    }
    for (auto sink : sinks) {
      sink.del_sink(q);
      sink.connect_driver(din);
    }
    node.del_node();
  }
}
uint64_t    hash_text(std::string_view text, uint64_t hash = hash_util::kFnv1a64_offset) { return hash_util::fnv1a64(text, hash); }
std::string file_text(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read " + path);
  }
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string environment_key(const Options& o, const ph::Technology& tech) {
  uint64_t h = kPlaceSalt;
  for (const auto& path : o.lefs) {
    h = hash_text(file_text(path), h);
  }
  if (!o.floorplan.empty()) {
    h = hash_text(file_text(o.floorplan), h);
  }
  h = hash_text(
      std::format("{}:{}:{}:{}:{}:{}:{}:{}", o.utilization, o.aspect, o.density, o.full, o.iterations, o.bins, o.threads, tech.dbu),
      h);
  for (const auto& [name, weight] : o.net_weights) {
    h = hash_text(std::format("{}:{}", name, weight), h);
  }
  return std::format("{:016x}", h);
}
std::string pin_name(Pin pin) {
  auto n = pin.get_master_node();
  if (auto io = n.get_subnode_io()) {
    const auto& decls = pin.is_driver() ? io->get_output_pin_decls() : io->get_input_pin_decls();
    for (const auto& d : decls) {
      if (d.port_id == pin.get_port_id()) {
        return d.name;
      }
    }
  }
  return std::string(pin.get_pin_name());
}
std::string bit_name(const std::string& name, int bits, int bit) { return bits > 1 ? std::format("{}[{}]", name, bit) : name; }
struct Endpoint {
  std::string instance, pin;
  int         constant                                     = -1;
  friend bool operator==(const Endpoint&, const Endpoint&) = default;
  friend bool operator<(const Endpoint& a, const Endpoint& b) {
    return std::tie(a.constant, a.instance, a.pin) < std::tie(b.constant, b.instance, b.pin);
  }
};
// Lower wiring by demand. The placed graph retains its bus interfaces and pure
// wiring nodes; only physical endpoints enter the placer's array model.
class Resolver {
  std::map<std::pair<uint64_t, int>, Endpoint> memo;
  std::set<std::pair<uint64_t, int>>           busy;
  const ph::Technology&                        tech;
  const std::map<uint64_t, std::string>&       memories;

public:
  explicit Resolver(const ph::Technology& t, const std::map<uint64_t, std::string>& m) : tech(t), memories(m) {}
  Endpoint bit(Pin p, int b, int depth = 0) {
    if (p.is_invalid() || b < 0) {
      throw std::runtime_error("invalid physical wiring operand");
    }
    if (depth > 4096) {
      throw std::runtime_error("physical wiring depth exceeds 4096");
    }
    if (p.is_const()) {
      const auto& c = gu::const_of(p);
      if (c.unknown_bit_test(b)) {
        throw std::runtime_error("physical net has an unknown constant");
      }
      return {{}, {}, c.bit_test(b) ? 1 : 0};
    }
    auto key = std::pair{static_cast<uint64_t>(p.get_debug_pid()), b};
    if (auto it = memo.find(key); it != memo.end()) {
      return it->second;
    }
    if (!busy.insert(key).second) {
      throw std::runtime_error("cycle in physical wiring aliases");
    }
    auto     n     = p.get_master_node();
    auto     input = [&](std::string_view name, int which) { return bit(gu::get_driver_of_sink_name(n, name), which, depth + 1); };
    Endpoint out;
    if (n.is_input_node()) {
      const auto io    = n.get_graph()->get_io();
      const auto name  = std::string(p.get_pin_name());
      const auto width = std::max(1, static_cast<int>(io->get_bits(name)));
      if (b >= width) {
        out = gu::is_unsign(p) ? Endpoint{{}, {}, 0} : Endpoint{"PIN", bit_name(name, width, width - 1), -1};
      } else {
        out = {"PIN", bit_name(name, width, b), -1};
      }
    } else if (memories.contains(n.get_debug_nid())) {
      // The proxy only has pins below the read width; extend past it like the
      // generic branch (a signed pin repeats its MSB).
      const int width = std::max(1, gu::bits_of(p));
      if (b >= width) {
        out = gu::is_unsign(p) ? Endpoint{{}, {}, 0} : bit(p, width - 1, depth + 1);
      } else {
        out = {std::string(n.get_name()), memory_pin(p.get_port_id(), b, true), -1};
      }
    } else if (auto io = n.get_subnode_io(); io && tech.macros.contains(std::string(io->get_name()))) {
      const auto& macro = tech.macros.at(std::string(io->get_name()));
      auto        name  = pin_name(p);
      if (macro.pins.contains(name)) {
        if (b == 0) {
          out = {std::string(n.get_name()), name, -1};
        } else {
          out.constant = 0;  // scalar physical cell outputs are zero-extended
        }
      } else {
        name = std::format("{}[{}]", name, b);
        if (!macro.pins.contains(name)) {
          throw std::runtime_error("LEF output pin not found: " + std::string(io->get_name()) + "/" + name);
        }
        out = {std::string(n.get_name()), name, -1};
      }
    } else {
      const auto op    = gu::type_op_of(n);
      const int  width = std::max(1, gu::bits_of(p));
      if (b >= width) {
        // Bits past the declared width extend the value; a signed pin repeats its
        // MSB (the op-specific indexing below would read an unrelated operand bit).
        out = gu::is_unsign(p) ? Endpoint{{}, {}, 0} : bit(p, width - 1, depth + 1);
      } else if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
        const auto range = gu::bit_range(n);
        if (!range) {
          throw std::runtime_error("nonconstant physical bit range");
        }
        if (op == Ntype_op::Get_mask) {
          out = input("a", b + range->first);
        } else {
          out = b >= range->first && b < range->second ? input("value", b - range->first) : input("a", b);
        }
      } else if (op == Ntype_op::Concat) {
        const auto lanes = gu::concat_lanes(n);
        if (lanes.empty()) {
          throw std::runtime_error("malformed physical concat");
        }
        out.constant = 0;
        for (const auto& lane : lanes) {
          if (b >= lane.offset && b < lane.offset + lane.width) {
            out = bit(lane.value, b - lane.offset, depth + 1);
          }
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        out.constant = op == Ntype_op::Or ? 0 : 1;
        std::vector<Pin> operands;
        for (auto sink : n.inp_sorted_pins()) {
          for (auto driver : sink.get_driver_pins()) {
            operands.push_back(driver);
          }
        }
        for (auto operand : operands) {
          const auto v = bit(operand, b, depth + 1);
          if (v.constant == (op == Ntype_op::Or ? 1 : 0)) {
            out = v;
            break;
          }
          if (v.constant >= 0) {
            continue;
          }
          if (out.constant < 0 && out != v) {
            throw std::runtime_error("unmapped Boolean operation in physical netlist");
          }
          out = v;
        }
      } else if (op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::Sext) {
        const auto value  = gu::get_driver_of_sink_name(n, "a");
        const auto amount = gu::get_driver_of_sink_name(n, "b");
        if (!amount.is_const() || !gu::const_of(amount).is_just_i64()) {
          throw std::runtime_error("unmapped shift in physical netlist");
        }
        const auto shift = gu::const_of(amount).to_just_i64();
        if (shift < 0 || shift > std::numeric_limits<int>::max() - b) {
          throw std::runtime_error("invalid physical shift");
        }
        if (op == Ntype_op::SHL) {
          out = b < shift ? Endpoint{{}, {}, 0} : bit(value, b - static_cast<int>(shift), depth + 1);
        } else if (op == Ntype_op::SRA) {
          out = bit(value, b + static_cast<int>(shift), depth + 1);
        } else {
          out = bit(value, std::min(b, static_cast<int>(shift)), depth + 1);
        }
      } else {
        throw std::runtime_error("physical placement requires mapped cells; found " + std::string(Ntype::get_name(op)) + " '"
                                 + std::string(n.get_name()) + "'");
      }
    }
    busy.erase(key);
    memo.emplace(key, out);
    return out;
  }
};
struct Cell {
  Node        node;
  std::string name, macro, block;
  Ann_place   place;
  bool        locked = false;
};
struct View {
  dp::Design                                design;
  ph::Floorplan                             fp;
  std::vector<Cell>                         cells;
  std::vector<Endpoint>                     pins;
  std::vector<Endpoint>                     drivers;
  std::map<Endpoint, std::vector<Endpoint>> nets;
  std::map<std::string, size_t>             terminal;
  std::map<std::string, size_t>             cell;
  std::map<std::string, double>             weights;
};
std::pair<double, double> offset(const ph::Macro& macro, const std::string& name, Orientation o, int64_t dbu) {
  auto it = macro.pins.find(name);
  if (it == macro.pins.end() || it->second.shapes.empty()) {
    throw std::runtime_error("missing LEF pin geometry: " + name);
  }
  const auto& r = it->second.shapes.front().rect;
  auto [x, y]
      = ph::orient_point((r.xl + r.xh) / 2 + macro.origin_x, (r.yl + r.yh) / 2 + macro.origin_y, macro.width, macro.height, o);
  return {x * dbu, y * dbu};
}
void make_arrays(View& v, const ph::Technology& tech) {
  v.design         = {};
  auto&       d    = v.design;
  const auto& rows = v.fp.rows;
  // Integer DBU row pitch, as validate_rows and check_legality use.
  const auto  rh   = std::llround(tech.sites.at(rows.front().site).height * tech.dbu);
  d.layout         = {double(rows.front().x),
                      double(rows.front().y),
                      double(rows.front().x + rows.front().count * rows.front().step),
                      double(rows.back().y + rh),
                      double(rows.front().step),
                      double(rh)};
  std::stable_sort(v.cells.begin(), v.cells.end(), [](const auto& a, const auto& b) {
    const auto movable
        = [](const auto& c) { return !c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED; };
    if (movable(a) != movable(b)) {
      return movable(a);
    }
    return a.name < b.name;
  });
  v.cell.clear();
  for (const auto& c : v.cells) {
    if (!v.cell.emplace(c.name, d.node_x.size()).second) {
      throw std::runtime_error("duplicate physical instance name: " + c.name);
    }
    d.node_x.push_back(c.place.x);
    d.node_y.push_back(c.place.y);
    d.node_size_x.push_back(c.place.width);
    d.node_size_y.push_back(c.place.height);
    if (!c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
      ++d.num_movable;
    }
  }
  for (size_t i = 0; i < v.fp.terminals.size(); ++i) {
    const auto& p      = v.fp.terminals[i];
    v.terminal[p.name] = i;
    d.node_x.push_back(p.x);
    d.node_y.push_back(p.y);
    d.node_size_x.push_back(0);
    d.node_size_y.push_back(0);
  }
  v.pins.clear();
  v.drivers.clear();
  for (const auto& [driver, sinks] : v.nets) {
    v.drivers.push_back(driver);
    auto append = [&](const Endpoint& e) {
      v.pins.push_back(e);
      d.flat_net2pin.push_back(d.pin2node.size());
      if (e.instance == "PIN") {
        d.pin2node.push_back(v.cells.size() + v.terminal.at(e.pin));
        d.pin_offset_x.push_back(0);
        d.pin_offset_y.push_back(0);
      } else {
        const auto  index = v.cell.at(e.instance);
        const auto& c     = v.cells[index];
        const auto [x, y] = offset(tech.macros.at(c.macro), e.pin, c.place.orientation, tech.dbu);
        d.pin2node.push_back(index);
        d.pin_offset_x.push_back(x);
        d.pin_offset_y.push_back(y);
      }
    };
    append(driver);
    for (const auto& sink : sinks) {
      append(sink);
    }
    d.flat_net2pin_start.push_back(d.pin2node.size());
    const auto key    = driver.instance == "PIN" ? driver.pin : driver.instance + ":" + driver.pin;
    const auto weight = v.weights.contains(key) ? v.weights.at(key) : 1;
    if (!std::isfinite(weight) || weight < 1 || weight > 100) {
      throw std::invalid_argument("invalid physical net weight");
    }
    d.net_weights.push_back(weight);
  }
  d.build_maps();
  d.validate();
}
void capture(View& v, const ph::Technology& tech, bool full) {
  for (size_t i = 0; i < v.cells.size(); ++i) {
    auto& c = v.cells[i];
    if (c.locked || c.place.status == Status::FIXED || c.place.status == Status::LOCKED) {
      continue;
    }
    c.place.x = std::llround(v.design.node_x[i]);
    c.place.y = std::llround(v.design.node_y[i]);
    if (full) {
      const auto row = static_cast<size_t>(std::llround((c.place.y - v.fp.rows.front().y) / v.design.layout.row_height));
      if (row >= v.fp.rows.size()) {
        throw std::runtime_error("placed cell outside rows");
      }
      c.place.orientation = v.fp.rows[row].orientation;
      if (c.place.orientation == Orientation::FS && !tech.macros.at(c.macro).symmetry_x) {
        throw std::runtime_error("cell cannot use flipped row: " + c.name);
      }
    }
    c.place.status = Status::PLACED;
  }
  // Recompute offsets after changing rows/orientations and round GP coordinates
  // consistently with the graph that STA and subsequent exports will read.
  make_arrays(v, tech);
}
void validate_rows(const ph::Technology& tech, ph::Floorplan& fp) {
  if (fp.dbu != tech.dbu || fp.rows.empty()) {
    throw std::runtime_error("DEF needs rows and the same DBU as LEF");
  }
  std::sort(fp.rows.begin(), fp.rows.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
  const auto& first = fp.rows.front();
  const auto  it    = tech.sites.find(first.site);
  if (it == tech.sites.end()) {
    throw std::runtime_error("DEF row SITE missing from LEF: " + first.site);
  }
  const auto sw = std::llround(it->second.width * tech.dbu), rh = std::llround(it->second.height * tech.dbu);
  for (size_t i = 0; i < fp.rows.size(); ++i) {
    const auto& r = fp.rows[i];
    if (r.site != first.site || r.count != first.count || r.x != first.x || r.step != sw
        || r.y != first.y + static_cast<int64_t>(i) * rh || (r.orientation != Orientation::N && r.orientation != Orientation::FS)) {
      throw std::runtime_error("floorplan requires contiguous equal-width N/FS rows; row gaps and rotated rows are unsupported");
    }
    if (r.x < fp.die.xl || r.y < fp.die.yl || r.x + r.count * r.step > fp.die.xh || r.y + rh > fp.die.yh) {
      throw std::runtime_error("DEF rows outside DIEAREA");
    }
  }
}
// Rectangular Hungarian assignment; cost is Manhattan distance to each net's
// current cell-pin centroid. Already fixed terminals reserve their slots. Slots
// lie on the DIEAREA boundary, which an imported floorplan may set outside the core.
void assign_io(View& v) {
  const auto&                            l   = v.design.layout;
  const auto&                            die = v.fp.die;
  std::vector<std::pair<double, double>> slots;
  const double                           pitch = std::min(l.site_width, l.row_height);
  for (double x = die.xl + pitch / 2; x < die.xh; x += pitch) {
    slots.emplace_back(x, die.yl);
    slots.emplace_back(x, die.yh);
  }
  for (double y = die.yl + pitch / 2; y < die.yh; y += pitch) {
    slots.emplace_back(die.xl, y);
    slots.emplace_back(die.xh, y);
  }
  std::erase_if(slots, [&](const auto& s) {
    for (const auto& t : v.fp.terminals) {
      if (t.status == Status::FIXED && std::abs(t.x - s.first) + std::abs(t.y - s.second) < pitch / 2) {
        return true;
      }
    }
    return false;
  });
  std::vector<size_t> movable;
  for (size_t i = 0; i < v.fp.terminals.size(); ++i) {
    if (v.fp.terminals[i].status != Status::FIXED) {
      movable.push_back(i);
    }
  }
  if (movable.empty()) {
    return;
  }
  if (slots.size() < movable.size()) {
    throw std::runtime_error("not enough die-edge slots for IO pins; enlarge the floorplan");
  }
  std::vector<std::pair<double, double>>       centers(v.fp.terminals.size(), {(l.xl + l.xh) / 2, (l.yl + l.yh) / 2});
  // One pass over the nets: accumulate every touching net's cell pins per terminal.
  std::vector<std::tuple<double, double, int>> sums(v.fp.terminals.size());
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    const int begin = v.design.flat_net2pin_start[net], end = v.design.flat_net2pin_start[net + 1];
    double    x = 0, y = 0;
    int       count = 0;
    for (int p = begin; p < end; ++p) {
      const int node = v.design.pin2node[p];
      if (node < static_cast<int>(v.cells.size())) {
        x += v.design.node_x[node] + v.design.pin_offset_x[p];
        y += v.design.node_y[node] + v.design.pin_offset_y[p];
        ++count;
      }
    }
    for (int p = begin; p < end; ++p) {
      const int node = v.design.pin2node[p];
      if (node >= static_cast<int>(v.cells.size())) {
        auto& [sx, sy, sc]  = sums[node - v.cells.size()];
        sx                 += x;
        sy                 += y;
        sc                 += count;
      }
    }
  }
  for (const auto terminal : movable) {
    if (const auto& [x, y, count] = sums[terminal]; count) {
      centers[terminal] = {x / count, y / count};
    }
  }
  const size_t        n = movable.size(), m = slots.size();
  std::vector<double> u(n + 1), w(m + 1);
  std::vector<size_t> p(m + 1), way(m + 1);
  for (size_t i = 1; i <= n; ++i) {
    p[0]                   = i;
    size_t              j0 = 0;
    std::vector<double> minv(m + 1, std::numeric_limits<double>::infinity());
    std::vector<bool>   used(m + 1);
    do {
      used[j0]  = true;
      size_t i0 = p[j0], j1 = 0;
      double delta = std::numeric_limits<double>::infinity();
      for (size_t j = 1; j <= m; ++j) {
        if (!used[j]) {
          auto [cx, cy] = centers[movable[i0 - 1]];
          double cur    = std::abs(cx - slots[j - 1].first) + std::abs(cy - slots[j - 1].second) - u[i0] - w[j];
          if (cur < minv[j]) {
            minv[j] = cur;
            way[j]  = j0;
          }
          if (minv[j] < delta) {
            delta = minv[j];
            j1    = j;
          }
        }
      }
      for (size_t j = 0; j <= m; ++j) {
        if (used[j]) {
          u[p[j]] += delta;
          w[j]    -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0]);
    do {
      size_t j1 = way[j0];
      p[j0]     = p[j1];
      j0        = j1;
    } while (j0);
  }
  for (size_t j = 1; j <= m; ++j) {
    if (p[j]) {
      auto& t  = v.fp.terminals[movable[p[j] - 1]];
      t.x      = std::llround(slots[j - 1].first);
      t.y      = std::llround(slots[j - 1].second);
      t.status = Status::PLACED;
    }
  }
}
std::unique_ptr<rules_cc::cc::runfiles::Runfiles> runfiles() {
  using rules_cc::cc::runfiles::Runfiles;
  std::string               error;
  std::unique_ptr<Runfiles> result(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
  if (result) {
    return result;
  }
  // Standard executable-adjacent Bazel runfiles. /proc is available on Linux;
  // macOS supplies _NSGetExecutablePath through the platform branch below.
  std::string executable;
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  executable.resize(size);
  _NSGetExecutablePath(executable.data(), &size);
  executable.resize(std::char_traits<char>::length(executable.c_str()));
#else
  std::error_code ec;
  executable = fs::read_symlink("/proc/self/exe", ec).string();
#endif
  result.reset(Runfiles::Create(executable, BAZEL_CURRENT_REPOSITORY, &error));
  if (!result) {
    throw std::runtime_error("cannot locate FLUTE runfiles: " + error);
  }
  return result;
}
void init_flute() {
  static std::once_flag once;
  std::call_once(once, [] {
    auto       files  = runfiles();
    const auto locate = [&](const char* name) {
      // Resolve the apparent archive name through this module's repository mapping.
      auto path = files->Rlocation(std::string("dreamplace/thirdparty/flute/lut.ICCAD2015/") + name);
      if (!fs::is_regular_file(path)) {
        throw std::runtime_error("FLUTE lookup table missing from runfiles: " + path);
      }
      return path;
    };
    const auto powv = locate("POWV9.dat"), post = locate("POST9.dat");
    flute::readLUT(powv.c_str(), post.c_str());
  });
}
std::shared_ptr<ph::Parasitics> estimate_rc(const View& v, const ph::Technology& tech, const Options& options,
                                            std::map<std::string, std::pair<double, double>>& pin_locations) {
  auto result         = std::make_shared<ph::Parasitics>();
  result->approximate = !options.full;
  double horizontal_r = options.resistance_ohm_um, horizontal_c = options.capacitance_ff_um;
  double vertical_r = horizontal_r, vertical_c = horizontal_c;
  if (horizontal_r < 0 || horizontal_c < 0) {
    const ph::Layer* horizontal = nullptr;
    const ph::Layer* vertical   = nullptr;
    for (const auto& name : tech.routing_layers) {
      const auto& layer = tech.layers.at(name);
      if (!(layer.width > 0 && layer.resistance > 0 && (layer.capacitance > 0 || layer.edge_cap > 0))) {
        continue;
      }
      if (!horizontal && layer.direction == "HORIZONTAL") {
        horizontal = &layer;
      } else if (horizontal && layer.direction == "VERTICAL") {
        vertical = &layer;
        break;
      }
    }
    if (!horizontal || !vertical) {
      throw std::runtime_error("LEF needs horizontal/vertical wire RC; set synth.place.wire_rc=ohm_per_um,fF_per_um");
    }
    horizontal_r = horizontal->resistance / horizontal->width;
    horizontal_c = (horizontal->capacitance * horizontal->width + 2 * horizontal->edge_cap) * 1000;
    vertical_r   = vertical->resistance / vertical->width;
    vertical_c   = (vertical->capacitance * vertical->width + 2 * vertical->edge_cap) * 1000;
  }
  init_flute();
  uint64_t hash
      = hash_text(std::format("{}:{}:{}:{}:{}:{}", horizontal_r, horizontal_c, vertical_r, vertical_c, options.full, tech.dbu));
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    ph::Rc_net rc;
    const auto endpoint = [](const Endpoint& e) { return e.instance == "PIN" ? e.pin : e.instance + ":" + e.pin; };
    rc.driver           = endpoint(v.drivers[net]);
    const int begin = v.design.flat_net2pin_start[net], end = v.design.flat_net2pin_start[net + 1];
    if (end - begin < 2) {
      continue;
    }
    std::vector<int>                        xs, ys;
    std::map<std::pair<int, int>, unsigned> junction;
    std::vector<std::pair<int, int>>        positions;
    for (int p = begin; p < end; ++p) {
      const auto   node = v.design.pin2node[p];
      const double x = v.design.node_x[node] + v.design.pin_offset_x[p], y = v.design.node_y[node] + v.design.pin_offset_y[p];
      if (std::abs(x) > std::numeric_limits<int>::max() / 4.0 || std::abs(y) > std::numeric_limits<int>::max() / 4.0) {
        throw std::runtime_error("coordinates exceed FLUTE integer range");
      }
      positions.emplace_back(std::llround(x), std::llround(y));
      pin_locations.emplace(endpoint(v.pins[p]), std::pair{x, y});
      rc.nodes.push_back({endpoint(v.pins[p]), 0});
      hash = hash_text(std::format("{}:{}:{};", rc.nodes.back().pin, positions.back().first, positions.back().second), hash);
    }
    auto vertex = [&](std::pair<int, int> xy) {
      auto [it, added] = junction.emplace(xy, rc.nodes.size());
      if (added) {
        rc.nodes.push_back({{}, 0});
      }
      return it->second;
    };
    for (size_t i = 0; i < positions.size(); ++i) {
      rc.edges.push_back({static_cast<unsigned>(i), vertex(positions[i]), 1e-9});
    }
    for (const auto& [xy, index] : junction) {
      (void)index;
      xs.push_back(xy.first);
      ys.push_back(xy.second);
    }
    auto add_edge = [&](unsigned a, unsigned b, double dx, double dy) {
      if (a == b) {
        return;
      }
      const double resistance  = (horizontal_r * dx + vertical_r * dy) / tech.dbu;
      const double capacitance = (horizontal_c * dx + vertical_c * dy) / tech.dbu;
      rc.edges.push_back({a, b, std::max(1e-9, resistance)});
      rc.nodes[a].capacitance_ff += capacitance / 2;
      rc.nodes[b].capacitance_ff += capacitance / 2;
    };
    if (xs.size() > 1 && xs.size() <= 1000) {
      auto                                    tree = flute::flute(xs.size(), xs.data(), ys.data(), 3);
      std::set<std::pair<unsigned, unsigned>> edges;
      for (int i = 0; i < 2 * tree.deg - 2; ++i) {
        const auto a  = std::pair{tree.branch[i].x, tree.branch[i].y};
        const auto b  = std::pair{tree.branch[tree.branch[i].n].x, tree.branch[tree.branch[i].n].y};
        const auto ai = vertex(a), bi = vertex(b);
        if (edges.emplace(std::min(ai, bi), std::max(ai, bi)).second) {
          add_edge(ai, bi, std::abs(double(a.first) - b.first), std::abs(double(a.second) - b.second));
        }
      }
      std::free(tree.branch);
    } else if (xs.size() > 1000) {
      // A bounded star estimate lets timing drive fanout repair before FLUTE's
      // degree limit can be met. Each branch has its Manhattan wire R/C; the
      // absence of shared trunks is explicitly reported as approximate.
      result->approximate = true;
      const auto root     = vertex(positions.front());
      for (const auto& [xy, index] : junction) {
        if (index != root) {
          add_edge(root,
                   index,
                   std::abs(double(xy.first) - positions.front().first),
                   std::abs(double(xy.second) - positions.front().second));
        }
      }
    }
    for (const auto& node : rc.nodes) {
      hash = hash_text(std::format("{}:{:.17g};", node.pin, node.capacitance_ff), hash);
    }
    for (const auto& edge : rc.edges) {
      hash = hash_text(std::format("{}:{}:{:.17g};", edge.from, edge.to, edge.resistance_ohm), hash);
    }
    result->nets.push_back(std::move(rc));
  }
  result->identity = std::format("{:016x}", hash);
  return result;
}
struct CacheInfo {
  size_t reused = 0, changed = 0;
  bool   hit = false;
};
std::map<std::string, uint64_t> block_hashes(const View& v) {
  std::map<std::string, uint64_t>    hashes;
  std::map<std::string, std::string> cell_blocks;
  for (const auto& c : v.cells) {
    cell_blocks.emplace(c.name, c.block);
    auto [it, added] = hashes.emplace(c.block, hash_util::kFnv1a64_offset);
    (void)added;
    it->second = hash_text(std::format("{}:{}:{}:{};", c.name, c.macro, c.place.width, c.place.height), it->second);
  }
  for (const auto& [driver, sinks] : v.nets) {
    for (const auto& sink : sinks) {
      const auto mix = [&](const Endpoint& e) {
        if (e.instance == "PIN") {
          return;
        }
        auto& h = hashes.at(cell_blocks.at(e.instance));
        h       = hash_text(std::format("{}/{}->{}/{};", driver.instance, driver.pin, sink.instance, sink.pin), h);
      };
      mix(driver);
      mix(sink);
    }
  }
  return hashes;
}
CacheInfo restore_cache(View& v, const std::string& path, const std::string& environment,
                        const std::map<std::string, uint64_t>& hashes) {
  CacheInfo info;
  info.changed = hashes.size();
  if (path.empty() || !fs::is_regular_file(path)) {
    return info;
  }
  rapidjson::Document json;
  json.Parse(file_text(path).c_str());
  if (json.HasParseError() || !json.IsObject() || !json.HasMember("environment") || !json["environment"].IsString()
      || json["environment"].GetString() != environment || !json.HasMember("blocks") || !json["blocks"].IsObject()) {
    return info;
  }
  // rapidjson member lookup is a linear scan; index each object once.
  using Members    = std::unordered_map<std::string_view, const rapidjson::Value*>;
  const auto index = [](const rapidjson::Value& object) {
    Members out;
    for (const auto& m : object.GetObject()) {
      out.emplace(std::string_view(m.name.GetString(), m.name.GetStringLength()), &m.value);
    }
    return out;
  };
  const auto                          blocks = index(json["blocks"]);
  std::map<std::string_view, Members> unchanged;  // block -> cell positions, hash-matching blocks only
  for (const auto& [name, hash] : hashes) {
    const auto it = blocks.find(name);
    if (it == blocks.end()) {
      continue;
    }
    const auto& block = *it->second;
    if (!block.IsObject() || !block.HasMember("hash") || !block["hash"].IsString() || !block.HasMember("cells")
        || !block["cells"].IsObject()) {
      return info;
    }
    if (block["hash"].GetString() == std::format("{:016x}", hash)) {
      unchanged.emplace(name, index(block["cells"]));
    }
  }
  // Validate the complete record before applying any cached coordinate.
  auto cells = v.cells;
  for (auto& cell : cells) {
    const auto block = unchanged.find(cell.block);
    if (block == unchanged.end()) {
      const auto old = blocks.find(cell.block);
      if (old != blocks.end() && old->second->IsObject()) {
        const auto& record = *old->second;
        if (record.HasMember("anchor") && record["anchor"].IsArray() && record["anchor"].Size() == 4
            && record["anchor"][0].IsInt64() && record["anchor"][1].IsInt64() && record["anchor"][2].IsInt64()
            && record["anchor"][3].IsInt64() && record["anchor"][0].GetInt64() < record["anchor"][2].GetInt64()
            && record["anchor"][1].GetInt64() < record["anchor"][3].GetInt64() && cell.place.status != Status::FIXED
            && cell.place.status != Status::LOCKED) {
          cell.place.x         = record["anchor"][0].GetInt64()
                                 + (record["anchor"][2].GetInt64() - record["anchor"][0].GetInt64() - cell.place.width) / 2;
          cell.place.y         = record["anchor"][1].GetInt64()
                                 + (record["anchor"][3].GetInt64() - record["anchor"][1].GetInt64() - cell.place.height) / 2;
          const auto positions = record.HasMember("cells") && record["cells"].IsObject() ? index(record["cells"]) : Members{};
          const auto xy        = positions.find(cell.name);
          if (xy != positions.end() && xy->second->IsArray() && xy->second->Size() == 2 && (*xy->second)[0].IsInt64()
              && (*xy->second)[1].IsInt64()) {
            cell.place.x = (*xy->second)[0].GetInt64();
            cell.place.y = (*xy->second)[1].GetInt64();
          }
          cell.place.status = Status::PLACED;
        }
      }
      continue;
    }
    const auto position = block->second.find(cell.name);
    if (position == block->second.end()) {
      return info;
    }
    const auto& xy = *position->second;
    if (!xy.IsArray() || xy.Size() != 2 || !xy[0].IsInt64() || !xy[1].IsInt64()) {
      return info;
    }
    if (cell.place.status == Status::FIXED || cell.place.status == Status::LOCKED) {
      if (cell.place.x != xy[0].GetInt64() || cell.place.y != xy[1].GetInt64()) {
        return info;
      }
    } else {
      cell.place.x = xy[0].GetInt64();
      cell.place.y = xy[1].GetInt64();
      cell.locked  = true;
    }
  }
  std::set<std::string> reused;
  for (auto& cell : cells) {
    if (cell.locked) {
      reused.insert(cell.block);
    }
  }
  info.reused  = reused.size();
  info.changed = hashes.size() - info.reused;
  info.hit     = info.changed == 0;
  // IO positions affect placement even if every logic block is unchanged.
  if (!json.HasMember("terminals") || !json["terminals"].IsObject()) {
    return CacheInfo{0, hashes.size(), false};
  }
  auto       terminals = v.fp.terminals;
  const auto terms     = index(json["terminals"]);
  for (auto& terminal : terminals) {
    const auto it = terms.find(terminal.name);
    if (it == terms.end()) {
      return CacheInfo{0, hashes.size(), false};
    }
    const auto& xy = *it->second;
    if (!xy.IsArray() || xy.Size() != 2 || !xy[0].IsInt64() || !xy[1].IsInt64()) {
      return CacheInfo{0, hashes.size(), false};
    }
    if (terminal.status != Status::FIXED) {
      terminal.x = xy[0].GetInt64();
      terminal.y = xy[1].GetInt64();
    }
  }
  const auto& first  = v.fp.rows.front();
  const auto  step   = first.step;
  const auto  height = v.fp.rows.size() > 1 ? v.fp.rows[1].y - first.y : static_cast<int64_t>(v.fp.die.yh - first.y);
  for (auto& cell : cells) {
    if (cell.locked) {
      if (cell.place.x < first.x || cell.place.y < first.y || cell.place.x > first.x + first.count * step - cell.place.width
          || cell.place.y > v.fp.rows.back().y + height - cell.place.height || (cell.place.x - first.x) % step
          || (cell.place.y - first.y) % height) {
        return CacheInfo{0, hashes.size(), false};
      }
      // Tie cells are materialized after the previous DEF frame was seeded.
      // Cache locks bypass capture(), so restore their status/orientation here
      // as well as their coordinates.
      cell.place.status      = Status::PLACED;
      cell.place.orientation = v.fp.rows.at((cell.place.y - first.y) / height).orientation;
    }
  }
  v.cells        = std::move(cells);
  v.fp.terminals = std::move(terminals);
  return info;
}
struct Eco_stats {
  bool     used           = false;
  bool     single_cell    = false;
  bool     released_locks = false;  // whole-core retry made cache-reused cells movable
  unsigned attempts = 0, moved = 0;
  double   max_displacement_um = 0;
  ph::Rect window;
};
bool legalize_edits(View& v, const ph::Technology& tech, const ph::Floorplan& reference, const std::vector<std::string>& forced,
                    Eco_stats& stats) {
  const auto                                  original = v.cells;
  std::map<std::string, const ph::Component*> old;
  for (const auto& c : reference.components) {
    old.emplace(c.name, &c);
  }
  std::set<std::string> dirty(forced.begin(), forced.end());
  for (const auto& c : v.cells) {
    const auto it = old.find(c.name);
    if (it == old.end() || it->second->macro != c.macro || it->second->place.width != c.place.width
        || it->second->place.height != c.place.height) {
      dirty.insert(c.name);
    }
  }
  const auto&            first = v.fp.rows.front();
  const int64_t          sw = first.step, rh = std::llround(tech.sites.at(first.site).height * tech.dbu);
  std::vector<Ann_place> obstacles;
  std::vector<size_t>    edits;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& c = v.cells[i];
    if (!dirty.contains(c.name) || c.locked || c.place.status == Status::FIXED || c.place.status == Status::LOCKED) {
      obstacles.push_back(c.place);
    } else if (!tech.macros.at(c.macro).klass.starts_with("CORE") || c.place.height != rh) {
      return false;
    } else {
      edits.push_back(i);
    }
  }
  std::sort(edits.begin(), edits.end(), [&](size_t a, size_t b) {
    return v.cells[a].place.width != v.cells[b].place.width ? v.cells[a].place.width > v.cells[b].place.width
                                                            : v.cells[a].name < v.cells[b].name;
  });
  for (const auto index : edits) {
    auto&      c    = v.cells[index];
    const auto seed = c.place;
    double     best = std::numeric_limits<double>::infinity();
    Ann_place  chosen;
    for (const auto& row : v.fp.rows) {
      if (row.orientation == Orientation::FS && !tech.macros.at(c.macro).symmetry_x) {
        continue;
      }
      std::vector<std::pair<int64_t, int64_t>> occupied;
      for (const auto& p : obstacles) {
        if (p.y < row.y + rh && p.y + p.height > row.y) {
          occupied.emplace_back(p.x, p.x + p.width);
        }
      }
      std::sort(occupied.begin(), occupied.end());
      int64_t       left     = row.x;
      const int64_t right    = row.x + row.count * sw;
      const auto    consider = [&](int64_t lo, int64_t hi) {
        lo = row.x + static_cast<int64_t>(std::ceil(double(lo - row.x) / sw)) * sw;
        hi = row.x + static_cast<int64_t>(std::floor(double(hi - c.place.width - row.x) / sw)) * sw;
        if (hi < lo) {
          return;
        }
        const auto    desired  = row.x + std::llround(double(seed.x - row.x) / sw) * sw;
        const int64_t x        = std::clamp<int64_t>(desired, lo, hi);
        const double  distance = std::abs(double(x - seed.x)) + std::abs(double(row.y - seed.y));
        if (distance < best) {
          best               = distance;
          chosen             = seed;
          chosen.x           = x;
          chosen.y           = row.y;
          chosen.orientation = row.orientation;
          chosen.status      = Status::PLACED;
        }
      };
      for (const auto& [lo, hi] : occupied) {
        if (hi <= left || lo >= right) {
          continue;
        }
        consider(left, std::min(lo, right));
        left = std::max(left, hi);
        if (left >= right) {
          break;
        }
      }
      if (left < right) {
        consider(left, right);
      }
    }
    if (!std::isfinite(best)) {
      v.cells = original;
      return false;
    }
    c.place = chosen;
    obstacles.push_back(chosen);
  }
  ph::Floorplan check = v.fp;
  check.components.clear();
  for (const auto& c : v.cells) {
    check.components.push_back({c.name, c.macro, c.place});
  }
  try {
    check_legality(tech, check);
  } catch (const std::runtime_error&) {
    v.cells = original;
    return false;
  }
  stats.used        = true;
  stats.attempts    = 1;
  stats.single_cell = true;
  stats.window      = v.fp.die;
  for (size_t i = 0; i < v.cells.size(); ++i) {
    const auto& p  = original[i].place;
    const auto& q  = v.cells[i].place;
    stats.moved   += p.x != q.x || p.y != q.y;
    stats.max_displacement_um
        = std::max(stats.max_displacement_um, (std::abs(double(p.x - q.x)) + std::abs(double(p.y - q.y))) / tech.dbu);
  }
  make_arrays(v, tech);
  return true;
}
dp::Result local_place(View& v, const ph::Technology& tech, const Options& options, const ph::Floorplan& reference,
                       const std::vector<std::string>& forced, Eco_stats& stats) {
  std::map<std::string, const ph::Component*> old;
  std::set<std::string>                       current, dirty(forced.begin(), forced.end());
  for (const auto& c : reference.components) {
    old.emplace(c.name, &c);
  }
  ph::Rect   bounds{std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()};
  const auto include = [&](const Ann_place& p) {
    bounds.xl = std::min(bounds.xl, double(p.x));
    bounds.yl = std::min(bounds.yl, double(p.y));
    bounds.xh = std::max(bounds.xh, double(p.x + p.width));
    bounds.yh = std::max(bounds.yh, double(p.y + p.height));
  };
  for (const auto& c : v.cells) {
    current.insert(c.name);
    const auto it = old.find(c.name);
    if (it == old.end() || it->second->macro != c.macro || it->second->place.width != c.place.width
        || it->second->place.height != c.place.height) {
      dirty.insert(c.name);
    }
    if (dirty.contains(c.name)) {
      include(c.place);
    }
  }
  for (const auto& [name, c] : old) {
    if (!current.contains(name) || dirty.contains(name)) {
      include(c->place);
    }
  }
  dp::Result metrics;
  if (!std::isfinite(bounds.xl)) {
    make_arrays(v, tech);
    metrics.legal = dp::is_legal(v.design);
    return metrics;
  }
  stats.used            = true;
  auto         original = v.cells;  // the whole-core retry releases its cache locks
  const auto&  first    = v.fp.rows.front();
  const double sw = first.step, rh = std::llround(tech.sites.at(first.site).height * tech.dbu);
  const double core_xh = first.x + first.count * sw, core_yh = v.fp.rows.back().y + rh;
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    v.cells             = original;
    const double margin = std::ldexp(2 * rh, attempt);
    ph::Rect     window{std::max(double(first.x), first.x + std::floor((bounds.xl - first.x - margin) / sw) * sw),
                        std::max(double(first.y), first.y + std::floor((bounds.yl - first.y - margin) / rh) * rh),
                        std::min(core_xh, first.x + std::ceil((bounds.xh - first.x + margin) / sw) * sw),
                        std::min(core_yh, first.y + std::ceil((bounds.yh - first.y + margin) / rh) * rh)};
    const bool   entire_core = window.xl == first.x && window.yl == first.y && window.xh == core_xh && window.yh == core_yh;
    stats.window             = window;
    stats.attempts           = attempt + 1;
    for (auto& c : v.cells) {
      const bool intersects = c.place.x < window.xh && c.place.x + c.place.width > window.xl && c.place.y < window.yh
                              && c.place.y + c.place.height > window.yl;
      c.locked              = c.locked || !intersects;
    }
    try {
      make_arrays(v, tech);
      auto& d  = v.design;
      d.layout = {window.xl, window.yl, window.xh, window.yh, sw, rh};
      for (int i = 0; i < static_cast<int>(d.node_x.size()); ++i) {
        if (i < d.num_movable) {
          if (d.node_size_x[i] > window.xh - window.xl || d.node_size_y[i] > window.yh - window.yl) {
            throw std::runtime_error("ECO cell does not fit window");
          }
          d.node_x[i] = std::clamp(d.node_x[i], window.xl, window.xh - d.node_size_x[i]);
          d.node_y[i] = std::clamp(d.node_y[i], window.yl, window.yh - d.node_size_y[i]);
        } else if (d.node_size_x[i] > 0 && d.node_size_y[i] > 0) {
          // Clip obstacles to the local solver domain, preserving every pin's
          // absolute location and the physical object's original graph geometry.
          const auto x = d.node_x[i], y = d.node_y[i];
          const auto xl = std::clamp(x, window.xl, window.xh), yl = std::clamp(y, window.yl, window.yh);
          const auto xh    = std::clamp(x + d.node_size_x[i], window.xl, window.xh);
          const auto yh    = std::clamp(y + d.node_size_y[i], window.yl, window.yh);
          d.node_x[i]      = xl;
          d.node_y[i]      = yl;
          d.node_size_x[i] = xh - xl;
          d.node_size_y[i] = yh - yl;
          for (int k = d.flat_node2pin_start[i]; k < d.flat_node2pin_start[i + 1]; ++k) {
            const int p        = d.flat_node2pin[k];
            d.pin_offset_x[p] += x - xl;
            d.pin_offset_y[p] += y - yl;
          }
        }
      }
      dp::Options control;
      control.max_iterations = std::min(options.iterations, 60);
      control.bins_x = control.bins_y = std::min(options.bins, 16);
      control.num_threads             = options.threads;
      control.target_density          = options.density;
      control.legalize                = true;
      control.detailed_place          = true;
      metrics                         = dp::place(d, control);
      capture(v, tech, true);
      ph::Floorplan check = v.fp;
      check.components.clear();
      std::map<std::string, const Cell*> before;
      for (const auto& c : original) {
        before.emplace(c.name, &c);
      }
      stats.moved               = 0;
      stats.max_displacement_um = 0;
      for (auto& c : v.cells) {
        check.components.push_back({c.name, c.macro, c.place});
        const auto& p = before.at(c.name)->place;
        if (c.place.x != p.x || c.place.y != p.y) {
          ++stats.moved;
        }
        stats.max_displacement_um = std::max(stats.max_displacement_um,
                                             (std::abs(double(c.place.x) - p.x) + std::abs(double(c.place.y) - p.y)) / tech.dbu);
        c.locked                  = before.at(c.name)->locked;
      }
      check_legality(tech, check);
      make_arrays(v, tech);
      return metrics;
    } catch (const std::runtime_error&) {
      if (entire_core) {
        // Changed blocks may need whitespace (or a new proxy's footprint) held by
        // cache-locked cells. Retry the whole core once with those unlocked;
        // FIXED/LOCKED status still pins real obstacles.
        if (!stats.released_locks && std::any_of(original.begin(), original.end(), [](const Cell& c) { return c.locked; })) {
          stats.released_locks = true;
          for (auto& c : original) {
            c.locked = false;
          }
          --attempt;  // rerun the same whole-core window
          continue;
        }
        v.cells = original;
        make_arrays(v, tech);
        throw;
      }
    }
  }
  throw std::runtime_error("ECO window expansion exhausted");
}
void save_cache(const View& v, const std::string& path, const std::string& environment,
                const std::map<std::string, uint64_t>& hashes) {
  if (path.empty()) {
    return;
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("environment");
  w.String(environment.c_str());
  w.Key("blocks");
  w.StartObject();
  std::map<std::string_view, std::vector<const Cell*>> members;
  for (const auto& c : v.cells) {
    members[c.block].push_back(&c);
  }
  for (const auto& [block, hash] : hashes) {
    w.Key(block.c_str());
    w.StartObject();
    w.Key("hash");
    w.String(std::format("{:016x}", hash).c_str());
    w.Key("cells");
    w.StartObject();
    int64_t xl = std::numeric_limits<int64_t>::max(), yl = xl, xh = std::numeric_limits<int64_t>::min(), yh = xh;
    for (const auto* c : members[block]) {
      w.Key(c->name.c_str());
      w.StartArray();
      w.Int64(c->place.x);
      w.Int64(c->place.y);
      w.EndArray();
      xl = std::min(xl, c->place.x);
      yl = std::min(yl, c->place.y);
      xh = std::max(xh, c->place.x + c->place.width);
      yh = std::max(yh, c->place.y + c->place.height);
    }
    w.EndObject();
    w.Key("anchor");
    w.StartArray();
    w.Int64(xl);
    w.Int64(yl);
    w.Int64(xh);
    w.Int64(yh);
    w.EndArray();
    w.EndObject();
  }
  w.EndObject();
  w.Key("terminals");
  w.StartObject();
  for (const auto& t : v.fp.terminals) {
    w.Key(t.name.c_str());
    w.StartArray();
    w.Int64(t.x);
    w.Int64(t.y);
    w.EndArray();
  }
  w.EndObject();
  w.EndObject();
  fs::create_directories(fs::path(path).parent_path());
  const auto temp = path + ".tmp";
  {
    std::ofstream file(temp);
    file << buffer.GetString() << '\n';
    file.close();
    if (!file) {
      throw std::runtime_error("cannot save placement cache");
    }
  }
  fs::rename(temp, path);
}
}  // namespace

void check_legality(const ph::Technology& tech, const ph::Floorplan& fp) {
  if (fp.rows.empty()) {
    throw std::runtime_error("placement has no rows");
  }
  const auto&                      first = fp.rows.front();
  const auto&                      site  = tech.sites.at(first.site);
  const int64_t                    rh = std::llround(site.height * tech.dbu), sw = std::llround(site.width * tech.dbu);
  const int64_t                    xl = first.x, yl = first.y, xh = xl + first.count * sw, yh = fp.rows.back().y + rh;
  std::vector<std::vector<size_t>> by_row((yh - yl + rh - 1) / rh);
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& a = fp.components[i];
    if (a.place.status == Status::UNPLACED || a.place.x < xl || a.place.y < yl || a.place.x + a.place.width > xh
        || a.place.y + a.place.height > yh) {
      throw std::runtime_error("cell outside core/unplaced: " + a.name);
    }
    const auto& macro    = tech.macros.at(a.macro);
    const bool  standard = macro.klass.starts_with("CORE");
    if (standard && ((a.place.x - xl) % sw || (a.place.y - yl) % rh)) {
      throw std::runtime_error("cell off row/site: " + a.name);
    }
    if (standard) {
      const auto row = static_cast<size_t>((a.place.y - yl) / rh);
      if (a.place.orientation != fp.rows.at(row).orientation) {
        throw std::runtime_error("cell orientation differs from row: " + a.name);
      }
    }
    // Rows below are bucket indices only; macros need not be row-aligned.
    for (int64_t row = (a.place.y - yl) / rh; row * rh + yl < a.place.y + a.place.height; ++row) {
      by_row[row].push_back(i);
    }
  }
  // Two overlapping rectangles share a row band; within a band sorted by x, the
  // left one's span reaches the right one's origin.
  for (auto& members : by_row) {
    std::sort(members.begin(), members.end(), [&](size_t i, size_t j) {
      return fp.components[i].place.x < fp.components[j].place.x;
    });
    for (size_t i = 0; i < members.size(); ++i) {
      const auto& a = fp.components[members[i]];
      for (size_t j = i + 1; j < members.size() && fp.components[members[j]].place.x < a.place.x + a.place.width; ++j) {
        const auto& b = fp.components[members[j]];
        if (a.place.y < b.place.y + b.place.height && b.place.y < a.place.y + a.place.height) {
          throw std::runtime_error("overlapping cells: " + a.name + ", " + b.name);
        }
      }
    }
  }
}

Steiner_tree steiner_tree(const std::vector<Point>& points) {
  if (points.empty() || points.size() > 4097) {
    throw std::invalid_argument("Steiner terminal count outside repair budget");
  }
  init_flute();
  Steiner_tree                            result;
  std::map<std::pair<int, int>, unsigned> indices;
  const auto                              vertex = [&](Point p) {
    if (!std::isfinite(p.first) || !std::isfinite(p.second) || std::abs(p.first) > std::numeric_limits<int>::max() / 4.0
        || std::abs(p.second) > std::numeric_limits<int>::max() / 4.0) {
      throw std::invalid_argument("invalid Steiner coordinate");
    }
    const auto key         = std::pair{int(std::llround(p.first)), int(std::llround(p.second))};
    const auto [it, added] = indices.emplace(key, result.points.size());
    if (added) {
      result.points.emplace_back(key.first, key.second);
    }
    return it->second;
  };
  for (const auto& p : points) {
    result.terminals.push_back(vertex(p));
  }
  if (indices.size() == 1) {
    return result;
  }
  if (indices.size() > 1000) {
    throw std::runtime_error("repair Steiner tree exceeds FLUTE's 1000 distinct locations");
  }
  std::vector<int> xs, ys;
  for (const auto& [xy, index] : indices) {
    (void)index;
    xs.push_back(xy.first);
    ys.push_back(xy.second);
  }
  auto                                    tree = flute::flute(xs.size(), xs.data(), ys.data(), 3);
  std::set<std::pair<unsigned, unsigned>> edges;
  for (int i = 0; i < 2 * tree.deg - 2; ++i) {
    const auto a = vertex({tree.branch[i].x, tree.branch[i].y});
    const auto b = vertex({tree.branch[tree.branch[i].n].x, tree.branch[tree.branch[i].n].y});
    if (a != b && edges.emplace(std::min(a, b), std::max(a, b)).second) {
      result.edges.emplace_back(a, b);
    }
  }
  std::free(tree.branch);
  return result;
}
Result place(hhds::Graph& source, hhds::GraphLibrary& library, const Options& options) {
  if (!(options.utilization > 0 && options.utilization < 1) || !(options.aspect > 0 && std::isfinite(options.aspect))
      || !(options.density > 0 && options.density <= 1) || options.utilization > options.density || options.iterations < 0
      || options.bins < 2 || options.threads < 1) {
    throw std::invalid_argument("invalid placement utilization/aspect/density/iteration/bin/thread settings");
  }
  auto                          tech = ph::read_lef(options.lefs);
  std::unordered_set<hhds::Gid> preserved;
  for (const auto& g : source.definitions().graphs()) {
    if (tech.macros.contains(std::string(g->get_name()))) {
      preserved.insert(g->get_gid());
    }
  }
  Result result;
  result.graph = partition::flatten_hierarchy(&source, &library, source.get_name(), nullptr, false, preserved, {}, true);
  if (!result.graph) {
    throw std::runtime_error("cannot flatten mapped netlist for placement");
  }
  auto& graph = *result.graph;
  remove_transparent_latches(graph);
  View v;
  v.weights            = options.net_weights;
  v.fp.design          = std::string(source.get_name());
  v.fp.dbu             = tech.dbu;
  const auto  memories = memory_proxies(graph, tech, options.liberty);
  Resolver    resolver(tech, memories);
  double      area      = 0;
  int64_t     max_width = 0, max_height = 0;
  std::string site_name;
  for (auto node : graph.body().nodes()) {
    if (auto io = node.get_subnode_io(); io || memories.contains(node.get_debug_nid())) {
      const auto name = io ? std::string(io->get_name()) : memories.at(node.get_debug_nid());
      auto       it   = tech.macros.find(name);
      if (it == tech.macros.end()) {
        throw std::runtime_error("mapped cell/macro absent from LEF: " + name);
      }
      const auto& macro = it->second;
      if (site_name.empty() && !macro.site.empty()) {
        site_name = macro.site;
      }
      auto instance = std::string(node.get_name());
      if (instance.empty()) {
        throw std::runtime_error("mapped physical cell has no stable instance name");
      }
      Ann_place p(0, 0, macro.width * tech.dbu, macro.height * tech.dbu);
      if (auto attr = node.attr(attrs::place); attr.has()) {
        p.x           = attr.get().x;
        p.y           = attr.get().y;
        p.status      = attr.get().status;
        p.orientation = attr.get().orientation;
      }
      if (p.width <= 0 || p.height <= 0) {
        throw std::runtime_error("invalid LEF cell size: " + name);
      }
      const auto dot    = instance.rfind('.');
      const auto prefix = dot == std::string::npos ? std::string{} : instance.substr(0, dot);
      const auto owner  = node.attr(attrs::placement_block);
      const auto block  = owner.has() ? owner.get() : std::format("{}:{}", prefix, gu::node_color_of(node));
      node.attr(attrs::placement_block).set(block);
      v.cells.push_back({node, instance, name, block, p});
      area       += double(p.width) * p.height;
      max_width   = std::max(max_width, p.width);
      max_height  = std::max(max_height, p.height);
    } else if (!Ntype::is_pin_trackable(gu::type_op_of(node)) && gu::type_op_of(node) != Ntype_op::AttrSet) {
      throw std::runtime_error("unmapped physical object: " + std::string(Ntype::get_name(gu::type_op_of(node))));
    }
  }
  if (v.cells.empty()) {
    throw std::runtime_error("placement needs at least one mapped physical cell");
  }
  if (site_name.empty() && !tech.sites.empty()) {
    site_name = tech.sites.begin()->first;
  }
  if (site_name.empty() || !tech.sites.contains(site_name)) {
    throw std::runtime_error("mapped cells have no LEF SITE");
  }
  const auto&   site = tech.sites.at(site_name);
  const int64_t sw = std::llround(site.width * tech.dbu), rh = std::llround(site.height * tech.dbu);
  if (sw <= 0 || rh <= 0) {
    throw std::runtime_error("invalid LEF SITE dimensions");
  }
  const auto env = environment_key(options, tech);
  const auto cache_path
      = (options.timing_seed || options.cache_dir.empty()) ? std::string{} : options.cache_dir + "/placement.json";
  auto previous         = options.previous;
  bool cached_reference = false;
  if (!previous && options.floorplan.empty() && !cache_path.empty() && fs::is_regular_file(cache_path)
      && fs::is_regular_file(options.cache_dir + "/floorplan.def")) {
    rapidjson::Document cached;
    cached.Parse(file_text(cache_path).c_str());
    if (!cached.HasParseError() && cached.IsObject() && cached.HasMember("environment") && cached["environment"].IsString()
        && cached["environment"].GetString() == env) {
      try {
        auto frame = ph::read_def(options.cache_dir + "/floorplan.def");
        validate_rows(tech, frame);
        for (auto& c : frame.components) {
          const auto master = tech.macros.find(c.macro);
          if (master != tech.macros.end()) {
            c.place.width  = std::llround(master->second.width * tech.dbu);
            c.place.height = std::llround(master->second.height * tech.dbu);
          }
        }
        const auto width = frame.rows.front().count * sw, height = static_cast<int64_t>(frame.rows.size()) * rh;
        if (frame.design == source.get_name() && area <= double(width) * height * options.density && max_width <= width
            && max_height <= static_cast<int64_t>(height)) {
          previous         = std::make_shared<ph::Floorplan>(std::move(frame));
          cached_reference = true;
        }
      } catch (const std::exception&) { /* A stale frame never blocks a fresh placement. */
      }
    }
  }
  if (options.floorplan.empty() && previous) {
    v.fp = *previous;
    validate_rows(tech, v.fp);
  } else if (options.floorplan.empty()) {
    const double core_area = area / options.utilization;
    const auto   columns   = std::max<int64_t>(std::ceil(std::sqrt(core_area * options.aspect) / sw), (max_width + sw - 1) / sw);
    const auto   rows
        = std::max<int64_t>({2, static_cast<int64_t>(std::ceil(core_area / (columns * sw * rh))), (max_height + rh - 1) / rh});
    v.fp.die = {0, 0, double(columns * sw), double(rows * rh)};
    // Alternating N/FS rows require X-symmetric cells, as in sky130HD.
    const bool flip
        = std::all_of(v.cells.begin(), v.cells.end(), [&](const auto& c) { return tech.macros.at(c.macro).symmetry_x; });
    for (int64_t row = 0; row < rows; ++row) {
      v.fp.rows.push_back(
          {"ROW_" + std::to_string(row), site_name, 0, row * rh, columns, sw, flip && row % 2 ? Orientation::FS : Orientation::N});
    }
  } else {
    v.fp = ph::read_def(options.floorplan);
    if (v.fp.design != source.get_name()) {
      throw std::runtime_error("floorplan DEF DESIGN differs from selected top");
    }
    std::unordered_map<std::string_view, Cell*> by_name;
    for (auto& c : v.cells) {
      by_name.emplace(c.name, &c);
    }
    for (const auto& imported : v.fp.components) {
      const auto it = by_name.find(imported.name);
      if (it == by_name.end()) {
        if (imported.place.status == Status::FIXED) {
          throw std::runtime_error("fixed DEF component absent from mapped netlist: " + imported.name);
        }
        continue;
      }
      auto& c = *it->second;
      if (imported.macro != c.macro) {
        throw std::runtime_error("floorplan cell master mismatch: " + c.name);
      }
      if (imported.place.status == Status::FIXED) {
        auto p   = imported.place;
        p.width  = c.place.width;
        p.height = c.place.height;
        c.place  = p;
      }
    }
  }
  validate_rows(tech, v.fp);
  if (previous) {
    std::map<std::string, const ph::Component*> old;
    for (const auto& c : previous->components) {
      old.emplace(c.name, &c);
    }
    for (auto& c : v.cells) {
      if (const auto it = old.find(c.name); it != old.end()) {
        c.place.x           = it->second->place.x;
        c.place.y           = it->second->place.y;
        c.place.orientation = it->second->place.orientation;
        if (c.macro == it->second->macro) {
          c.place.status = it->second->place.status;
        } else {
          c.place.status = Status::PLACED;
        }
      } else if (c.place.status == Status::UNPLACED && !options.changed_cells.empty()) {
        for (const auto& name : options.changed_cells) {
          if (const auto anchor = old.find(name); anchor != old.end()) {
            c.place.x      = anchor->second->place.x;
            c.place.y      = anchor->second->place.y;
            c.place.status = Status::PLACED;
            break;
          }
        }
      }
    }
  }
  std::unordered_map<std::string, ph::Terminal> imported_terms;
  for (auto& t : v.fp.terminals) {
    imported_terms.emplace(t.name, std::move(t));
  }
  v.fp.terminals.clear();
  v.fp.components.clear();
  v.fp.nets.clear();
  std::map<std::string, Pin>      top_pins;
  // Terminals whose position came from the floorplan/previous frame; only the
  // rest get the default spread (and, on the ECO path, a slot from assign_io).
  std::unordered_set<std::string> imported_pos;
  auto                            add_terminal = [&](Pin p, const std::string& name, const std::string& direction) {
    ph::Terminal t;
    t.name      = name;
    t.direction = direction;
    t.status    = Status::PLACED;
    t.x         = std::llround((v.fp.die.xl + v.fp.die.xh) / 2);
    t.y         = std::llround(v.fp.die.yl);
    auto layer  = std::find_if(tech.routing_layers.begin(), tech.routing_layers.end(), [&](const auto& layer_name) {
      return tech.layers.at(layer_name).direction == "HORIZONTAL";
    });
    if (layer == tech.routing_layers.end()) {
      throw std::runtime_error("LEF has no horizontal routing layer for IO pins");
    }
    const double half = std::max(1.0, std::round(tech.layers.at(*layer).width * tech.dbu / 2));
    t.shapes.push_back({
        *layer,
        {-half, -half, half, half}
    });
    if (const auto it = imported_terms.find(name); it != imported_terms.end()) {
      if (it->second.status != Status::FIXED && !previous) {
        throw std::runtime_error("floorplan PINS must be FIXED: " + name);
      }
      t           = it->second;
      t.direction = direction;
      imported_pos.insert(name);
    }
    top_pins.emplace(name, p);
    v.fp.terminals.push_back(std::move(t));
  };
  auto io = graph.get_io();
  for (const auto& d : io->get_input_pin_decls()) {
    const auto p = graph.get_input_pin(d.name);
    for (int b = 0; b < std::max(1, static_cast<int>(d.bits)); ++b) {
      add_terminal(p, bit_name(d.name, std::max(1, static_cast<int>(d.bits)), b), "INPUT");
    }
  }
  for (const auto& d : io->get_output_pin_decls()) {
    const auto p       = graph.get_output_pin(d.name);
    const auto drivers = p.get_driver_pins();
    if (drivers.size() != 1) {
      throw std::runtime_error("top output needs exactly one driver: " + d.name);
    }
    for (int b = 0; b < std::max(1, static_cast<int>(d.bits)); ++b) {
      auto name = bit_name(d.name, std::max(1, static_cast<int>(d.bits)), b);
      add_terminal(p, name, "OUTPUT");
      v.nets[resolver.bit(drivers.front(), b)].push_back({"PIN", name, -1});
    }
  }
  // Start with distributed side pins so the first GP does not pull every
  // cell toward one die edge. The subsequent Hungarian assignment may move
  // these pins to any available edge slot.
  for (const auto* direction : {"INPUT", "OUTPUT"}) {
    size_t count = 0, index = 0;
    for (const auto& terminal : v.fp.terminals) {
      if (terminal.direction == direction && terminal.status != Status::FIXED && !imported_pos.contains(terminal.name)) {
        ++count;
      }
    }
    for (auto& terminal : v.fp.terminals) {
      if (terminal.direction == direction && terminal.status != Status::FIXED && !imported_pos.contains(terminal.name)) {
        terminal.x = std::llround(std::string_view(direction) == "INPUT" ? v.fp.die.xl : v.fp.die.xh);
        terminal.y = std::llround(v.fp.die.yl + (++index) * (v.fp.die.yh - v.fp.die.yl) / (count + 1));
      }
    }
  }
  for (const auto& cell : v.cells) {
    const bool memory = memories.contains(cell.node.get_debug_nid());
    for (auto sink : cell.node.inp_sorted_pins()) {
      if (memory && !memory_runtime_pin(sink.get_port_id())) {
        continue;
      }
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1) {
        throw std::runtime_error("physical input needs exactly one driver: " + cell.name);
      }
      if (memory) {
        for (int b = 0; b < std::max(1, gu::bits_of(drivers.front())); ++b) {
          v.nets[resolver.bit(drivers.front(), b)].push_back({cell.name, memory_pin(sink.get_port_id(), b, false), -1});
        }
        continue;
      }
      const auto& macro = tech.macros.at(cell.macro);
      const auto  name  = pin_name(sink);
      if (macro.pins.contains(name)) {
        v.nets[resolver.bit(drivers.front(), 0)].push_back({cell.name, name, -1});
      } else {
        bool found = false;
        for (int b = 0; macro.pins.contains(std::format("{}[{}]", name, b)); ++b) {
          v.nets[resolver.bit(drivers.front(), b)].push_back({cell.name, std::format("{}[{}]", name, b), -1});
          found = true;
        }
        if (!found) {
          throw std::runtime_error("physical input pin absent from LEF: " + cell.macro + "/" + name);
        }
      }
    }
  }
  // Constants remain logical constants in the graph. Instantiate real tie cells
  // for the physical view and graph so DEF/Verilog/STA share the same netlist.
  std::array<Pin, 2> tie_drivers;
  for (int value = 0; value < 2; ++value) {
    Endpoint key{{}, {}, value};
    auto     it = v.nets.find(key);
    if (it == v.nets.end()) {
      continue;
    }
    std::string macro_name, output;
    // SKY130's characterized dual tie cell. Other technologies need an explicit
    // matching physical tie cell; never emit a dangling constant net.
    if (tech.macros.contains("sky130_fd_sc_hd__conb_1")) {
      macro_name = "sky130_fd_sc_hd__conb_1";
      output     = value ? "HI" : "LO";
    }
    if (macro_name.empty()) {
      throw std::runtime_error("constant wiring needs a physical tie cell (sky130 conb_1 is supported)");
    }
    // A re-placed graph (timing/repair/resynth rounds) already carries this tie
    // cell, and a constant top output keeps its logical constant: reuse the tie.
    const std::string tie_name = value ? "__place_tiehi" : "__place_tielo";
    const auto        existing = std::find_if(v.cells.begin(), v.cells.end(), [&](const Cell& c) { return c.name == tie_name; });
    Node              node;
    if (existing != v.cells.end()) {
      if (existing->macro != macro_name) {
        throw std::runtime_error("tie cell master mismatch: " + tie_name);
      }
      node = existing->node;
    } else {
      auto tie_io = library.find_io(macro_name);
      if (!tie_io) {
        tie_io = library.create_io(macro_name);
        tie_io->add_output("HI", 0);
        tie_io->add_output("LO", 1);
        tie_io->set_bits("HI", 1);
        tie_io->set_bits("LO", 1);
      }
      node = graph.create_node();
      gu::set_type_op(node, Ntype_op::Sub);
      node.set_subnode(tie_io);
      node.set_name(tie_name);
      const auto& macro = tech.macros.at(macro_name);
      Ann_place   p(0, 0, macro.width * tech.dbu, macro.height * tech.dbu);
      // Ties are materialized after the previous frame seeded the cells.
      if (previous) {
        for (const auto& c : previous->components) {
          if (c.name == tie_name && c.macro == macro_name) {
            p.x           = c.place.x;
            p.y           = c.place.y;
            p.orientation = c.place.orientation;
            p.status      = c.place.status;
          }
        }
      }
      v.cells.push_back({node, tie_name, macro_name, "__ties", p});
    }
    auto sinks = std::move(it->second);
    v.nets.erase(it);
    // Append: a reused tie already drives the cells wired to it last round.
    auto& net = v.nets[{tie_name, output, -1}];
    net.insert(net.end(), sinks.begin(), sinks.end());
    // Physical connectivity replaces constants at each directly connected cell
    // input. Constants embedded in bus wiring keep their logical aliases; the
    // physical view resolves those aliases to this same tie source.
    auto driver = node.create_driver_pin(output);
    gu::set_bits(driver, 1);
    tie_drivers[value] = driver;
    std::unordered_map<std::string_view, Node> nodes;
    for (const auto& c : v.cells) {
      nodes.emplace(c.name, c.node);
    }
    for (const auto& target : sinks) {
      if (target.instance != "PIN") {
        for (auto sink : nodes.at(target.instance).inp_pins_snapshot()) {
          if (pin_name(sink) == target.pin) {
            const auto old_drivers = sink.get_driver_pins();
            if (old_drivers.size() == 1 && old_drivers.front().is_const()) {
              gu::drop_drivers(sink);
              sink.connect_driver(driver);
            }
          }
        }
      }
    }
  }
  // Materialize tie sources inside bus aliases too, including memory ports.
  // Timing, DEF and the saved logical graph must see the same bit connectivity.
  for (const auto& c : v.cells) {
    for (auto sink : c.node.inp_pins_snapshot()) {
      const bool memory = memories.contains(c.node.get_debug_nid());
      if (memory && !memory_runtime_pin(sink.get_port_id())) {
        continue;
      }
      const auto drivers = sink.get_driver_pins();
      if (drivers.size() != 1) {
        continue;
      }
      const auto original = drivers.front();
      int        width    = std::max(1, gu::bits_of(original));
      if (!memory) {
        const auto  name  = pin_name(sink);
        const auto& macro = tech.macros.at(c.macro);
        if (macro.pins.contains(name)) {
          width = 1;
        } else {
          width = 0;
          while (macro.pins.contains(std::format("{}[{}]", name, width))) {
            ++width;
          }
        }
      }
      std::vector<int> constants;
      bool             changed = false;
      for (int b = 0; b < width; ++b) {
        const int value = resolver.bit(original, b).constant;
        constants.push_back(value);
        changed |= value >= 0;
      }
      if (!changed) {
        continue;
      }
      std::vector<Pin> values;
      for (int b = 0; b < width; ++b) {
        const int value = constants[b];
        if (value >= 0) {
          if (tie_drivers[value].is_invalid()) {
            throw std::runtime_error("missing physical tie source");
          }
          values.push_back(tie_drivers[value]);
        } else {
          values.push_back(width == 1 ? original : gu::create_get_mask(graph, original, b).get_driver_pin(0));
        }
      }
      gu::drop_drivers(sink);
      if (width == 1) {
        sink.connect_driver(values.front());
      } else {
        auto       concat = gu::create_typed_node(graph, Ntype_op::Concat);
        const auto one    = gu::create_const(graph, *Dlop::create_integer(1));
        for (int lane = 0; lane < width; ++lane) {
          concat.create_sink_pin(2 * lane).connect_driver(values[width - 1 - lane]);
          concat.create_sink_pin(2 * lane + 1).connect_driver(one);
        }
        auto out = concat.create_driver_pin(0);
        gu::set_bits(out, width);
        gu::set_unsign(out);
        sink.connect_driver(out);
      }
    }
  }
  for (auto& c : v.cells) {
    if (c.place.status == Status::UNPLACED) {
      const auto  h   = hash_text(c.name);
      const auto& row = v.fp.rows[(h >> 32) % v.fp.rows.size()];
      c.place.x       = row.x + static_cast<int64_t>(h % std::max<int64_t>(1, row.count - c.place.width / sw + 1)) * sw;
      c.place.y       = row.y;
    }
  }
  // Square proxy macros are packed first and become obstacles for DREAMPlace.
  // Their exact square dimensions need not be a multiple of the standard site.
  std::vector<Cell*> macros;
  for (auto& c : v.cells) {
    if (memories.contains(c.node.get_debug_nid())) {
      macros.push_back(&c);
    }
  }
  std::sort(macros.begin(), macros.end(), [](const auto* a, const auto* b) {
    return a->place.width != b->place.width ? a->place.width > b->place.width : a->name < b->name;
  });
  for (auto* macro : macros) {
    if (macro->place.status == Status::FIXED || macro->place.status == Status::LOCKED) {
      continue;
    }
    bool        found = false;
    const auto& first = v.fp.rows.front();
    for (int64_t y = first.y; !found && y + macro->place.height <= v.fp.rows.back().y + rh; y += rh) {
      for (int64_t x = first.x; !found && x + macro->place.width <= first.x + first.count * sw; x += sw) {
        bool overlap = false;
        for (const auto& c : v.cells) {
          if (&c != macro && (c.place.status == Status::FIXED || c.place.status == Status::LOCKED) && x < c.place.x + c.place.width
              && x + macro->place.width > c.place.x && y < c.place.y + c.place.height && y + macro->place.height > c.place.y) {
            overlap = true;
            break;
          }
        }
        if (!overlap) {
          macro->place.x           = x;
          macro->place.y           = y;
          macro->place.orientation = Orientation::N;
          macro->place.status      = Status::FIXED;
          found                    = true;
        }
      }
    }
    if (!found) {
      throw std::runtime_error("no space for square memory proxy: " + macro->name);
    }
  }
  std::sort(v.cells.begin(), v.cells.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  const auto hashes = block_hashes(v);
  auto       cache  = restore_cache(v, cache_path, env, hashes);
  make_arrays(v, tech);
  dp::Options control;
  control.max_iterations = options.iterations;
  control.bins_x = control.bins_y = options.bins;
  control.num_threads             = options.threads;
  control.target_density          = options.density;
  control.legalize                = false;
  control.detailed_place          = false;
  dp::Result   metrics;
  const double initial_hpwl = [&] {
    auto d = v.design;
    std::fill(d.net_weights.begin(), d.net_weights.end(), 1);
    return dp::hpwl(d);
  }();
  Eco_stats eco;
  if (options.timing_seed) {
    metrics.legal = false;
  } else if (!cache.hit && previous && options.full) {
    // The ECO path keeps imported IO positions; slot only the new terminals,
    // treating the imported ones as fixed obstacles for assign_io.
    if (std::any_of(v.fp.terminals.begin(), v.fp.terminals.end(), [&](const auto& t) {
          return t.status != Status::FIXED && !imported_pos.contains(t.name);
        })) {
      std::vector<Status> saved;
      for (auto& t : v.fp.terminals) {
        saved.push_back(t.status);
        if (imported_pos.contains(t.name)) {
          t.status = Status::FIXED;
        }
      }
      assign_io(v);
      for (size_t i = 0; i < saved.size(); ++i) {
        v.fp.terminals[i].status = saved[i];
      }
      make_arrays(v, tech);
    }
    auto changed = options.changed_cells;
    if (changed.empty() && cached_reference && cache.changed) {
      for (const auto& c : v.cells) {
        if (!c.locked && c.place.status != Status::FIXED && c.place.status != Status::LOCKED) {
          changed.push_back(c.name);
        }
      }
    }
    if (options.legalize_cells && legalize_edits(v, tech, *previous, changed, eco)) {
      metrics.legal = true;
    } else {
      metrics = local_place(v, tech, options, *previous, changed, eco);
      if (eco.released_locks) {
        cache = {0, hashes.size(), false};  // reused blocks were movable, as below
      }
    }
  } else if (!cache.hit) {
    metrics = dp::place(v.design, control);
    capture(v, tech, false);
    assign_io(v);
    make_arrays(v, tech);
    control.legalize       = options.full;
    control.detailed_place = options.full;
    try {
      metrics = dp::place(v.design, control);
    } catch (const std::runtime_error&) {
      if (cache.reused == 0) {
        throw;
      }
      // Changed blocks may need whitespace outside their old anchors. Retry
      // with reusable blocks movable; imported FIXED cells remain fixed.
      for (auto& c : v.cells) {
        c.locked = false;
      }
      cache = {0, hashes.size(), false};
      make_arrays(v, tech);
      metrics = dp::place(v.design, control);
    }
    capture(v, tech, options.full);
  } else {
    metrics.legal     = dp::is_legal(v.design);
    metrics.converged = false;
    capture(v, tech, options.full);
  }
  for (auto& c : v.cells) {
    c.node.attr(attrs::place).set(c.place);
    c.node.attr(attrs::placement_block).set(c.block);
    v.fp.components.push_back({c.name, c.macro, c.place});
  }
  // Keep input port and net names identical: OpenROAD's structural Verilog
  // writer does not emit input aliases for otherwise valid DEF pin/net names.
  // Reserve every port name before choosing names for internal nets.
  std::set<std::string> net_names;
  for (const auto& terminal : v.fp.terminals) {
    net_names.insert(terminal.name);
  }
  for (size_t net = 0; net < v.drivers.size(); ++net) {
    auto name = v.drivers[net].instance == "PIN" ? v.drivers[net].pin : std::format("net_{}", net);
    if (v.drivers[net].instance != "PIN") {
      const auto base   = name;
      unsigned   suffix = 0;
      while (!net_names.insert(name).second) {
        name = base + "_" + std::to_string(++suffix);
      }
    }
    ph::Net n{name, {}};
    n.connections.push_back({v.drivers[net].instance, v.drivers[net].pin});
    for (const auto& sink : v.nets.at(v.drivers[net])) {
      n.connections.push_back({sink.instance, sink.pin});
    }
    for (const auto& c : n.connections) {
      if (c.instance == "PIN") {
        v.fp.terminals[v.terminal.at(c.pin)].net = name;
      }
    }
    v.fp.nets.push_back(std::move(n));
  }
  // Unconnected input ports still need a DEF NET and stable terminal identity.
  for (auto& t : v.fp.terminals) {
    if (t.net.empty()) {
      t.net = t.name;
      v.fp.nets.push_back({t.net, {{"PIN", t.name}}});
    }
  }
  for (auto& t : v.fp.terminals) {
    if (t.status != Status::FIXED) {
      for (auto& shape : t.shapes) {
        const double half = (shape.rect.xh - shape.rect.xl) / 2;
        if (t.x == v.fp.die.xl) {
          shape.rect = {0, -half, 2 * half, half};
        } else if (t.x == v.fp.die.xh) {
          shape.rect = {-2 * half, -half, 0, half};
        } else if (t.y == v.fp.die.yl) {
          shape.rect = {-half, 0, half, 2 * half};
        } else if (t.y == v.fp.die.yh) {
          shape.rect = {-half, -2 * half, half, 0};
        }
      }
    }
  }
  for (const auto& t : v.fp.terminals) {
    Ann_place p;
    p.x                  = t.x;
    p.y                  = t.y;
    p.orientation        = t.orientation;
    p.status             = t.status;
    const auto pin       = top_pins.at(t.name);
    const auto decl_name = std::string(pin.get_pin_name());
    if (graph.get_io()->get_bits(decl_name) <= 1) {
      pin.attr(attrs::pin_place).set(p);
    }
  }
  if (options.full && !options.timing_seed) {
    check_legality(tech, v.fp);
  }
  auto rc_options = options;
  if (options.timing_seed) {
    rc_options.resistance_ohm_um = 0;
    rc_options.capacitance_ff_um = 0;
  }
  result.parasitics = estimate_rc(v, tech, rc_options, result.pin_locations);
  for (const auto& c : v.cells) {
    result.parasitics->cells.push_back(
        {c.name, c.place.x, c.place.y, static_cast<unsigned>(c.place.orientation), c.macro, c.place.width, c.place.height});
  }
  for (const auto& t : v.fp.terminals) {
    result.parasitics->terminals.push_back({t.name, t.x, t.y});
  }
  graph.get_input_node().attr(attrs::placement_rc).set(ph::serialize_parasitics(*result.parasitics));
  fs::create_directories(options.output_dir);
  if (memories.empty()) {
    std::error_code error;
    fs::remove(options.output_dir + "/memory_proxies.lef", error);
  }
  if (!memories.empty()) {
    std::ofstream lef(options.output_dir + "/memory_proxies.lef");
    lef << "VERSION 5.8 ;\nBUSBITCHARS \"[]\" ;\nDIVIDERCHAR \"/\" ;\n";
    lef.precision(12);
    for (const auto& [id, name] : memories) {
      (void)id;
      const auto& macro = tech.macros.at(name);
      lef << "MACRO " << name << "\n CLASS BLOCK ;\n ORIGIN 0 0 ;\n SIZE " << macro.width << " BY " << macro.height << " ;\n";
      for (const auto& [pin_label, pin] : macro.pins) {
        lef << " PIN " << pin_label << "\n  DIRECTION " << pin.direction << " ;\n  USE SIGNAL ;\n  PORT\n";
        for (const auto& shape : pin.shapes) {
          const auto& r = shape.rect;
          lef << "   LAYER " << shape.layer << " ;\n   RECT " << r.xl << ' ' << r.yl << ' ' << r.xh << ' ' << r.yh << " ;\n";
        }
        lef << "  END\n END " << pin_label << "\n";
      }
      lef << "END " << name << "\n";
    }
    lef << "END LIBRARY\n";
    if (!lef) {
      throw std::runtime_error("cannot write memory proxy LEF");
    }
  }
  const auto def = options.output_dir + "/out.def";
  if (options.full && !options.timing_seed) {
    ph::write_def(v.fp, def);
    ph::verify_def_roundtrip(v.fp, ph::read_def(def));
  } else {
    std::error_code ec;
    fs::remove(def, ec);
  }
  result.qor = std::format(
      R"({{"schema_version":1,"kind":"placement","mode":"{}","legal":{},"gp_converged":{},"gp_overflow":{},"hpwl_um":{},"initial_hpwl_um":{},"cells":{},"nets":{},"estimated_macros":{},"timing_model":"placement_rc","timing_approximate":{},"rc_identity":"{}","incremental":{{"enabled":{},"hit":{},"reused_blocks":{},"changed_blocks":{}}}}})",
      options.timing_seed ? "post_synthesis_zero_wire"
      : options.full      ? "full"
                          : "gp",
      options.timing_seed ? "false"
      : options.full      ? "true"
                          : (metrics.legal ? "true" : "false"),
      metrics.converged ? "true" : "false",
      cache.hit ? "null" : std::format("{:.9g}", metrics.overflow),
      [&] {
        auto d = v.design;
        std::fill(d.net_weights.begin(), d.net_weights.end(), 1);
        return dp::hpwl(d) / tech.dbu;
      }(),
      initial_hpwl / tech.dbu,
      v.cells.size(),
      v.nets.size(),
      memories.size(),
      result.parasitics->approximate ? "true" : "false",
      result.parasitics->identity,
      cache_path.empty() ? "false" : "true",
      cache.hit ? "true" : "false",
      cache.reused,
      cache.changed);
  {
    rapidjson::Document json;
    json.Parse(result.qor.c_str());
    rapidjson::Document details;
    const auto          text = std::format(
        R"({{"used":{},"single_cell":{},"released_locks":{},"attempts":{},"moved_cells":{},"max_displacement_um":{},"window_dbu":[{},{},{},{}]}})",
        eco.used,
        eco.single_cell,
        eco.released_locks,
        eco.attempts,
        eco.moved,
        eco.max_displacement_um,
        eco.window.xl,
        eco.window.yl,
        eco.window.xh,
        eco.window.yh);
    details.Parse(text.c_str());
    json.AddMember("eco", rapidjson::Value(details, json.GetAllocator()), json.GetAllocator());
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    json.Accept(writer);
    result.qor = buffer.GetString();
  }
  {
    std::ofstream out(options.output_dir + "/qor.json");
    out << result.qor << '\n';
    if (!out) {
      throw std::runtime_error("cannot write placement QoR");
    }
  }
  if (options.full && !options.timing_seed) {
    save_cache(v, cache_path, env, hashes);
    if (!options.cache_dir.empty()) {
      ph::write_def(v.fp, options.cache_dir + "/floorplan.def");
    }
  }
  for (const auto& driver : v.drivers) {
    const auto name = driver.instance == "PIN" ? driver.pin : driver.instance + ":" + driver.pin;
    if (v.weights.contains(name)) {
      result.net_weights.emplace(name, v.weights.at(name));
    }
  }
  result.floorplan = std::move(v.fp);
  return result;
}
}  // namespace livehd::placement
