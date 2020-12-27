
# Semantic Pass Function Descriptions

## Type Check Functions

<br>

`is_primitive_op()`
* **Parameters**: LNAST node-type
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-type is a primitive-type (e.g. logical, unary, n-ary operations). Otherwise, returns false.

`is_tree_structs()`
* **Parameters**: LNAST node-type
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-type is a tree-structure-type (e.g. if, for, while statement). Otherwise, returns false.

`is_temp_var()`
* **Parameters**: LNAST node-name
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name is a temporary variable often denoted by 3 underscores ("____"). Otherwise, returns false.

`is_a_number()`
* **Parameters**: LNAST node-name
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name is an integer. Otherwise, returns false.

<br>

---

<br>

## Existence Check Functions

| Parameter | Description |
| ------- | ----------- |
| `FlatHashMap write_dict` | Holds written-to-variables as keys and statement names they belong to as values |
| `FlatHashMap read_dict` | Holds read-from-variables as keys and statement names they belong to as values |
| `FlatHashSet inefficient_LNAST` | Holds inefficient LNAST node-names |
| `FlatHashSet output_vars` | Holds LNAST output-type variable names that are not written to |
| `std::vector<Lnast_nid> lhs_list` | Vector of left-hand-side (lhs) LNAST nodes |
| `std::vector<std::vector<Lnast_nid>> rhs_list` | Vector of vectors of right-hand-side (rhs) LNAST nodes |
| `std::vector<FlatHashMap> in_scope_stack` | Vector of FlatHashMaps (`write_dict`, `read_dict`) |

<br>

`in_write_list()`
* **Parameters**: FlatHashMap, LNAST node-name, LNAST statement name (parent of node)
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name and its parent statement name exists in the FlatHashMap. Otherwise, returns false.

`in_read_list()`
* **Parameters**: FlatHashMap, LNAST node-name, LNAST statement name (parent of node)
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name and its parent statement name exists in the FlatHashMap. Otherwise, returns false.

`in_inefficient_LNAST()`
* **Parameters**: LNAST node-name
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name exists in `inefficient_LNAST`. Otherwise, returns false.

`in_output_vars()`
* **Parameters**: LNAST node-name
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name exists in `output_vars`. Otherwise, returns false.

`in_lhs_list()`
* **Parameters**: LNAST class, index _n_ (integer)
* **Return value**: `std::string_view`
* **Description**: Returns LNAST node-name at index _n_ if it exists in `lhs_list`. Otherwise return an empty `std::string_view`.

`in_rhs_list()`
* **Parameters**: LNAST class, LNAST node-name, index _n_ (integer)
* **Return value**: Integer
* **Description**: Returns the index where node-name is located in `rhs_list` when starting search from index 0 or from _n_. If node-name does not exists, return -1.

`in_in_scope_stack()`
* **Paremeters**: LNAST node-name
* **Return value**: Boolean
* **Description**: Returns true if LNAST node-name exists in even indices of `in_scope_stack`. Otherwise, returns false.

<br>

---

<br>

## Insert Functions

| Parameters | Description |
| ---------- | ----------- |
| `FlatHashMap write_dict` | Holds written-to-variables as keys and statement node names they belong to as values |
| `FlatHashMap read_dict` | Holds read-from-variables as keys and statement node names they belong to as values |
| `FlatHashSet output_vars` | Holds LNAST output-type variable names that are not written to |
| `FlatHashMap perm_write_dict` | Similiar to `write_dict` except static (will not change when scope changes) |
| `FlatHashMap perm_read_dict` | Similiar to `read_dict` except static (will not change when scope changes) |

<br>

`add_to_write_list()`
* **Parameters**: LNAST class, LNAST node-name, LNAST statement name (parent to node name)
* **Return value**: Void
* **Description**: Adds LNAST node-name to `write_dict` and/or `perm_write_dict` if not in it. <br>
If LNAST node-name is a temporary variable (name starts with "____"), throw error message (temporary variables should only be written to once). <br>
If LNAST node-name is a output variable (name starts with "%"), call `add_to_output_vars().`

`add_to_read_list()`
* **Parameters**: LNAST node-name, LNAST statement name (parent to node name)
* **Return value**: Void
* **Description**: Adds LNAST node-name to `read_dict` and/or `perm_read_dict` if not in it.

`add_to_output_vars()`
* **Parameters**: LNAST node-name
* **Return value**: void
* **Description**: Adds LNAST node-name to `output_vars` if not in it.

<br>

---

<br>

## Error Functions

`print_out_of_scope_vars()`
* **Paremeters**: LNAST class
* **Return value**: Void
* **Description**: Print LNAST AST and an error message ("Out of Scope Variable Error") followed by LNAST variable name(s) that have thrown the error.

`error_print_lnast_by_name()`
* **Parameters**: LNAST class, LNAST node-name
* **Return value**: Void
* **Description**: Print LNAST AST and indicate with an arrow the LNAST node-name that has thrown an error.

`error_print_lnast_by_type()`
* **Parameters**: LNAST class, LNAST node-name
* **Return value**: Void
* **Description**: Print LNAST AST and indicate with an arrow the LNAST node-type that has thrown an error.

`error_print_lnast_by_warn()`
* **Parameters**: LNAST class, vector of LNAST node-names
* **Return value**: Void
* **Description**: Print LNAST AST and indicate with an arrow the LNAST node-names from a vector that have thrown an error(s).

<br>

---

<br>

## Misc. Check Functions

