//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_label.hpp"
#include "eprp_utils.hpp"
#include "label_synth.hpp"
#include "label_mincut.hpp"
#include "label_acyclic.hpp"

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
  Eprp_method m1("pass.label.mincut", "Label a graph with mincut", &Pass_label::label_mincut);
  m1.add_label_optional("hier", "hierarchical traversal/labeling", "false");
  m1.add_label_optional("verbose", "verbose statistics and information", "false");
  register_pass(m1);

  Eprp_method m2("pass.label.synth", "Label a graph with synthesis boundaries", &Pass_label::label_synth);
  m2.add_label_optional("hier", "hierarchical traversal/labeling", "false");
  m2.add_label_optional("verbose", "verbose statistics and information", "false");
  register_pass(m2);

  Eprp_method m3("pass.label.acyclic", "Label a graph with aclyclic combinational calls", &Pass_label::label_acyclic);
  m3.add_label_optional("hier", "hierarchical traversal/labeling", "false");
  m3.add_label_optional("verbose", "verbose statistics and information", "false");
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

  Label_synth p(pp.verbose, pp.hier);

  for (const auto &l : var.lgs) {
    p.label(l);
  }
}

void Pass_label::label_acyclic(Eprp_var &var) {
  Pass_label pp(var);

  Label_acyclic p(pp.verbose, pp.hier);

  for (const auto &l : var.lgs) {
    p.label(l);
  }
}

