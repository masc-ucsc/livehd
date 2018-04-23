
#include <string>
#include <fstream>

#include "bitscan/bitscan.h"
#include "inou_yaml.hpp"

#include "lgraph.hpp"
#include "lgedgeiter.hpp"

#define DEBUG_FULL_YAML 1

Inou_yaml::Inou_yaml() {

}

Inou_yaml_options_pack::Inou_yaml_options_pack() {

  Options::get_desc()->add_options()
    ("yaml_output,o", boost::program_options::value(&yaml_output), "yaml output <filename> for graph")
    ("yaml_input,i" , boost::program_options::value(&yaml_input ), "yaml input <filename> for graph")
    ;

  boost::program_options::variables_map vm;
  boost::program_options::store(boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(*Options::get_desc()).allow_unregistered().run(), vm);

  if (vm.count("yaml_output")) {
    yaml_output = vm["yaml_output"].as<std::string>();
  }else{
    yaml_output = "output.yaml";
  }

  if (vm.count("yaml_input") && graph_name != "") {
    console->error("inou_yaml can only have a yaml_input or a graph_name, not both\n");
    exit(-3);
  }

  if (vm.count("yaml_input")) {
    yaml_input = vm["yaml_input"].as<std::string>();
  }else{
    yaml_input = "input.yaml";
  }

  console->info("inou_yaml yaml_output:{} yaml_input:{} graph_name:{}", yaml_output, yaml_input, graph_name);
}

std::vector<LGraph *> Inou_yaml::generate() {

  std::vector<LGraph *> lgs;

  if (opack.graph_name!="") {
    lgs.push_back(new LGraph(opack.lgdb_path, opack.graph_name, false)); // Do not clear
    // No need to sync because it is a reload. Already sync
  }else{
    assert(opack.yaml_input != "");

    lgs.push_back(new LGraph(opack.lgdb_path));
    from_yaml(lgs[0], opack.yaml_input);
    lgs[0]->sync();
  }

  return lgs;
}

void Inou_yaml::generate(std::vector<const LGraph *> out) {
  if (out.size() == 1) {
    to_yaml(out[0], opack.yaml_output);
  }else{
    for(const auto &g:out) {
      std::string file = g->get_name() + "_" + opack.yaml_output;
      to_yaml(g, file);
    }
  }
}

