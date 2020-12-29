//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "gioc.hpp"
#include "lbench.hpp"
#include "lgcpp_plugin.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgtuple.hpp"
#include "pass_gioc.hpp"

#define TRACE(x)
//#define TRACE(x) x

Gioc::Gioc(std::string_view _path) : path(_path){}

//FIXME->sh: traverse whole graph just for searching sub-graph node? slow!?
void Gioc::do_trans(LGraph *lg) {
  for (auto node : lg->fast()) {
    if (node.get_type_op() == Ntype_op::Sub) {
      auto sub_name = node.get_type_sub_node().get_name();
      if (sub_name.substr(0, 6) == "__fir_") 
        continue; 
             
      auto *library      = Graph_library::instance(path);
      auto subg_paras    = split_name(node.get_name(), ":");
      auto &arg_tup_name = subg_paras[0];
      auto &ret_name     = subg_paras[1];
      auto &func_name    = subg_paras[2];

      Sub_node* sub;
      if (library->has_name(func_name)) {
        auto lgid = library->get_lgid(func_name);
        sub       = library->ref_sub(lgid);
      } else {
        Pass::error("Global IO connection pass cannot find existing subgraph {} in lgdb\n", func_name);
        return;
      }

      collect_tgs_from_unified_out(node);
      subgraph_io_connection(lg, sub, arg_tup_name, ret_name, node);
      reconnect_the_tgs_from_unified_out(ret_name);
      tgs_spins_from_unified_ta.clear();
    }
  }
}

void Gioc::collect_tgs_from_unified_out(Node subg_node) {
  I(subg_node.out_edges().size() == 1);
  auto unified_out_ta = subg_node.out_edges().begin()->sink.get_node();

  for (auto &e : unified_out_ta.out_edges()) {
    tgs_spins_from_unified_ta.emplace_back(e.sink);
    e.del_edge();
  }
}

void Gioc::reconnect_the_tgs_from_unified_out(std::string_view ret_name) {
  auto ret_ta_dpin = name2dpin[ret_name];
  for (auto &tg_spin : tgs_spins_from_unified_ta) {
    ret_ta_dpin.connect(tg_spin);
  }
}

