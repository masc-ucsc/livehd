// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// A text that many nodes of one graph share, stored once.
//
// A synthesis color group or a placement block names a set of cells: a design has a few thousand of them
// and hundreds of thousands of cells. Each node holds a small id and the graph holds every text once, in a
// table on its input node. The text is the information; the id is only where a node points.
//
//   Color_group::text(node)        the text, or empty when the node has none
//   Color_group::has(node)
//   Color_group::set(node, text)   a few sets; searches the table
//   Color_group::copy(from, to)    same graph: copies the id. Another graph: goes through the text
//   Color_group::Writer w(graph)   many sets on one graph: keeps an index, writes the table once
//
// An id is local to its graph. Never copy the id attribute to a node of another graph: use copy().
//
// One Writer per graph at a time. While it is alive, read and write that graph's texts through it (its
// table is ahead of the stored one until flush()); flush() refuses to write if the stored table changed
// behind it.

#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "attrs.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

template <class Id_tag, class Names_tag>
struct Shared_text {
  using Table = std::vector<std::string>;

  [[nodiscard]] static const Table*     table_of(hhds::Graph* graph) { return graph->get_input_node().attr(Names_tag{}).try_get(); }
  [[nodiscard]] static uint32_t         id_of(const hhds::Node_class& node) { return node.attr(Id_tag{}).get_or(0); }
  [[nodiscard]] static bool             has(const hhds::Node_class& node) { return id_of(node) != 0; }
  // The view is into the graph's table: it holds until a text is added to that graph.
  [[nodiscard]] static std::string_view text(const hhds::Node_class& node) {
    const auto id = id_of(node);
    if (id == 0) {
      return {};
    }
    const auto* table = table_of(node.get_graph());
    if (table == nullptr || id > table->size()) {
      throw std::logic_error("shared text id without its text: the id was copied from another graph or read under a Writer");
    }
    return (*table)[id - 1];
  }
  static void clear(const hhds::Node_class& node) { node.attr(Id_tag{}).del(); }
  // `id` came from id_of() of a node of the SAME graph (that node may be gone by now).
  static void set_id(const hhds::Node_class& node, uint32_t id) {
    if (id != 0) {
      node.attr(Id_tag{}).set(id);
    }
  }
  // For a few nodes. A loop over the nodes of a graph uses a Writer.
  static void set(const hhds::Node_class& node, std::string_view text) {
    auto*       graph = node.get_graph();
    const auto* table = table_of(graph);
    if (table != nullptr) {
      for (size_t i = 0; i < table->size(); ++i) {
        if ((*table)[i] == text) {
          node.attr(Id_tag{}).set(static_cast<uint32_t>(i + 1));
          return;
        }
      }
    }
    Table grown = table == nullptr ? Table{} : *table;
    grown.emplace_back(text);
    const auto id = static_cast<uint32_t>(grown.size());
    graph->get_input_node().attr(Names_tag{}).set(std::move(grown));
    node.attr(Id_tag{}).set(id);
  }
  // `to` gets what `from` has (nothing, if `from` has none and `to` is in another graph or has none).
  static void copy(const hhds::Node_class& from, const hhds::Node_class& to) {
    const auto id = id_of(from);
    if (id == 0) {
      return;
    }
    if (from.get_graph() == to.get_graph()) {
      to.attr(Id_tag{}).set(id);
    } else {
      set(to, text(from));
    }
  }

  class Writer {
  public:
    explicit Writer(hhds::Graph* graph) : graph_(graph) {
      if (const auto* table = table_of(graph)) {
        for (const auto& stored : *table) {
          append(stored);
        }
      }
      loaded_ = texts_.size();
    }
    Writer(const Writer&)            = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() {
      try {
        flush();
      } catch (...) {  // NOLINT(bugprone-empty-catch): a destructor during unwinding; flush() explicitly to see the error
      }
    }
    [[nodiscard]] uint32_t id(std::string_view text) {
      if (const auto found = index_.find(text); found != index_.end()) {
        return found->second;
      }
      return append(text);
    }
    [[nodiscard]] std::string_view text(uint32_t id) const {
      return id == 0 ? std::string_view{} : std::string_view(texts_.at(id - 1));
    }
    [[nodiscard]] std::string_view text(const hhds::Node_class& node) const { return text(id_of(node)); }
    void                           set(const hhds::Node_class& node, std::string_view text) { node.attr(Id_tag{}).set(id(text)); }
    void                           set(const hhds::Node_class& node, uint32_t id) { node.attr(Id_tag{}).set(id); }
    [[nodiscard]] hhds::Graph*     graph() const { return graph_; }
    // Store the table if it grew.
    void                           flush() {
      if (texts_.size() == loaded_) {
        return;
      }
      const auto* stored = table_of(graph_);
      if ((stored == nullptr ? size_t{0} : stored->size()) != loaded_) {
        throw std::logic_error("shared text table changed under a Writer");
      }
      graph_->get_input_node().attr(Names_tag{}).set(Table(texts_.begin(), texts_.end()));
      loaded_ = texts_.size();
    }

  private:
    uint32_t append(std::string_view text) {
      const auto& stored = texts_.emplace_back(text);  // a deque never moves its elements: the index keys stay valid
      const auto  id     = static_cast<uint32_t>(texts_.size());
      index_.emplace(std::string_view(stored), id);
      return id;
    }
    hhds::Graph*                                    graph_;
    std::deque<std::string>                         texts_;
    absl::flat_hash_map<std::string_view, uint32_t> index_;
    size_t                                          loaded_ = 0;
  };

  // The ids of one source graph in a destination graph, for a copy of many nodes across graphs (a prefix can
  // scope every text on the way). Each distinct source text is looked up once.
  class Translator {
  public:
    Translator(Writer& to, std::string prefix = {}) : to_(to), prefix_(std::move(prefix)) {}
    // 0 when `from` has none.
    [[nodiscard]] uint32_t id(const hhds::Node_class& from) {
      const auto source = id_of(from);
      if (source == 0) {
        return 0;
      }
      auto& entry = by_source_[from.get_graph()];
      if (entry.size() < source) {
        entry.resize(source, 0);
      }
      if (entry[source - 1] == 0) {
        entry[source - 1]
            = prefix_.empty() ? to_.id(Shared_text::text(from)) : to_.id(prefix_ + std::string(Shared_text::text(from)));
      }
      return entry[source - 1];
    }
    void copy(const hhds::Node_class& from, const hhds::Node_class& to) {
      if (const auto translated = id(from); translated != 0) {
        to_.set(to, translated);
      }
    }

  private:
    Writer&                                                  to_;
    std::string                                              prefix_;
    absl::flat_hash_map<hhds::Graph*, std::vector<uint32_t>> by_source_;
  };
};

using Color_group     = Shared_text<livehd::attrs::synth_color_group_id_t, livehd::attrs::synth_color_group_names_t>;
using Placement_block = Shared_text<livehd::attrs::placement_block_id_t, livehd::attrs::placement_block_names_t>;

}  // namespace livehd::graph_util
