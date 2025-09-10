// See LICENSE for details

#include "inou_lefdef.hpp"

#include "absl/strings/str_split.h"
#include "def2lg.hpp"
#include "inou_def.hpp"
#include "lef2lg.hpp"
#include "perf_tracing.hpp"

static Pass_plugin sample("inou_lefdef", Inou_lefdef::setup);

Inou_lefdef::Inou_lefdef(const Eprp_var &var) : Pass("inou.lefdef", var) { lgdb = var.get("lgdb"); }

void Inou_lefdef::setup() {
  Eprp_method m1("inou.lefdef", "read lef/def to lgraph", &Inou_lefdef::to_lg);

  register_inou("lefdef", m1);
}

void Inou_lefdef::to_lg(Eprp_var &var) {
  TRACE_EVENT("inou", "lefdef");

  std::vector<Lgraph *> lgs;

  auto files = var.get("files");

  Inou_lefdef pp(var);
  for (const auto &f : absl::StrSplit(files, ',')) {
    if (str_tools::ends_with(f, ".def")) {
      auto new_lgs = pp.parse_def(f);
      lgs.insert(lgs.end(), new_lgs.begin(), new_lgs.end());
    } else {
      Pass::error("unsupported format file {}", f);
      return;
    }
  }
}

std::vector<Lgraph *> Inou_lefdef::parse_def(std::string_view def_file) {
  std::print("process {}\n", def_file);

  std::vector<Lgraph *> lgs;
#if 1
  Def_info dinfo;
  // def_parsing(dinfo, def_file);

  for (auto &&io : dinfo.ios) {
    std::print("io.name:{}\n", io.io_name);
  }

#else
  // clear since loading from def
  auto *g = Lgraph_create(opack.lgdb_path, dinfo.mod_name, opack.def_file);

  const Tech_library &tlib            = g->get_tlibrary();
  const int           cell_types_size = tlib.get_cell_types_size();

  std::unordered_map<std::string, Node> ht_comp2node;

  // set up node place coordinate and node type
  for (auto iter_compo = dinfo.compos.begin(); iter_compo != dinfo.compos.end();
       ++iter_compo) {  // iterate over all components in def file
    Node compo_node                = g->create_node();
    ht_comp2node[iter_compo->name] = compo_node;
    // comment out for IWLS18
    // g->node_place_set(compo_nid, iter_compo->posx, iter_compo->posy);
    // cout<< "node_place of nid " << compo_nid << " is " << g->get_x(compo_nid) << " " << g->get_y(compo_nid) << endl;
    for (uint16_t cell_id = 0; cell_id < cell_types_size; cell_id++) {  // decide component's cell_type
      auto cell_type_name = tlib.get_const_cell(cell_id)->get_name();
      if (iter_compo->macro_name == cell_type_name) {
        compo_node.set_type_tmap_id(cell_id);  // node nid's cell type is cell_id.
      }
    }
    // const Tech_cell* cell_type = tlib.get_const_cell(g->tmap_id_get(compo_nid));
    // cout << "node_type of nid "  << compo_nid << " is " << cell_type->get_name()<< endl;
    // cout << "total pin nunber is " << cell_type->get_pins_size() << endl;
    // for(int i = 0 ; i< cell_type->get_pins_size(); ++i)
    //    cout << "has pin " << cell_type->get_name(i) << " with direction " << cell_type->get_direction(i) << endl;
    // cout << endl;
  }  // end outer for

  Node cf_node = g->create_node();                                    // cf = chip_frame_node
  for (uint16_t cell_id = 0; cell_id < cell_types_size; cell_id++) {  // decide chip_frame_node's cell_type
    auto cell_type_name = tlib.get_const_cell(cell_id)->get_name();
    if (cell_type_name == "chip_frame") {
      cf_node.set_type_tmap_id(cell_id);  // node nid's cell type is cell_id.
    }
  }

  for (auto iter_net = dinfo.nets.begin(); iter_net != dinfo.nets.end(); ++iter_net) {
    Node                 src_node;
    Port_ID              src_pid;
    std::vector<Node>    dst_nodes;
    std::vector<Port_ID> dst_pids;

    // deal with a net without drive/drived relationship, such as a net between memory D pin and a cell pin {(MEM D[1]) (inst533
    // A1)}
    bool has_drive = false;

    // since the src_node will not appear in a determined order, we have parse all the connections in a net first, record src and
    // dst nid and pids, then create our edges
    for (auto iter_conn = iter_net->conns.begin(); iter_conn != iter_net->conns.end();
         ++iter_conn) {                      // determine src nid/pid and dst nids/pids
      if (iter_conn->compo_name == "PIN") {  // compo type is an io pin
        const Tech_cell *cell_type = tlib.get_const_cell((uint16_t)cf_node.get_type_tmap_id());
        Port_ID          pid       = cell_type->get_pin_id(iter_conn->pin_name);
        if (cell_type->is_output(iter_conn->pin_name)) {
          src_node = cf_node;
          src_pid  = pid;
        } else {
          dst_nodes.push_back(cf_node);
          dst_pids.push_back(pid);
        }
      } else {  // compo type is not an io pin, i.e. regular cell
        Node             compo_node = ht_comp2node[iter_conn->compo_name];
        const Tech_cell *cell_type  = tlib.get_const_cell((uint16_t)compo_node.get_type_tmap_id());
        Port_ID          pid        = cell_type->get_pin_id(iter_conn->pin_name);

        if (cell_type->is_output(iter_conn->pin_name)) {
          src_node  = compo_node;  // for each net, there will be only 1 drive and N-fanout
          src_pid   = pid;
          has_drive = true;
        } else {
          dst_nodes.push_back(compo_node);
          dst_pids.push_back(pid);
        }
      }
    }  // end iter_conn

    // if(has_drive == false){
    //  //corner case: deal with a net without drive/drived relationship, such as a net between memory D pin and a cell pin {(MEM
    //  D[1]) (inst533 A1)}
    //    src_nid = dst_nids.back();
    //    src_pid = dst_pids.back();
    //    dst_nids.pop_back();
    //    dst_pids.pop_back();
    //}

    if (has_drive) {
      for (size_t i = 0; i < dst_nodes.size(); ++i) {
        // create N lgraph edge"s" to connect N fanout from 1 src pin
        assert(!src_node.is_invalid());
        assert(!dst_nodes[i].is_invalid());
        Node_pin dst_pin = dst_nodes[i].setup_sink_pin(dst_pids[i]);
        Node_pin src_pin = src_node.setup_driver_pin(src_pid);
        g->add_edge(src_pin, dst_pin);
      }
    }
  }

  g->sync();
  lgs.push_back(g);

#endif
  return lgs;
}