void Gioc::subgraph_io_connection(LGraph *lg, Sub_node* sub, std::string_view arg_tup_name, std::string_view ret_name, Node subg_node) {
  // start query subgraph io and construct TGs for connecting inputs, TAs/scalar for connecting outputs
  for (const auto *io_pin : sub->get_io_pins()) {
    I(!io_pin->is_invalid());
    if (io_pin->name == "$" || io_pin->name == "%")
      continue;

    // I. io_pin is_input
    if (io_pin->is_input()) {
      std::vector<Node_pin> created_tup_gets;
      auto hier_inp_subnames = split_name(io_pin->name, ".");
      for (const auto& subname : hier_inp_subnames) {
        auto tup_get = lg->create_node(Ntype_op::TupGet);
        auto tn_spin = tup_get.setup_sink_pin("tuple_name"); 
        auto field_spin = tup_get.setup_sink_pin("field"); // key name

        Node_pin tn_dpin;
        if (&subname == &hier_inp_subnames.front()) {
          tn_dpin = setup_tuple_ref(lg, arg_tup_name);
        } else {
          tn_dpin = created_tup_gets.back();
        }

        tn_dpin.connect_sink(tn_spin);
        auto field_dpin = setup_field_dpin(lg, subname);
        field_dpin.connect_sink(field_spin);

        // note: for scalar input, front() == back()
        if (&subname == &hier_inp_subnames.back()) {
          auto subg_spin = subg_node.setup_sink_pin(io_pin->name); 
          tup_get.setup_driver_pin().connect_sink(subg_spin);
        }
        created_tup_gets.emplace_back(tup_get.get_driver_pin());
      }
      continue;
    }

    // II. hier-subgraph-output
    auto hier_inp_subnames = split_name(io_pin->name, ".");
    auto i = 0;
    for (const auto &subname : hier_inp_subnames) {
      if (i == 0) {
        // handle the TA of function call return and the TA of first subname
        auto ta_ret      = lg->create_node(Ntype_op::TupAdd);
        auto ta_ret_dpin = ta_ret.setup_driver_pin();

        auto ta_ret_tn_dpin = setup_tuple_ref(lg, ret_name);
        ta_ret_tn_dpin.connect_sink(ta_ret.setup_sink_pin("tuple_name"));

        auto ta_ret_field_dpin = setup_field_dpin(lg, subname);
        ta_ret_field_dpin.connect_sink(ta_ret.setup_sink_pin("field"));

        name2dpin[ret_name] = ta_ret_dpin;
        ta_ret_dpin.set_name(ret_name);


        if (hier_inp_subnames.size() == 1) {
          auto subg_dpin = subg_node.setup_driver_pin(io_pin->name);
          subg_dpin.connect_sink(ta_ret.setup_sink_pin("value"));
          break;
        } else {
          I(hier_inp_subnames.size() > 1); 
          auto ta_subname      = lg->create_node(Ntype_op::TupAdd);
          auto ta_subname_dpin = ta_subname.setup_driver_pin();

          auto ta_subname_tn_dpin = setup_tuple_ref(lg, subname);
          ta_subname_tn_dpin.connect_sink(ta_subname.setup_sink_pin("tuple_name"));
        // note: we don't know the field and value for ta_subname yet till next subname

          ta_subname_dpin.connect_sink(ta_ret.setup_sink_pin("value")); //connect to parent value_dpin
          name2dpin[subname] = ta_subname_dpin;
          ta_subname_dpin.set_name(subname);
          i++;
        } 
      } else if (i == (int)(hier_inp_subnames.size() - 1)) {
        auto parent_field_dpin = setup_field_dpin(lg, subname);
        auto parent_subname    = hier_inp_subnames[i-1];
        auto ta_hier_parent    = name2dpin[parent_subname].get_node();
        parent_field_dpin.connect_sink(ta_hier_parent.setup_sink_pin("field"));
        auto subg_dpin = subg_node.setup_driver_pin(io_pin->name);
        subg_dpin.connect_sink(ta_hier_parent.setup_sink_pin("value"));

      } else { //the middle ones, if any
        I(hier_inp_subnames.size() >= 3);
        auto ta_subname         = lg->create_node(Ntype_op::TupAdd);
        auto parent_subname     = hier_inp_subnames[i-1];
        auto ta_hier_parent    = name2dpin[parent_subname].get_node();
        auto parent_field_dpin  = setup_field_dpin(lg, subname);

        auto ta_subname_tn_dpin = setup_tuple_ref(lg, subname);
        ta_subname_tn_dpin.connect_sink(ta_subname.setup_sink_pin("tuple_name"));
        // note: we don't know the field and value for ta_subname yet till next subname

        auto ta_subname_dpin = ta_subname.setup_driver_pin();
        ta_subname_dpin.connect_sink(ta_hier_parent.setup_sink_pin("value"));
        parent_field_dpin.connect_sink(ta_hier_parent.setup_sink_pin("field"));

        name2dpin[subname] = ta_subname_dpin;
        ta_subname_dpin.set_name(subname);
        i++;
      }
    }
  }
}


bool Gioc::subgraph_outp_is_tuple(Sub_node* sub) {
  uint16_t outp_cnt = 0;
  for (const auto *io_pin : sub->get_io_pins()) {
    if (io_pin->is_output()) 
      outp_cnt ++;
    
    if (outp_cnt > 1)
      return true;
  }
  return false;
}


std::vector<std::string_view> Gioc::split_name(std::string_view hier_name, std::string_view delimiter) {
  auto start = 0u;
  auto end = hier_name.find(delimiter);
  std::vector<std::string_view> token_vec;
  while (end != std::string_view::npos) {
    std::string_view token = hier_name.substr(start, end - start);
    token_vec.emplace_back(token);
    start = end + 1; //
    end = hier_name.find(delimiter, start);
  }
  std::string_view token = hier_name.substr(start, end - start);

  token_vec.emplace_back(token);
  return token_vec;
}


Node_pin Gioc::setup_tuple_ref(LGraph *lg, std::string_view ref_name) {
  auto it = name2dpin.find(ref_name);

  if (it != name2dpin.end())
    return it->second;

  Node_pin dpin;
  dpin = Node_pin::find_driver_pin(lg, ref_name);
  if (!dpin.is_invalid()) {
    return dpin;
  }

  dpin = lg->create_node(Ntype_op::TupRef).setup_driver_pin();
  dpin.set_name(ref_name);
  name2dpin[ref_name] = dpin;
  return dpin;
}

Node_pin Gioc::setup_field_dpin(LGraph *lg, std::string_view field_name) {
  auto it = field2dpin.find(field_name);
  if (it != field2dpin.end()) {
    return it->second;
  }

  auto dpin = lg->create_node(Ntype_op::TupKey).setup_driver_pin();
  dpin.set_name(field_name);
  field2dpin[field_name] = dpin;

  return dpin;
}

