// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Load Liberty corners into the fast timing tables and report: load time, size, what was left out, and the
// agreement of the pooled single-precision lookup with liberty::timing::Table::lookup on random queries.
//
//   cell_types_probe NAME=FILE[,FILE...] [NAME=FILE[,FILE...] ...]
//
// A corner is one or more Liberty files (a `.gz` file is read through zcat); the files of one corner are
// concatenated, so the cells of a split library (asap7's AO, INVBUF, OA, SIMPLE, SEQ) form one corner.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "cell_types.hpp"

namespace fast = livehd::timing::fast;
namespace lt   = livehd::liberty::timing;

namespace {
std::string read_file(const std::string& path) {
  const bool  gz   = path.size() > 3 && path.substr(path.size() - 3) == ".gz";
  FILE*       file = gz ? popen(("zcat '" + path + "'").c_str(), "r") : fopen(path.c_str(), "rb");
  std::string text;
  if (file == nullptr) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    std::exit(2);
  }
  char   buffer[1 << 16];
  size_t got;
  while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, got);
  }
  gz ? pclose(file) : fclose(file);
  return text;
}
double seconds(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}
}  // namespace

int main(int argc, char** argv) {
  std::vector<lt::Library> libraries;
  std::vector<std::string> names;
  size_t                   text_bytes = 0;
  const auto               start      = std::chrono::steady_clock::now();
  for (int i = 1; i < argc; ++i) {
    const std::string spec = argv[i];
    const auto        eq   = spec.find('=');
    if (eq == std::string::npos) {
      std::fprintf(stderr, "usage: cell_types_probe NAME=FILE[,FILE...] ...\n");
      return 2;
    }
    std::string text;
    for (size_t at = eq + 1; at <= spec.size();) {
      auto comma = spec.find(',', at);
      if (comma == std::string::npos) {
        comma = spec.size();
      }
      text += read_file(spec.substr(at, comma - at));
      text += '\n';
      at    = comma + 1;
    }
    text_bytes += text.size();
    names.push_back(spec.substr(0, eq));
    libraries.push_back(lt::parse(text));
  }
  const double                    parse_s = seconds(start);
  std::vector<const lt::Library*> pointers;
  size_t                          model_cells = 0;
  for (const auto& library : libraries) {
    pointers.push_back(&library);
    model_cells += library.cells.size();
  }
  const auto   build_start = std::chrono::steady_clock::now();
  const auto   types       = fast::Cell_types::build(pointers, names);
  const double build_s     = seconds(build_start);
  std::printf("corners %zu, liberty text %.1f MB, parse %.2f s, build %.2f s\n", names.size(), text_bytes / 1e6, parse_s, build_s);
  std::printf("types %zu (cells in the reference corner %zu), tables %zu, axes %zu, table values %zu\n",
              types.types(),
              libraries[0].cells.size(),
              types.table_count(),
              types.axis_count(),
              types.value_count());
  std::printf("pooled tables: %.2f MB (%.1f KB per cell type); ignored arcs %zu; diagnostics %zu\n",
              types.bytes() / 1e6,
              types.bytes() / 1e3 / std::max<size_t>(1, types.types()),
              types.ignored_arcs(),
              types.diagnostics().size());
  for (size_t i = 0; i < types.diagnostics().size() && i < 6; ++i) {
    std::printf("  diag: %s\n", types.diagnostics()[i].c_str());
  }

  // Agreement with the library tables.
  std::mt19937                           random(1);
  std::uniform_real_distribution<double> slew(0, 400), load(0, 200);
  double                                 worst_abs = 0, worst_rel = 0;
  size_t                                 queries       = 0;
  const auto                             compare_start = std::chrono::steady_clock::now();
  double                                 lookup_s      = 0;
  for (size_t k = 0; k < libraries.size(); ++k) {
    for (const auto& [name, cell] : libraries[k].cells) {
      const auto type = types.find(name);
      if (type == fast::no_index) {
        continue;
      }
      for (const auto& [pin_name, pin] : cell.pins) {
        const auto local = types.find_pin(type, pin_name);
        const auto arcs  = types.arcs_into(type, local);
        // Arcs kept by the build are a prefix-compatible subset: match them in order, skipping ignored ones.
        size_t     a     = 0;
        for (const auto& reference : pin.arcs) {
          if (a >= arcs.size()) {
            break;
          }
          if (arcs[a].from != types.find_pin(type, reference.related_pin)) {
            continue;
          }
          for (unsigned group = 0; group < 3; ++group) {
            for (unsigned t = 0; t < 2; ++t) {
              const auto& table = group == 0 ? reference.delay[t] : group == 1 ? reference.slew[t] : reference.constraint[t];
              const auto  id    = types.table(arcs[a], k, group, t);
              if (!table || id == fast::no_index) {
                continue;
              }
              for (int q = 0; q < 8; ++q) {
                const double s = slew(random), l = load(random), r = slew(random), c = slew(random);
                const double expected
                    = table->lookup({.input_slew_ps = s, .output_load_ff = l, .related_slew_ps = r, .constrained_slew_ps = c});
                const auto   t0 = std::chrono::steady_clock::now();
                const double got
                    = types.lookup(id,
                                   {static_cast<float>(s), static_cast<float>(l), static_cast<float>(r), static_cast<float>(c)});
                lookup_s  += seconds(t0);
                worst_abs  = std::max(worst_abs, std::abs(got - expected));
                worst_rel  = std::max(worst_rel, std::abs(got - expected) / std::max(1.0, std::abs(expected)));
                ++queries;
              }
            }
          }
          ++a;
        }
      }
    }
  }
  std::printf("parity: %zu lookups, worst absolute error %.4g ps, worst relative %.3g (compare took %.2f s)\n",
              queries,
              worst_abs,
              worst_rel,
              seconds(compare_start));
  std::printf("lookup cost (with timer overhead): %.0f ns\n", queries ? lookup_s / queries * 1e9 : 0.0);
  (void)model_cells;
  return 0;
}
