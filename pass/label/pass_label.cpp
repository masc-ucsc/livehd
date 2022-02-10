//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_label.hpp"

#include "eprp_utils.hpp"
#include "label_acyclic.hpp"
#include "label_mincut.hpp"
#include "label_synth.hpp"

static Pass_plugin sample("pass_label", Pass_label::setup);

Pass_label::Pass_label(const Eprp_var &var) : Pass("pass.label", var) {
  
  auto hier_txt = var.get("hier");

  if (hier_txt != "false" && hier_txt != "0")
    hier = true;
  else
    hier = false;

  auto verbose_txt = var.get("verbose");

  if (verbose_txt != "false" && verbose_txt != "0")
    verbose = true;
  else
    verbose = false;
}

void Pass_label::setup() {
  Eprp_method m1(mmap_lib::str("pass.label.mincut"), mmap_lib::str("Label a graph with mincut"), &Pass_label::label_mincut);
  m1.add_label_optional("hier", mmap_lib::str("hierarchical traversal/labeling"), "false");
  m1.add_label_optional("verbose", mmap_lib::str("verbose statistics and information"), "false");
  register_pass(m1);

  Eprp_method m2(mmap_lib::str("pass.label.synth"), mmap_lib::str("Label a graph with synthesis boundaries"), &Pass_label::label_synth);
  m2.add_label_optional("hier", mmap_lib::str("hierarchical traversal/labeling"), "false");
  m2.add_label_optional("alg", mmap_lib::str("algorithm (pipe, synth)"), "pipe");
  m2.add_label_optional("verbose", mmap_lib::str("verbose statistics and information"), "false");
  register_pass(m2);

  Eprp_method m3(mmap_lib::str("pass.label.acyclic"), mmap_lib::str("Label a graph with aclyclic combinational calls"), &Pass_label::label_acyclic);
  m3.add_label_optional("hier", mmap_lib::str("hierarchical traversal/labeling"), "false");
  m3.add_label_optional("cutoff", mmap_lib::str("small partition node count cutoff"), "1");
  m3.add_label_optional("merge", mmap_lib::str("enables merging of acyclic partitions"), "false");
  m3.add_label_optional("verbose", mmap_lib::str("verbose statistics and information"), "false");
  register_pass(m3);
}

void Pass_label::label_mincut(Eprp_var &var) {
  Pass_label pp(var);

  Label_mincut p(pp.verbose, pp.hier);

  for (const auto &l : var.lgs) {
    p.label(l);
  }
}

void Pass_label::label_synth(Eprp_var &var) {
  Pass_label pp(var);

  auto alg_txt = var.get("alg");

  Label_synth p(pp.verbose, pp.hier, alg_txt);

  for (const auto &l : var.lgs) {
    p.label(l);
  }
}

void Pass_label::label_acyclic(Eprp_var &var) {
  Pass_label pp(var);
  
  // cutoff_str will be type mmap_lib::str
  auto cutoff_str = var.get("cutoff");
  auto cutoff = static_cast<uint8_t>(std::stoi(cutoff_str.to_s()));

  auto merge_txt = var.get("merge");
  bool merge_en = false;
  if (merge_txt != "false" && merge_txt != "0") merge_en = true;

  Label_acyclic p(pp.verbose, pp.hier, cutoff, merge_en);

  for (const auto &l : var.lgs) {
    p.label(l);
  }
}