void Inou_yaml::to_yaml(const LGraph *g, const std::string filename) const {

  YAML::Emitter out;
  out << YAML::BeginSeq;

  bitarray visited(g->size());

  for(auto idx:g->backward()) {

    assert(g->is_root(idx));

    out << YAML::Value << idx;
    {
      out << YAML::BeginMap;
#ifdef DEBUG_FULL_YAML
      out << YAML::Key << "inputs";
      {
        out << YAML::BeginSeq;
        for(const auto &c:g->inp_edges(idx)) {
          out << YAML::Key << (int)c.get_inp_pin().get_pid();
          {
            out << YAML::BeginMap;

            out << YAML::Key << "nid";
            out << YAML::Value << (int)c.get_idx();
            out << YAML::EndMap;
          }
        }

        out << YAML::EndSeq;
      }
#endif
      out << YAML::Key << "op";
      if(g->node_type_get(idx).op == U32Const_Op) {
        out << YAML::Value << g->node_value_get(idx);
      } else if(g->node_type_get(idx).op == StrConst_Op) {
        out << YAML::Value << "'" + g->node_const_value_get(idx);
      } else {
        out << YAML::Value << g->node_type_get(idx).get_name();
      }
      if (g->is_graph_input(idx)) {
        out << YAML::Key << "input_name";
        out << YAML::Value << g->get_graph_input_name(idx);
      }
      if (g->is_graph_output(idx)) {
        out << YAML::Key << "output_name";
        out << YAML::Value << g->get_graph_output_name(idx);
      }
      if (g->get_wid(idx) != 0) {
        out << YAML::Key << "wirename";
        out << YAML::Value << g->get_node_wirename(idx);
      }
      out << YAML::Key << "outputs";
      {
        out << YAML::BeginSeq;

        std::vector<const Edge *> s;
        for(const auto &c:g->out_edges(idx)) {
          s.push_back(&c);
        }

#if 0
        // Not worth the effort
        std::sort(s.begin(), s.end(), [](const Edge *a, const Edge *b) {
            if (b->is_root() && !a->is_root())
              return false;
            if (!b->is_root() && a->is_root())
              return true;
            if (b->get_idx() == a->get_idx())
              return b->get_out_pin().get_pid() > a->get_out_pid();
            return b->get_idx() > a->get_idx();
            });
#endif

        for(const auto &c:s) {

          out << YAML::Key << (int)c->get_out_pin().get_pid();
          {
            out << YAML::BeginMap;

            out << YAML::Key   << "idx";
            out << YAML::Value << (int)c->get_idx();
            //assert(c->get_idx()!=66);

            if (c->get_inp_pin().get_pid()) {
              out << YAML::Key   << "pid";
              out << YAML::Value << (int)c->get_inp_pin().get_pid();
            }

            if(c->is_root()) {
              auto node = g->get_dest_node(*c);

              if (!visited.is_bit(node.get_nid())) {
                float delay = node.delay_get();
                if (delay!=0) {
                  out << YAML::Key   << "delay";
                  out << YAML::Value << delay;
                }
                int bits = c->get_bits();
                if (bits!=0) {
                  out << YAML::Key   << "bits";
                  out << YAML::Value << bits;
                }
              }
              visited.set_bit(node.get_nid());
            }

            out << YAML::EndMap;
          }
        }

        out << YAML::EndSeq;
      }
      out << YAML::EndMap;
    }
  }
  out << YAML::EndSeq;

  if (filename == "-" || filename == "") {
    std::cout << out.c_str() << std::endl;
  }else{
    std::fstream fs;
    fs.open(filename, std::ios::out | std::ios::trunc);
    if(!fs.is_open()) {
      std::cerr << "ERROR: could not open yaml file [" << filename << "] for writing\n";
      exit(-4);
    }
    fs << out.c_str() << std::endl;
    fs.close();
  }
}

void Inou_yaml::from_yaml_outputs(LGraph *g, Index_ID nid, const YAML::Node nlist) {
  Port_ID last_eid = 0;

  for(const auto &node:nlist) {

    if(node.Type() == YAML::NodeType::Scalar) {
      last_eid = std::stoi(node.as<std::string>());
    }else if(node.Type() == YAML::NodeType::Map) {
      if (!node["idx"]) {
        std::cerr << "Missing idx entry at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
        exit(1);
      }
      Index_ID dst_nid = node["idx"].as<int>(); // Required

      if (yaml_remap.find(dst_nid) == yaml_remap.end()) {
        yaml_remap[dst_nid] = g->create_node().get_nid();
      }
      dst_nid = yaml_remap[dst_nid];

      Port_ID out_pid = 0;
      if (node["pid"]) {
        out_pid = node["pid"].as<int>();   // Optional
      }
      Node_Pin src_pin(nid    , out_pid , false);
      Node_Pin dst_pin(dst_nid, last_eid, true );
      if (node["bits"]) {
        int bits = node["bits"].as<int>(); // Optional
        //fmt::print("src_nid:{} src_pid:{} dst_nid:{} dst_pid:{} bits:{}\n",src_pin.get_nid(), src_pin.get_pid(), dst_pin.get_nid(), dst_pin.get_pid(), bits);
        g->add_edge(src_pin, dst_pin, bits);
      }else{
        g->add_edge(src_pin, dst_pin);
      }
    }else{
      std::cerr << "Unexpected entry at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
      exit(-1);
    }
  }
}

void Inou_yaml::from_yaml_delay(LGraph *g, Index_ID nid, const YAML::Node node) {
  float delay = node.as<double>();

  g->node_delay_set(nid, delay);
}


