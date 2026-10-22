// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>
namespace warp {
using Clock = std::chrono::steady_clock;
double cross(struct Point, struct Point);
struct Point {
  double x = 0, y = 0;
  Point  operator+(Point p) const { return {x + p.x, y + p.y}; }
  Point  operator-(Point p) const { return {x - p.x, y - p.y}; }
  Point  operator*(double s) const { return {x * s, y * s}; }
};
struct Token {
  std::string text;
  size_t      begin = 0, end = 0;
};
struct Inst {
  std::string           name, master, orient;
  Point                 p, original;
  double                w = 0, h = 0;
  bool                  macro = false, fixed = false, standard = false, cover = false;
  size_t                xtoken = 0, ytoken = 0;
  int                   face = -1;
  std::array<double, 4> weights{};
};
struct Layout {
  std::string                      source;
  std::vector<Token>               tokens;
  std::vector<Inst>                insts;
  std::vector<std::vector<size_t>> nets;
  double                           dbu = 0, row_height = 0;
  Point                            lo, hi;
  double                           width() const { return hi.x - lo.x; }
  double                           height() const { return hi.y - lo.y; }
  Point                            normalize(Point p) const { return {(p.x - lo.x) / width(), (p.y - lo.y) / height()}; }
  Point                            physical(Point p) const { return {lo.x + p.x * width(), lo.y + p.y * height()}; }
};
struct Options {
  std::vector<std::string> lefs;
  std::string              input, output, report = "warp_report.png", metrics;
  int                      bins = 64, iters = 6, pixels = 1600;
  double                   rho = .85, mu = 1, lambda = -1, tol = .02, macro_factor = 2, time_limit = 30;
  double                   balance       = 0;
  std::string              balance_model = "area";
  std::string              replay;
  std::string              density_input;
  bool                     macro_space   = false;
  int                      smooth_window = 32;
  double                   smooth_blend  = .5;
  std::string              label;
  bool                     density_grid        = false;
  std::string              preview_grid        = "macro-edges";
  std::string              density_scale       = "auto";
  bool                     deformation_preview = false;
  bool                     render_only = false, require_cap = false, movable_macros = false;
};
struct Face {
  std::array<int, 4> v;
  int                macro  = -1;
  double             demand = 0, initial_area = 0, reserved = 0, target = -1;
};
struct Mesh {
  bool                             density_only = false;
  int                              nx = 0, ny = 0, reassigned = 0;
  std::vector<double>              xs, ys;
  std::vector<Point>               original, points;
  std::vector<Face>                faces;
  std::vector<std::pair<int, int>> edges;
  std::vector<size_t>              macros;
  std::vector<std::vector<int>>    macro_vertices;
  int                              vertex(int i, int j) const { return j * nx + i; }
  double area(const Face& f) const { return .5 * cross(points[f.v[2]] - points[f.v[0]], points[f.v[3]] - points[f.v[1]]); }
};
struct Pass {
  std::string axis, status;
  double      slack = 0, seconds = 0;
  int         slack_faces     = 0;
  double      objective_scale = 1;
};

double              length(Point);
void                require(bool, const std::string&);
void                invariants(const Mesh&, const Layout&);
Pass                solve_axis(Mesh&, const Layout&, const Options&, int);
double              face_density(const Mesh&);
double              coordinate(Point, int);
std::vector<Point>  clip_box(std::vector<Point>, Point, Point);
Point               sample_field(const Mesh&, Point);
std::vector<double> macro_occupancy(const Layout&, int);
std::vector<double> smooth_box(const std::vector<double>&, int, int);
struct Density_result {
  Layout              after;
  Mesh                mesh;
  std::vector<Pass>   passes;
  std::vector<double> occupied, free, target, target_density, out_mass, out_occupied, a, b;
  double              total = 0, out_total = 0, total_occupied = 0, transported_occupied = 0, seconds = 0;
};
// In-memory, atomic field solve. Layout instances are macro anchors; mass is
// standard-cell area per bin. No IO or rendering.
// Throws on invalid/infeasible data or solver limits; caller retains prior targets.
Density_result density_warp(const Layout&, const std::vector<double>& mass, const Options&);
}  // namespace warp
