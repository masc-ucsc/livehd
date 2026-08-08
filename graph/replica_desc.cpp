// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "replica_desc.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>

#include "iassert.hpp"
#include "node_util.hpp"

namespace livehd::graph_util {

namespace {

// first + ordinal*step with a hard overflow check (the domain is i64 and a
// descriptor that would wrap is rejected rather than silently aliasing two
// ordinals onto one index).
bool index_at_checked(int64_t first, int64_t step, uint64_t ordinal, int64_t& out) {
  if (ordinal > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  const auto r = static_cast<int64_t>(ordinal);
  int64_t    scaled = 0;
  if (__builtin_mul_overflow(r, step, &scaled)) {
    return false;
  }
  return !__builtin_add_overflow(first, scaled, &out);
}

// Bits needed to represent `v` in two's complement, including the sign bit.
int signed_bits_of(int64_t v) {
  if (v == 0) {
    return 1;
  }
  if (v > 0) {
    int b = 1;  // sign bit
    while (v != 0) {
      v >>= 1;
      ++b;
    }
    return b;
  }
  // Smallest w with v >= -2^(w-1); walk up from 2 bits.
  int b = 2;
  while (b < 64 && v < -(static_cast<int64_t>(1) << (b - 1))) {
    ++b;
  }
  return b;
}

std::optional<int64_t> parse_i64(std::string_view s) {
  int64_t    v     = 0;
  const bool neg   = !s.empty() && s.front() == '-';
  const auto body  = neg ? s.substr(1) : s;
  if (body.empty()) {
    return std::nullopt;
  }
  const auto res = std::from_chars(body.data(), body.data() + body.size(), v);
  if (res.ec != std::errc{} || res.ptr != body.data() + body.size()) {
    return std::nullopt;
  }
  return neg ? -v : v;
}

std::optional<uint64_t> parse_u64(std::string_view s) {
  uint64_t   v   = 0;
  const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
  if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
    return std::nullopt;
  }
  return v;
}

}  // namespace

std::optional<int64_t> Replica_desc::index_at(uint64_t ordinal) const {
  int64_t out = 0;
  if (!index_at_checked(first, step, ordinal, out)) {
    return std::nullopt;
  }
  return out;
}

int Replica_desc::index_signed_bits() const {
  if (count == 0) {
    return 1;
  }
  const auto lo = index_at(0);
  const auto hi = index_at(count - 1);
  if (!lo || !hi) {
    return 64;
  }
  return std::max(signed_bits_of(*lo), signed_bits_of(*hi));
}

bool Replica_desc::is_carry_source(hhds::Port_id output_pid) const {
  return std::ranges::any_of(carries, [output_pid](const auto& c) { return c.output_pid == output_pid; });
}

bool Replica_desc::is_carry_dest(hhds::Port_id input_pid) const {
  return std::ranges::any_of(carries, [input_pid](const auto& c) { return c.input_pid == input_pid; });
}

std::string Replica_desc::validate() const {
  if (step == 0) {
    return "step is zero (an infinite domain)";
  }
  // Every generated index must be representable; check the last ordinal, which
  // is the extreme one for either sign of step.
  if (count > 0 && !index_at(count - 1)) {
    return std::format("domain first={} step={} count={} overflows int64", first, step, count);
  }

  for (const auto& c : carries) {
    if (c.input_pid == livehd::Port_invalid || c.output_pid == livehd::Port_invalid) {
      return "a carry declares an invalid source or destination port";
    }
  }
  // Duplicate carry DESTINATIONS are invalid (two carries would fight for one
  // input). One output feeding several carries is legal.
  for (std::size_t i = 0; i < carries.size(); ++i) {
    for (std::size_t j = i + 1; j < carries.size(); ++j) {
      if (carries[i].input_pid == carries[j].input_pid) {
        return std::format("duplicate carry destination pid {}", static_cast<uint64_t>(carries[i].input_pid));
      }
    }
  }

  // A role-marked port may not double as a carry endpoint: the index and
  // activation inputs are supplied per occurrence by realization, so a carry
  // writing them has no meaning.
  if (index_input && is_carry_dest(*index_input)) {
    return "the index input is also a carry destination";
  }
  if (activation_input && is_carry_dest(*activation_input)) {
    return "the activation input is also a carry destination";
  }
  // `next_active` is consumed by the activation chain, not by a carry.
  if (next_active_output && is_carry_source(*next_active_output)) {
    return "the next_active output is also a carry source";
  }
  // Chaining activation is only meaningful when there is an activation input
  // to chain into.
  if (next_active_output && !activation_input) {
    return "next_active is declared without an activation input to chain into";
  }
  return {};
}

std::string Replica_desc::serialize() const {
  std::string out = std::format("version={};first={};step={};count={}", Replica_desc_version, first, step, count);
  if (index_input) {
    out += std::format(";idx={}", static_cast<uint64_t>(*index_input));
  }
  if (activation_input) {
    out += std::format(";act={}", static_cast<uint64_t>(*activation_input));
  }
  if (next_active_output) {
    out += std::format(";nact={}", static_cast<uint64_t>(*next_active_output));
  }
  if (!carries.empty()) {
    out += ";carry=";
    bool first_c = true;
    for (const auto& c : carries) {
      if (!first_c) {
        out += ",";
      }
      first_c = false;
      out += std::format("{}>{}", static_cast<uint64_t>(c.output_pid), static_cast<uint64_t>(c.input_pid));
    }
  }
  return out;
}

std::optional<Replica_desc> replica_desc_from_string(std::string_view txt, std::string* err) {
  const auto fail = [&](std::string msg) -> std::optional<Replica_desc> {
    if (err != nullptr) {
      *err = std::move(msg);
    }
    return std::nullopt;
  };

  Replica_desc desc;
  bool         saw_version = false;
  bool         saw_first = false, saw_step = false, saw_count = false;

  std::size_t pos = 0;
  while (pos <= txt.size()) {
    const auto       semi = txt.find(';', pos);
    const std::string_view field = txt.substr(pos, semi == std::string_view::npos ? std::string_view::npos : semi - pos);
    pos                          = (semi == std::string_view::npos) ? txt.size() + 1 : semi + 1;
    if (field.empty()) {
      continue;
    }
    const auto eq = field.find('=');
    if (eq == std::string_view::npos) {
      return fail(std::format("malformed replica descriptor field `{}`", field));
    }
    const auto key = field.substr(0, eq);
    const auto val = field.substr(eq + 1);

    if (key == "version") {
      const auto v = parse_u64(val);
      if (!v) {
        return fail(std::format("replica descriptor version `{}` is not a number", val));
      }
      if (*v != Replica_desc_version) {
        return fail(std::format(
            "replica descriptor version {} is not supported (this build understands version {}); regenerate the graph",
            *v,
            Replica_desc_version));
      }
      saw_version = true;
    } else if (!saw_version) {
      return fail("replica descriptor does not start with its version field");
    } else if (key == "first") {
      const auto v = parse_i64(val);
      if (!v) {
        return fail(std::format("replica descriptor first=`{}` is not an integer", val));
      }
      desc.first = *v;
      saw_first  = true;
    } else if (key == "step") {
      const auto v = parse_i64(val);
      if (!v) {
        return fail(std::format("replica descriptor step=`{}` is not an integer", val));
      }
      desc.step = *v;
      saw_step  = true;
    } else if (key == "count") {
      const auto v = parse_u64(val);
      if (!v) {
        return fail(std::format("replica descriptor count=`{}` is not a number", val));
      }
      desc.count = *v;
      saw_count  = true;
    } else if (key == "idx" || key == "act" || key == "nact") {
      const auto v = parse_u64(val);
      if (!v || *v >= static_cast<uint64_t>(livehd::Port_invalid)) {
        return fail(std::format("replica descriptor {}=`{}` is not a valid port id", key, val));
      }
      const auto pid = static_cast<hhds::Port_id>(*v);
      if (key == "idx") {
        desc.index_input = pid;
      } else if (key == "act") {
        desc.activation_input = pid;
      } else {
        desc.next_active_output = pid;
      }
    } else if (key == "carry") {
      std::size_t cp = 0;
      while (cp <= val.size()) {
        const auto comma = val.find(',', cp);
        const auto item  = val.substr(cp, comma == std::string_view::npos ? std::string_view::npos : comma - cp);
        cp               = (comma == std::string_view::npos) ? val.size() + 1 : comma + 1;
        if (item.empty()) {
          continue;
        }
        const auto gt = item.find('>');
        if (gt == std::string_view::npos) {
          return fail(std::format("malformed replica carry `{}` (expected <out>><in>)", item));
        }
        const auto o = parse_u64(item.substr(0, gt));
        const auto i = parse_u64(item.substr(gt + 1));
        if (!o || !i || *o >= static_cast<uint64_t>(livehd::Port_invalid) || *i >= static_cast<uint64_t>(livehd::Port_invalid)) {
          return fail(std::format("replica carry `{}` has a bad port id", item));
        }
        desc.carries.emplace_back(Replica_carry{static_cast<hhds::Port_id>(*i), static_cast<hhds::Port_id>(*o)});
      }
    } else {
      // Unknown keys are REJECTED, not skipped: silently dropping a field a
      // newer writer considered meaningful is exactly the stale-artifact
      // failure the version check exists to prevent.
      return fail(std::format("unknown replica descriptor field `{}`", key));
    }
  }

  if (!saw_version) {
    return fail("replica descriptor has no version field");
  }
  if (!saw_first || !saw_step || !saw_count) {
    return fail("replica descriptor is missing first/step/count");
  }
  if (auto msg = desc.validate(); !msg.empty()) {
    return fail(std::format("invalid replica descriptor: {}", msg));
  }
  return desc;
}

// Deliberately ATTRIBUTE PRESENCE, not parseability. A payload this build
// cannot read (a newer version, a corrupt string) must make every guard REFUSE
// — answering false there is the exact "read a compact node as one ordinary
// instance" outcome the version field exists to prevent, and it is silent.
// The consumer that needs the contents (expand_replicated_subs) reports the
// parse error itself.
bool is_replicated_sub(const hhds::Node_class& node) {
  if (node.is_invalid() || !is_type_sub(node)) {
    return false;
  }
  return node.attr(livehd::attrs::replica_desc).has();
}

std::optional<Replica_desc> replica_desc_of(const hhds::Node_class& node, std::string* err) {
  if (node.is_invalid() || !is_type_sub(node)) {
    return std::nullopt;
  }
  auto a = node.attr(livehd::attrs::replica_desc);
  if (!a.has()) {
    return std::nullopt;
  }
  return replica_desc_from_string(a.get(), err);
}

void set_replica_desc(const hhds::Node_class& node, const Replica_desc& desc) {
  I(is_type_sub(node), "a replica descriptor only belongs on a Sub node");
  I(desc.validate().empty(), "set_replica_desc on an invalid descriptor");
  node.attr(livehd::attrs::replica_desc).set(desc.serialize());
}

void del_replica_desc(const hhds::Node_class& node) {
  auto a = node.attr(livehd::attrs::replica_desc);
  if (a.has()) {
    a.del();
  }
}

bool graph_has_replicated_subs(hhds::Graph* g) {
  if (g == nullptr) {
    return false;
  }
  for (auto n : g->fast_class()) {
    if (is_replicated_sub(n)) {
      return true;
    }
  }
  return false;
}

}  // namespace livehd::graph_util
