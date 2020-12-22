
# Semantic Pass Function Descriptions

## Type Check Functions

`is_primitive_op()`
* Parameters: LNAST Node Type
* Return value: boolean
* Returns true if LNAST Node Type is a Primitive type (e.g. logical, unary, nary operations). Otherwise, returns false.

`is_tree_structs()`
* Parameters: LNAST Node Type
* Return value: boolean
* Returns true if LNAST Node Type is a Tree Structure type (e.g. if, for, while statement). Otherwise, returns false.

`is_temp_var()`
* Parameters: LNAST Node Name
* Return value: boolean
* Returns true if LNAST Node Name is a temporary variable often denoted by 3 underscores ("____"). Otherwise, returns false.

`is_a_number()`
* Parameters: LNAST Node Name
* Return value: boolean
* Returns true if LNAST Node Name is an integer. Otherwise, returns false.

<br>

---

<br>

## Existence Check Functions

| Parameter | Description |
| ------- | ----------- |
| `FlatHashMap write_dict` | Holds written-to-variables as keys and statement names they belong to as values |
| `FlatHashMap read_dict` | Holds read-from-variables as keys and statement names they belong to as values |
| `FlatHashSet inefficient_LNAST` | Holds inefficient LNAST Node Names |
| `FlatHashSet output_vars` | Holds LNAST output-type variable names that are not written to |
| `std::vector<Lnast_nid> lhs_list` | Vector of left-hand-side (lhs) LNAST Nodes |
| `std::vector<std::vector<Lnast_nid>> rhs_list` | Vector of vectors of right-hand-side (rhs) LNAST Nodes |
| `std::vector<FlatHashMap> in_scope_stack` | Vector of FlatHashMaps (`write_dict`, `read_dict`) |

<br>

`in_write_list()`
* Parameters: FlatHashMap, LNAST Node Name, LNAST Statement Name (the statement node belongs to)
* Return value: boolean
* Returns true if LNAST Node Name and its Statement Name exists in the FlatHashMap. Otherwise, returns false.

`in_read_list()`
* Parameters: FlatHashMap, LNAST Node Name, LNAST Statement Name (the statement node belongs to)
* Return value: boolean
* Returns true if LNAST Node Name and its Statement Name exists in the FlatHashMap. Otherwise, returns false.

`in_inefficient_LNAST()`
* Parameters: LNAST Node Name
* Return value: boolean
* Returns true if LNAST Node Name exists in `inefficient_LNAST`. Otherwise, returns false.

`in_output_vars()`
* Parameters: LNAST Node Name
* Return value: boolean
* Returns true if LNAST Node Name exists in `output_vars`. Otherwise, returns false.

`in_lhs_list()`
* Parameters: LNAST Class, Index _n_ (integer)
* Return value: `std::string_view`
* Returns LNAST Node Name at Index _n_ if it exists in `lhs_list`. Otherwise return an empty `std::string_view`.

`in_rhs_list()`
* Parameters: LNAST Class, LNAST Node Name, Index _n_ (integer)
* Return value: integer
* Returns the index where Node Name is located in `rhs_list` when starting search from 0 or from _n_. If Node Name does not exists, return -1.

`in_in_scope_stack()`
* Paremeter: LNAST Node Name
* Return value: boolean
* Returns true if LNAST Node Name exists in even indices of in_scope_stack. Otherwise, returns false.

<br>

---

<br>

## Insert Functions

| Parameters | Description |
| ---------- | ----------- |
| `FlatHashMap write_dict` | Holds written-to-variables as keys and statement node names they belong to as values |
| `FlatHashMap read_dict` | Holds read-from-variables as keys and statement node names they belong to as values |
| `FlatHashSet output_vars` | Holds LNAST output-type variable names that are not written to |
| `FlatHashMap perm_write_dict` | Similiar to write_dict except static (will not change when scope changes) |
| `FlatHashMap perm_read_dict` | Similiar to read_dict except static (will not change when scope changes) |

