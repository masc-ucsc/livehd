
#include "graph_core.hpp"
#include "graph_library.hpp"


Graph_core::Graph_core(std::string_view path, std::string_view name) {
  Graph_core::table_next_free = 0;
  Graph_core::overflow_next_free = 0;
}

void Graph_core::add_edge(const Index_ID sink_id, const Index_ID driver_id) {
  Graph_core::Entry16* sink = &table.at(sink_id);
  Graph_core::Entry16* driver = &table.at(driver_id);

  int8_t d_to_s = sink_id - driver_id;
  int8_t s_to_d = driver_id - sink_id;

  size_t sink_index = sink->available_index();
  size_t driver_index = driver->available_index();

  size_t max_edges = std::size(sink->edge_storage);

  if (sink_index < max_edges && driver_index < max_edges) {
    sink->edge_storage[sink_index] = s_to_d;
    driver->edge_storage[driver_index] = d_to_s;
  } else {
    //go to overflow
    sink->set_overflow(1);
    driver->set_overflow(1);
    Entry64* e = new Entry64(driver_id, sink_id);
    overflow.push_back(*e);
  }
}

void Graph_core::del_edge(const Index_ID sink_id, const Index_ID driver_id) {
  Graph_core::Entry16* sink = &table.at(sink_id);
  Graph_core::Entry16* driver = &table.at(driver_id);

  int8_t d_to_s = sink_id - driver_id;
  int8_t s_to_d = driver_id - sink_id;

  size_t sink_index = sink->index_of_val(s_to_d);
  size_t driver_index = driver->index_of_val(d_to_s);

  size_t max_edges = std::size(sink->edge_storage);

  if (sink_index < max_edges && driver_index < max_edges) {
    sink->edge_storage[sink_index] = 0;
    driver->edge_storage[driver_index] = 0;
  } else {
    bool d_other = false;
    bool s_other = false;
    for (int i = 0; i < overflow.size(); i++) {
      Entry64 e = overflow.at(i);
      if(e.driver == driver_id && e.sink == sink_id)
        overflow.erase(overflow.begin() + i);
      if(e.driver == driver_id && e.sink != sink_id)
        d_other = true;
      if(e.driver != driver_id && e.sink == sink_id)
        s_other = true;
    }
    if (!d_other)
      table.at(driver_id).set_overflow(0);
    if (!s_other)
      table.at(sink_id).set_overflow(0);
  }
}

Index_ID Graph_core::create_master_root(uint8_t type) {
  Entry16 mr;
  mr.pid_bits_or_type = type;
  mr.set_master_root();

  auto it = table.begin() + Graph_core::get_next_free();

  it = table.insert(it, mr);
  table_next_free = table_next_free + 1;

  return it - table.begin();
}

std::vector<Index_ID> Graph_core::get_edges(Index_ID s) {
  std::vector<Index_ID> edges;
  for (int i = 0; i < std::size(table.at(s).edge_storage); i++) {
    uint8_t delta = table.at(s).edge_storage[i];
    if (delta != 0) {
      Index_ID other_node = (delta + s) % 256;
      edges.push_back(other_node);
    }
  }
  if (table.at(s).get_overflow() == 1) {
    for (Entry64 e : overflow)
    {
      if (e.driver == s || e.sink == s) {
        if(e.driver != s)
          edges.push_back(e.driver);
        else
          edges.push_back(e.sink);
      }
    }
  }

  return edges;
}

void Graph_core::del(const Index_ID s) {
  std::vector<Index_ID> edges = Graph_core::get_edges(s);
  for (Index_ID e : edges) {
    //Entry16* node = &(table.at(e));
    del_edge(e, s);
    del_edge(s, e);
    //node->edge_storage[node->index_of_val(s - e)] = 0;
  }

  table.at(s).set_writable();
  for (int i = 0; i < std::size(table.at(s).edge_storage); i++)
    table.at(s).edge_storage[i] = 0;
}
