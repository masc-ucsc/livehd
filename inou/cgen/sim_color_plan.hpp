// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::sim {

// Read-only occurrence-wide discovery for the simulator color schedule.
//
// This is deliberately a separate object from Cgen_sim.  It is built only
// after the simulator-private graph library has completed every structural
// rewrite, and its occurrence handles must never survive another rewrite.
// Fine-grain state-version sites ride on top of discovery. Coarsening and the
// final control/derived-clock cases remain explicit report status instead of
// pretending a base node has one phase when it may execute at several versions.
class Color_plan {
public:
  static constexpr size_t invalid_index = static_cast<size_t>(-1);

  enum class Site_kind : uint8_t {
    data,
    state,
    instance,
    conditional_control,
    loop_control,
  };

  enum class Dependency_kind : uint8_t {
    data,
    state_read,
    state_update,
    control,
    loop_carry,
  };

  enum class State_version : uint8_t {
    pre_rise,
    post_rise,
    post_fall,
  };

  enum class Execution_slot : uint8_t {
    pre_rise_eval,
    rise_commit,
    post_rise_eval,
    fall_commit,
    post_fall_publish,
  };

  enum class Version_role : uint8_t {
    data,
    state_read,
    state_update,
  };

  enum class Boundary_kind : uint8_t {
    color_value,
    top_input,
    top_output,
    observation_input,
    observation_output,
    state_current,
    state_pending,
  };

  struct Site {
    std::string           structural_id;
    std::string           storage_id;  // occurrence-unique ABI identity; excluded from kernel reuse
    Site_kind             kind = Site_kind::data;
    hhds::Occurrence_node node;
    uint64_t              gate_equivalents = 0;
    bool                  live             = false;
  };

  struct Dependency {
    size_t          producer      = 0;
    size_t          consumer      = 0;
    hhds::Port_id   producer_port = 0;
    hhds::Port_id   consumer_port = 0;
    Dependency_kind kind          = Dependency_kind::data;
    bool            cut           = false;
  };

  struct Observation {
    std::string name;
    std::string structural_id;
    bool        input = false;
    uint32_t    port  = 0;
  };

  struct Version_site {
    std::string    structural_id;
    size_t         base_site       = 0;
    // Outermost structural conditional region containing this occurrence.
    // Coarsening may never cross this boundary: doing so would erase the
    // activation contract and force an otherwise idle region to execute.
    size_t         control_owner   = invalid_index;
    hhds::Port_id  output_port     = 0;  // data versions may split a multi-output node by protocol port
    State_version  version         = State_version::pre_rise;
    Execution_slot slot            = Execution_slot::pre_rise_eval;
    Version_role   role            = Version_role::data;
    uint64_t       execution_order = 0;
  };

  struct Version_dependency {
    size_t   producer      = 0;
    size_t   consumer      = 0;
    uint64_t boundary_bits = 0;
  };

  // Exact value-flow record retained alongside the precedence-only version
  // DAG. A single version dependency may cover several of these uses, while
  // state-transition precedence has no Value_use at all.
  struct Value_use {
    size_t        producer_version    = invalid_index;
    size_t        consumer_version    = invalid_index;
    State_version version             = State_version::pre_rise;
    hhds::Port_id producer_port       = 0;
    uint32_t      producer_shift      = 0;  // position an LSB-aligned slice in the consumer's whole word
    uint32_t      producer_extract_lo = 0;  // non-empty [lo,hi): store only this fixed source lane
    uint32_t      producer_extract_hi = 0;
    hhds::Port_id consumer_port       = 0;
    uint32_t      consumer_input      = 0;  // exact inp_edges() position; ports may be variadic
    uint32_t      width               = 1;  // physical producer/storage width
    uint32_t      consumer_width      = 1;  // width after an erased GraphIO boundary cast
    bool          unsign              = false;
    bool          top_input           = false;
    bool          preextracted        = false;  // consumer Get_mask is the identity on this lane
  };

  struct Boundary_consumer {
    size_t        version_site = invalid_index;
    size_t        color        = invalid_index;
    hhds::Port_id port         = 0;
    uint32_t      input        = 0;  // exact inp_edges() position within the consumer
    uint32_t      width        = 1;  // consumer-visible width after boundary casting
    bool          preextracted = false;
  };

  // Direct color ABI. Each slot has exactly one writer. For color values the
  // producer color owns storage; current and pending state slots are owned by
  // the state occurrence (one pending next-value slot, not one per operand);
  // top I/O slots are public root storage. Consumers bind directly to a slot --
  // they never call another module's settle/eval routine.
  struct Boundary_slot {
    std::string                    structural_id;
    Boundary_kind                  kind                = Boundary_kind::color_value;
    State_version                  version             = State_version::pre_rise;
    size_t                         owner_site          = invalid_index;
    size_t                         producer_version    = invalid_index;
    size_t                         producer_color      = invalid_index;
    hhds::Port_id                  producer_port       = 0;
    uint32_t                       producer_shift      = 0;
    uint32_t                       producer_extract_lo = 0;
    uint32_t                       producer_extract_hi = 0;
    hhds::Port_id                  public_port         = 0;
    uint32_t                       width               = 1;
    bool                           unsign              = false;
    std::string                    literal;  // non-empty only for an observation alias driven by a constant
    std::vector<Boundary_consumer> consumers;
  };

  struct Color {
    std::string         structural_id;
    Execution_slot      slot = Execution_slot::pre_rise_eval;
    std::vector<size_t> members;
    uint64_t            gate_equivalents = 0;
    uint64_t            execution_order  = 0;
  };