| Parameter | Description |
| --------- | ----------- |
| `FlatHashMap write_dict` | Holds written-to-variables as keys and statement node names they belong to as values |
| `FlatHashMap read_dict` | Holds read-from-variables as keys and statement node names they belong to as values |
| `std::vector<Lnast_nid> lhs_list` | vector of left-hand-side (lhs) LNAST nodes |
| `std::vector<std::vector<Lnast_nid>> rhs_list` | vector of vectors of right-hand-side (rhs) LNAST nodes |
| `FlatHashSet output_vars` | Holds LNAST output-type variable names that are not written to |
| `FlatHashMap perm_write_dict` | Similiar to `write_dict` except static (will not change when scope changes) |
| `FlatHashMap perm_read_dict` | Similiar to `read_dict` except static (will not change when scope changes) |
| `FlatHashSet inefficient_LNAST` | Holds inefficient LNAST node-names |
| `FlatHashSet functions` | Holds LNAST function names from LNAST func_def node |
| `std::vector<std::string_view> out_of_scope_vars | Vector of LNAST node-names that are not defined in their respective scopes |


<br>

`resolve_read_write_lists()`
* **Parameters**: LNAST class
* **Return value**: Void
* **Description**: Iterate through `rhs_list` and search if the LNAST node exists in `lhs_list`. <br>
If LNAST node does not exist in `lhs_list`, display the LNAST AST and throw error (Last-Write Variable Warning). <br>
Iterate through `perm_write_dict` and check the following:
If LNAST node is not an output-type variable and exists in the `perm_read_dict`, then remove it from `perm_write_dict`. <br>
If LNAST node is an output-type variable, remove it from `output_vars` and from `perm_write_dict`. <br>
If `perm_write_dict` is not empty, display the LNAST AST and throw error (Never-Read Variable Warning) <br>
If output_vars is not empty, display the LNAST AST and throw error (Output Variable Warning)

`resolve_lhs_rhs_lists()`
* **Parameters**: LNAST class
* **Return value**: Void
* **Description**: Iterate through `lhs_list` and check if LNAST node exists in `rhs_list`. If it does, insert into `inefficient_LNAST`. <br>
If `inefficient_LNAST` is not empty, display the LNAST AST and throw error (Inefficient LNAST Warning)

`resolve_out_of_scope()`
* **Parameters**: None
* **Return value**: void
* **Description**: Iterate through `read_dict` and insert the LNAST node if the node is not a number, not a boolean, not in `in_scope_stack`, not in `write_dict`, not a temporary variable, and not in `functions`. Otherwise, continue iteration.

<br>

---

<br>

## Semantic Check Functions

<br>

`check_primitive_ops`
* **Parameters**: LNAST class, LNAST node, LNAST node-type, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: <br>
For Unary Operations, check the LNAST node for the existence of only two *ref-type* child-nodes (right-hand-side, left-hand-side). <br>
For N-ary Operations, check the LNAST node for the existence of at least two *ref-type* child-nodes. <br>
For Tuple Declaration, check the LNAST node for the existence of only one *ref-type* child-node and at most two *assign-type* child-nodes. <br>
For Tuple Concatenate, Select, and Dot Operations, check the LNAST node for the existence of only three *ref-type* child-nodes.
All other node-types will throw an error.

`check_tree_struct_ops`
* **Parameters**: LNAST class, LNAST node-type, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: <br>
If node-type is an if statement, call `check_if_ops()`. <br>
If node-type is a *for statement*, call `check_for_ops()`. <br>
If node-type is a *while statement*, call `check_while_op()`. <br>
If node-type is a *func_call statement*, call `check_func_call()`. <br>
If node-type is a *func_def statement*, call `check_func_def()`. <br>
All other node-types will throw an error.
Following each tree-structure operation, push `write_dict` and `read_dict` onto `out_of_scope_stack` and pop `write_dict` and `read_dict` off of `in_scope_stack`.

`check_if_ops`
* **Parameters**: LNAST class, LNAST node, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: Check the LNAST node to make sure the only child node-types are '*cstmts*', '*stmts*', or '*ref*'. All other node-types will throw an error.

`check_for_ops`
* **Parameters**: LNAST class, LNAST node, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: Checks the LNAST node for the existence of two *ref-type* and one *statement-type* child-nodes. All other node-types will throw an error.

`check_while_ops`
* **Parameters**: LNAST class, LNAST node, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: Checks the LNAST node for the existence of one *condition-type* and one *statement-type* child-nodes. All other node-types will throw an error.

`check_func_def`
* **Parameters**: LNAST class, LNAST node, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: Checks the LNAST node for the existence of at least one *ref-type*, one *condition-type*, and one *statement-type* child-nodes. All other node-types will throw an error. Also checks to make sure all output-type variables are written to.

`check_func_call`
* **Parameters**: LNAST class, LNAST node, LNAST statement name (parent to node)
* **Return value**: Void
* **Description**: Checks the LNAST node for the existence of three *ref-type* child-nodes. All other node-types will throw an error.

<br>

---

<br>

## LNAST Semantic and Miscellaneous Check (top-level function)

`do_check()`
* **Parameters**: LNAST class
* **Return value**: Void
* **Description**: Iterate through nodes of LNAST AST. <br>
If node-type is a primitive operation, call `check_primitive_ops()`. <br>
If node-type is a func_call operation, call `check_func_call()`. <br>
If node-type is a tree structure operation, call `check_tree_struct_ops()`. <br>
After iteration of LNAST AST, call `resolve_out_of_scope()`, `resolve_read_write_lists()`, and `resolve_lhs_rhs_lists()` to check for any errors/warnings.

<br>

---

<br>
