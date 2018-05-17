//
// Created by birdeclipse on 1/24/18.
//
#include "./inou/yosys/dump_yosys.hpp"
#include "inou.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "options.hpp"
#include "tech_library.hpp"
#include <string>

#ifndef LGRAPH_INOU_BLIF_HPP
#define LGRAPH_INOU_BLIF_HPP

#define MAX_WIRENAME_LENGTH 255

class Inou_blif_options_pack : public Options_pack {
public:
  Inou_blif_options_pack();

  std::string blif_output;
  std::string verbose;
};

class Inou_blif : public Inou {
protected:
  Inou_blif_options_pack opack;

public:
  struct index_bits {
    Index_ID idx;
    int      bits[2];
  };

  struct KeyHash {
    std::size_t operator()(const Index_ID &k) const {
      return k;
    }
  };

  Inou_blif();

  virtual ~Inou_blif();

  std::vector<LGraph *> generate() final;

  using Inou::generate;

  void generate(std::vector<const LGraph *> &out) final;

private:
  std::vector<Index_ID> Comb_Op_ID;
  std::vector<Index_ID> Latch_Op_ID;
  std::vector<Index_ID> GraphIO_input_ID;
  std::vector<Index_ID> GraphIO_output_ID;

  bool is_latch(const Tech_cell *tcell) {
    std::string cell_name = tcell->get_name();
    std::string flop      = "FF";
    std::string latch     = "LATCH";
    if(cell_name.find(flop) != std::string::npos) {
      return true;
    } else if(cell_name.find(latch) != std::string::npos) {
      return true;
    } else
      return false;
  }

  void find_cell_conn(const LGraph *g);

  void recursive_find(const LGraph *g, const Edge *input, std::vector<index_bits> &pid, int *bit_addr);

  bool is_techmap(const LGraph *g);

  using node_conn = std::unordered_map<Index_ID, std::vector<index_bits>, KeyHash>;
  using wire_name = std::unordered_map<Index_ID, std::string, KeyHash>;
  node_conn comb_conn;
  node_conn latch_conn;
  node_conn primary_output_conn;
  wire_name wirename;

  void to_blif(const LGraph *g, const std::string filename);

  void gen_module(const LGraph *g, std::ofstream &fs);

  void gen_io_conn(const LGraph *g, std::ofstream &fs);

  void gen_const_conn(const LGraph *g, std::ofstream &fs);

  void gen_cell_conn(const LGraph *g, std::ofstream &fs);

  void gen_latch_conn(const LGraph *g, std::ofstream &fs);
};

#endif //LGRAPH_INOU_BLIF_HPP
