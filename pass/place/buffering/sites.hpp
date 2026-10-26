// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace livehd::placement::buffering {
// All coordinates are integer database units. Rows describe single-height
// placement sites, not routing capacity. Touching rectangle edges are legal.
struct Site_rect {
  int64_t xl = 0, yl = 0, xh = 0, yh = 0;
  bool    operator==(const Site_rect&) const = default;
};
struct Site_row {
  std::string name;
  Site_rect   body;
  int64_t     pitch                             = 0;
  bool        operator==(const Site_row&) const = default;
};
struct Site_span {
  std::string row;
  uint64_t    first = 0, count = 0;
  bool        operator==(const Site_span&) const = default;
};
struct Row_sites {
  Site_row               row;
  std::vector<Site_span> gaps;
  bool                   operator==(const Row_sites&) const = default;
};
using Legal_sites = std::map<std::string, Row_sites>;

// Exclusions include every fixed body/blockage. Their union, after an explicit
// halo plus legalization margin, blocks intersecting WHOLE sites once. The
// result is compressed contiguous gaps; no capacity is inferred for wires.
// Invalid/overlapping rows and coordinate overflow throw before returning.
Legal_sites build_legal_sites(const std::vector<Site_row>& rows, const std::vector<Site_rect>& exclusions, int64_t halo = 0,
                              int64_t legalization_margin = 0);
Site_rect   site_body(const Legal_sites&, const Site_span&);
// Maximum nonoverlapping equal-width single-row footprints, rather than the
// number of possible starts (which overstates narrow-gap capacity).
uint64_t    buffer_slots(const Legal_sites&, uint64_t width_in_sites);
// Whether two spans share at least one site of the same row.
bool        spans_overlap(const Site_span&, const Site_span&);

enum class Reservation_status { reserved, unavailable, conflict };
struct Reservation_result {
  Reservation_status status = Reservation_status::reserved;
  std::string        owner;
};
// Exact shared capacity for virtual buffer footprints. A replacement is atomic:
// a failed proposal retains ALL prior reservations. Multi-site buffers reserve
// every occupied site, so overlapping candidate starts never create capacity.
// Copying the allocator is a complete reservation snapshot for rollback. The
// caller still validates library/electrical/routing and full physical legality.
class Site_reservations {
  Legal_sites                                   sites;
  std::map<std::string, std::vector<Site_span>> owners;
  Reservation_result                            conflict(const std::string& owner, const std::vector<Site_span>& sorted) const;

public:
  explicit Site_reservations(Legal_sites legal_sites);
  Reservation_result replace(const std::string& owner, std::vector<Site_span> proposal);
  // The status replace() would return, without changing any reservation.
  Reservation_result check(const std::string& owner, std::vector<Site_span> proposal) const;
  void               release(const std::string& owner);
  // Passing the current owner permits reuse of its prior footprints during a
  // replacement. Other owners' reserved sites remain unavailable.
  Legal_sites        available_sites(const std::string& replacing_owner = {}) const;
  const auto&        reservations() const { return owners; }
  const Legal_sites& geometry() const { return sites; }
};
}  // namespace livehd::placement::buffering