<br>

`add_to_write_list()`
* Parameters: LNAST Class, LNAST Node Name, LNAST Statement Name
* Return value: void
* Adds LNAST Node Name to `write_dict` and/or `perm_write_dict` if not in it.
If LNAST Node Name is a temporary variable (name starts with "____"), throw error message (temporary variables should only be written to once). 
If LNAST Node Name is a output variable (name starts with "%"), call `add_to_output_vars().`

`add_to_read_list()`
* Parameters: LNAST Node Name, LNAST Statement Name
* Return value: void
* Adds LNAST Node Name to `read_dict` if not in it and to `perm_read_dict` if not in it.

`add_to_output_vars()`
* Parameters: LNAST Node Name
* Return value: void
* Adds LNAST Node Name to `output_vars` if not in it.

<br>

---

<br>

## Error Functions

`print_out_of_scope_vars()`
* Paremeters: LNAST Class
* Return value: void
* Print LNAST AST and an error message ("Out of Scope Variable Error") followed by LNAST variable name(s) that have thrown the error.

`error_print_lnast_by_name()`
* Parameters: LNAST Class, LNAST Node Name
* Return value: void
* Print LNAST AST and indicates with an arrow the LNAST Node Name that has thrown an error.

`error_print_lnast_by_type()`
* Parameters: LNAST Class, LNAST Node Name
* Return value: void
* Print LNAST AST and indicates with an arrow the LNAST Node Type that has thrown an error.

`error_print_lnast_by_warn()`
* Parameters: LNAST Class, vector of LNAST Node Names
* Return value: void
* Print LNAST AST and indicates with an arrow the LNAST Node Names from a vector that have thrown an error or errors.

<br>

---

<br>

## Misc. Check Functions

| Parameter | Description |
| --------- | ----------- |
| `FlatHashMap write_dict` | Holds written-to-variables as keys and statement node names they belong to as values |
| `FlatHashMap read_dict` | Holds read-from-variables as keys and statement node names they belong to as values |
| `std::vector<Lnast_nid> lhs_list` | vector of LNAST nodes that hold lhs nodes |
| `std::vector<std::vector<Lnast_nid>> rhs_list` | vector of vectors of LNAST nodes that hold rhs nodes |
| `FlatHashSet output_vars` | Holds LNAST output-type variable names that are not written to |
| `FlatHashMap perm_write_dict` | Similiar to write_dict except static (will not change when scope changes) |
| `FlatHashMap perm_read_dict` | Similiar to read_dict except static (will not change when scope changes) |
| `FlatHashSet inefficient_LNAST` | Holds inefficient LNAST Node Names |

<br>

`resolve_read_write_lists()`
* Parameters: LNAST Class
* Return value: void
* Iterate through `rhs_list` and search if the LNAST node exists in `lhs_list`. If LNAST node does not exist in `lhs_list`, display the LNAST AST and throw error (Last-Write Variable Warning).
Iterate through `perm_write_dict` and check the following:
If LNAST node is not an output-type variable and exists in the `perm_read_dict`, then remove it from `perm_write_dict`.
If LNAST node is an output-type variable, remove it from `output_vars` and from `perm_write_dict`.
If `perm_write_dict` is not empty, display the LNAST AST and throw error (Never-Read Variable Warning)
If output_vars is not empty, display the LNAST AST and throw error (Output Variable Warning)

`resolve_lhs_rhs_lists()`
* Parameters: LNAST Class
* Return value: void
* Iterate through `lhs_list` and check if LNAST node exists in `rhs_list`. If it does, insert into `inefficient_LNAST`.
If `inefficient_LNAST` is not empty, display the LNAST AST and throw error (Inefficient LNAST Warning)

`resolve_out_of_scope()`
* Parameters: None
* Return value: void

`resolve_out_of_scope_func_def()`
* Parameters: None
* Return value: void

<br>

---

<br>

## Semantic Check Functions

<br>

---

<br>