  struct Color_dependency {
    size_t   producer      = 0;
    size_t   consumer      = 0;
    uint64_t boundary_bits = 0;
  };

  struct Kernel_class {
    std::string         signature;
    size_t              representative = invalid_index;
    std::vector<size_t> colors;
  };

  struct Summary {
    uint64_t grouped_sites             = 0;
    uint64_t outer_sites               = 0;
    uint64_t live_sites                = 0;
    uint64_t physical_occurrence_sites = 0;
    uint64_t compact_loops             = 0;
    uint64_t conditional_regions       = 0;
    uint64_t carry_edges_cut           = 0;
    // Version edges dropped because producer == consumer. A precedence edge from
    // a site to ITSELF is UNSATISFIABLE, not merely vacuous: it is a genuine
    // self-dependency that reached version identity. Dropping it keeps the Kahn
    // stall diagnostic readable; the plan is still failed (versioning_complete =
    // false, one witness in errors_) so the module is refused rather than
    // silently scheduled against a stale value.
    // MEASURED 2026-08-18: no design has produced one (XiangShan `Backend` is 0).
    uint64_t self_edges_dropped        = 0;
    uint64_t version_sites             = 0;
    uint64_t version_edges             = 0;
    uint64_t fine_colors               = 0;
    uint64_t colors                    = 0;
    uint64_t color_merges              = 0;
    uint64_t value_uses                = 0;
    uint64_t boundary_slots            = 0;
    uint64_t boundary_bits             = 0;
    uint64_t kernel_classes            = 0;
    uint64_t kernel_reuses             = 0;
    bool     boundary_one_writer       = true;
    bool     boundary_dominance        = true;
    bool     runtime_random            = false;
    bool     complete                  = true;
    bool     versioning_complete       = true;
    bool     version_dag_acyclic       = true;
    bool     color_dag_acyclic         = true;
  };

  Color_plan() = default;

  // `root` must already be fully prepared.  The plan holds lazy occurrence
  // handles, so structurally mutating root's library afterwards is a contract
  // violation caught by HHDS's mutation-epoch assertion in debug builds.
  static Color_plan discover(hhds::Graph* root, bool include_observations = true);

  [[nodiscard]] const std::vector<Site>&                sites() const noexcept { return sites_; }
  [[nodiscard]] const std::vector<Dependency>&          dependencies() const noexcept { return dependencies_; }
  [[nodiscard]] const std::vector<Observation>&         observations() const noexcept { return observations_; }
  [[nodiscard]] const std::vector<Version_site>&        version_sites() const noexcept { return version_sites_; }
  [[nodiscard]] const std::vector<Version_dependency>&  version_dependencies() const noexcept { return version_dependencies_; }
  [[nodiscard]] const std::vector<Value_use>&           value_uses() const noexcept { return value_uses_; }
  [[nodiscard]] const std::vector<Boundary_slot>&       boundary_slots() const noexcept { return boundary_slots_; }
  [[nodiscard]] const std::vector<Color>&               colors() const noexcept { return colors_; }
  [[nodiscard]] const std::vector<Color_dependency>&    color_dependencies() const noexcept { return color_dependencies_; }
  [[nodiscard]] const std::vector<Kernel_class>&        kernel_classes() const noexcept { return kernel_classes_; }
  // Per color, version-site indices in the canonical rank order used by its
  // verified kernel class. Equal kernel signatures therefore have an exact,
  // name-free member correspondence for generated ABI binding.
  [[nodiscard]] const std::vector<std::vector<size_t>>& canonical_members() const noexcept { return canonical_members_; }
  [[nodiscard]] const Summary&                          summary() const noexcept { return summary_; }
  [[nodiscard]] const std::vector<std::string>&         errors() const noexcept { return errors_; }
  [[nodiscard]] bool                                    complete() const noexcept { return summary_.complete; }

  // Re-resolves lazy edges after construction.  Besides checking the retained
  // handles, this pins the policy-lifetime contract: moving a Color_plan must
  // not leave HHDS's function_ref pointing at a builder-local lambda.
  [[nodiscard]] bool validate_retained_handles() const;

  // Stable, line-oriented bring-up artifact.  Raw Gid/Nid/Class_index values
  // and user names are excluded from schedule identity; names occur only in
  // the final observation-map section.
  [[nodiscard]] std::string report() const;
  void                      write_report(std::string_view path) const;

private:
  using Policy = std::function<hhds::Instance_action(const hhds::Instance_site&)>;

  // Heap allocation is intentional. Hierarchy_policy is function_ref, so the
  // callable address -- not merely the Color_plan object -- must remain stable
  // across return-value moves and later lazy edge resolution.
  std::shared_ptr<Policy> outer_policy_;
  std::shared_ptr<Policy> discovery_policy_;

  std::vector<hhds::Occurrence_node> outer_nodes_;
  std::vector<Site>                  sites_;
  std::vector<Dependency>            dependencies_;
  std::vector<Observation>           observations_;
  std::vector<Version_site>          version_sites_;
  std::vector<Version_dependency>    version_dependencies_;
  std::vector<Value_use>             value_uses_;
  std::vector<Boundary_slot>         boundary_slots_;
  std::vector<Color>                 colors_;
  std::vector<Color_dependency>      color_dependencies_;
  std::vector<Kernel_class>          kernel_classes_;
  std::vector<std::vector<size_t>>   canonical_members_;
  Summary                            summary_;
  std::vector<std::string>           errors_;
};

}  // namespace livehd::sim