// FIXME: anyway of doing this checks more efficiently?
bool Inou_yaml::is_int(std::string s) {
    return !s.empty() && std::find_if(s.begin(),
        s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}

// FIXME: anyway of doing this checks more efficiently?
bool Inou_yaml::is_const(std::string s) {
    return !s.empty() && s[0] == '\'' && std::find_if(s.substr(1).begin(),
        s.end(), [](char c) { return !(c != '0' && c != '1' && c != 'x' && c != 'z'); }) == s.end();
}


void Inou_yaml::from_yaml_op(LGraph *g, Index_ID nid, const YAML::Node node) {
	std::string op = node.as<std::string>();

	if (Node_Type::is_type(op)) {
    g->node_type_set(nid, Node_Type::get(op));
	} else if(is_int(op)) {
    //FIXME: (nasty) there is no stou function, so we need one extra step
    uint64_t val = std::stoul(op);
    assert(val < (uint32_t)-1);
    g->node_u32type_set(nid, (uint32_t) val);
	} else if (is_const(op)){
    g->node_const_type_set(nid, op.substr(1));
  } else {
    // sub graphs and blackboxes
  }
}

void Inou_yaml::from_yaml_input_name(LGraph *g, Index_ID nid, const YAML::Node node) {
  std::string name = node.as<std::string>();

  g->add_graph_input(name.c_str(),nid);
}

void Inou_yaml::from_yaml_output_name(LGraph *g, Index_ID nid, const YAML::Node node) {
  std::string name = node.as<std::string>();

  g->add_graph_output(name.c_str(),nid);
}

void Inou_yaml::from_yaml(LGraph *g, const std::string filename) {

  std::ifstream f(filename.c_str());
  if (!f.good()) {
    std::cerr << "ERROR: could not open yaml file [" << filename << "] for reading\n";
    exit(-4);
  }
  f.close();

  if (!g->empty()) {
    std::cerr << "ERROR: could not call from_yaml file [" << filename << "] for an existing open graph\n";
    exit(-4);
  }

  YAML::Node yfile;
  try {
    yfile = YAML::LoadFile(filename);
  } catch(YAML::ParserException& e) {
    std::cerr << "ERROR: invalid yaml parsing\n";
    std::cerr << e.what() << "\n";
    exit(0);
  }

  uint64_t last_nid = 0;

  yaml_remap.clear();

  for(const auto &node:yfile) {

    if(node.Type() == YAML::NodeType::Scalar) {
      last_nid = std::stoi(node.as<std::string>());
    }else if(node.Type() == YAML::NodeType::Map) {

      if (yaml_remap.find(last_nid) == yaml_remap.end()) {
        // Not always because it may be created out of order with the input list
        yaml_remap[last_nid] = g->create_node().get_nid();
      }
      last_nid = yaml_remap[last_nid];

      for(YAML::const_iterator it2=node.begin();it2!=node.end();++it2) {
        std::string key = it2->first.as<std::string>();
        if (key == "outputs") {
          from_yaml_outputs(g, last_nid, it2->second);
        }else if (key == "delay") {
          from_yaml_delay(g, last_nid, it2->second);
        }else if (key == "op") {
          from_yaml_op(g, last_nid, it2->second);
        }else if (key == "input_name") {
          from_yaml_input_name(g, last_nid, it2->second);
        }else if (key == "output_name") {
          from_yaml_output_name(g, last_nid, it2->second);
        }else if (key == "inputs") {
          // Nothing to do, just extra redundant dump option
        }else{
          std::cerr << "Unknown [" << key << "] field for nid: " << last_nid;
          std::cerr << " at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
          exit(-1);
        }
      }
    }else{
      switch (node.Type()) {
        case YAML::NodeType::Null:
          std::cerr << "Null\n";
          break;
        case YAML::NodeType::Scalar: // ...
          std::cerr << "scalar \n";
          break;
        case YAML::NodeType::Sequence: // ...
          std::cerr << "sequence\n";
          break;
        case YAML::NodeType::Map: // ...
          std::cerr << "map\n";
          break;
        case YAML::NodeType::Undefined: // ...
          std::cerr << "undefined\n";
          break;
      }
      std::cerr << "Unexpected entry at line " << node.Mark().line << " col " << node.Mark().column << std::endl;
      exit(-1);
    }
  }
  yaml_remap.clear();
}
