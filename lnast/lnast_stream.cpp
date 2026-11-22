//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnast_stream.hpp"

#include <format>

namespace livehd::lnast_stream {

void Tree_sink::begin(const std::shared_ptr<Lnast>& header) {
  out_ = std::make_shared<Lnast>(header->get_top_module_name());
  out_->set_lambda_kind(header->get_lambda_kind());
  out_->set_verilog_origin(header->is_verilog_origin());
  const auto root = out_->set_root(Lnast_ntype::create_top());
  for (auto child : header->children(header->get_root())) {
    if (Lnast_ntype::is_stmts(header->get_type(child))) {
      stmts_ = out_->add_child(root, Lnast_ntype::create_stmts());
    } else {
      header->copy_subtree(child, *out_, root);
    }
  }
}

void Tree_sink::stmt(const std::shared_ptr<Lnast>& tree, const Lnast_nid& s) { tree->copy_subtree(s, *out_, stmts_); }

Sink& Chain::link(Sink& final_sink) {
  Sink* downstream = &final_sink;
  for (auto it = passes_.rbegin(); it != passes_.rend(); ++it) {
    (*it)->set_next(downstream);
    downstream = it->get();
  }
  return *downstream;
}

bool parse_chain(std::string_view names, Chain& chain, std::string& error) {
  while (!names.empty()) {
    const auto comma = names.find(',');
    const auto name  = names.substr(0, comma);
    names            = comma == std::string_view::npos ? std::string_view{} : names.substr(comma + 1);
    if (name.empty()) {
      continue;
    }
    auto pass = make_pass(name);
    if (!pass) {
      error = std::format("unknown LNAST stream pass '{}' (known: balance)", name);
      return false;
    }
    chain.add(std::move(pass));
  }
  return true;
}

}  // namespace livehd::lnast_stream
