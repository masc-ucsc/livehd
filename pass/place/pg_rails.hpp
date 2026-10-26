// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// PG rail parity of multi-row CORE cells, the rule OpenROAD's check_placement
// applies (DPL "placed in rows"): a standard-cell row has the edge rails of its
// site's one-row cells, swapped when the row is flipped (FS, S); a k-row master
// spans k rows and each of its k + 1 local row edges that carries a PG rail
// must land on a row edge of the same polarity. A two-row master therefore
// fits only starts whose bottom rail is its own bottom rail: with alternating
// N/FS rows, every other start. A one-row cell flips with its row and always
// fits.
enum class Pg_rail : uint8_t { none, power, ground };
std::string_view                      pg_rail_name(Pg_rail);
// Polarity of each local row edge of a CORE master, bottom (y = 0) up to its
// height: the USE (POWER or GROUND) of a PG pin with a routing-layer shape
// centered on that edge, none where no PG shape is centered. Empty for a
// master that is not a whole number (at least one) of its SITE's rows. Two
// polarities on one edge throw std::invalid_argument.
std::vector<Pg_rail>                  master_edge_rails(const physical::Technology&, const physical::Macro&);
// Bottom and top rail of an N row of `site`: the edge rails every one-row CORE
// master on the site with distinct rails on both edges agrees on. nullopt when
// no master has them or two disagree (the parity is then unknown).
std::optional<std::array<Pg_rail, 2>> site_row_rails(const physical::Technology&, const std::string& site);
enum class Rail_fit : uint8_t { fit, mismatch, unknown };
// Rail checks over one technology, caching the masters' and sites' rails.
class Pg_rail_model {
public:
  explicit Pg_rail_model(const physical::Technology& technology) : tech(technology) {}
  // Whether `master` in orientation o, its bottom on the first of `rows` (the
  // orientations of the core rows it spans, bottom up), puts every edge rail
  // on a row edge of its polarity. unknown when the master has no edge rail
  // or its site's row rails are unknown; mismatch also for a span length
  // other than the master's rows or a rotated orientation. `why` (optional)
  // names the first mismatching edge.
  Rail_fit                    fit(const std::string& master, physical::Orientation o, std::span<const physical::Orientation> rows,
                                  std::string* why = nullptr) const;
  // The master's edge rails (cached; see master_edge_rails).
  const std::vector<Pg_rail>& edges(const std::string& master) const;
  // The site's N-row rails (cached; see site_row_rails).
  const std::optional<std::array<Pg_rail, 2>>& site(const std::string& name) const;

private:
  const physical::Technology&                                          tech;
  mutable std::map<std::string, std::vector<Pg_rail>>                  masters;
  mutable std::map<std::string, std::optional<std::array<Pg_rail, 2>>> sites;
};
}  // namespace livehd::placement
