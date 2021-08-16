
# Coding Style

These are the coding style rules for LiveHD C++. Each rule can be broken, but it
should be VERY rare, and a small comment should be placed explaining why.

## Overall

* When possible keep the system simple. Complexity is the enemy of maintenance.
* Deprecate no longer used features.
* Try to reduce friction. This means to avoid hidden/complex steps.
* Every main API should have a unit test for testing but also to demonstrate usage.

## comments

Code should be the comments, try to keep comments concise. They should explain
the WHY not the HOW. The code is the HOW.

Labels used in comments:

```
// FIXME: Known bug/issue but no time to fix it at the moment

// TODO: Code improvement that will improve perf/quality/??? but no time at the moment

// WARNING: message for some "strange" "weird" code that if changes has effects
// (bug). Usually, this is a "not nice" code that must be kept for some reason.

// NOTE: Any comment that you want to remember something about (not critical)

// STYLE: why you broke a style rule (pointers, iterator...)
```

## strings

Avoid std::string and std::string_view. Use them only when interfacing external project. Use mmap_lib::str

Avoid pointers, use std::unique_ptr with RAII

## Variable naming rules

* No camelCase. Use underscores to separate words:
```cpp
foo_bar = Foo_bar(3);
```
* Use plural for containers with multiple entries like vector, singular otherwise
```cpp
elem = entries[index];
```
* Classes/types/enums start with uppercase. Lowercase otherwise
```cpp
val = My_enum::Big;
class Sweet_potato {
```

## Error handling and exceptions

Use the Pass::error or Pass:warn for error and likely error (warn). Internally, error generates
and exception capture by the main lgshell to move to the next task.

```cpp
Pass::error("inou_yaml: can only have a yaml_input or a graph_name, not both");
Pass::warn("inou_yaml.to_lg: output:{} input:{} graph:{}", output, input, graph_name);
```

## No tabs, indentation is 2 spaces

Make sure to configure your editor to use 2 spaces

You can configure your text editor to do this automatically

## Include order

First do C includes (try to avoid when possible), then an empty line with C++
includes, then an empty line followed with lgraph related includes. E.g:

```
#include <sys/types.h>
#include <dirent.h>

#include <iostream>
#include <set>

#include "graph_library.hpp"
#include "lgedgeiter.hpp"
```

## Keep column widths short

- Less than 120 characters if at all possible (meaning not compromising
  readability)

You can configure your text editor to do this automatically

## Avoid trailing spaces

You can configure your text editor to highlight them.
 https://github.com/ntpeters/vim-better-whitespace

## Use C++14 iterators not ::iterator

```cpp
for(auto idx:g->unordered()) {
}
```

Use structured returns when iterator is returned for cleaner code:

```cpp
for(const auto &[name, id]:name2id) {
  // ...
```

## Use "auto", or "const auto", when possible.

```cpp
for(auto idx:g->unordered()) {
  for(const auto &c:g->out_edges(idx)) {
```

## const and local variables

It may be too verbose to write const all the time. The coding style request to use 
const (when possible) in iterators and pointers. The others are up to the programmer.


## Do not use std::unordered_set, std::map, use flat_hash_map or flat_hash_set from abseil

```cpp
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

absl::flat_hash_map<Index_ID, RTLIL::Wire *>   my_example;
```

## Some common idioms to handle map/sets

Traverse the map/set, and as it traverses decide to erase some of the entries:
```cpp
for (auto it = m.begin(), end = m.end(); it != end;) {
  if (condition_to_erase_it) {
    m.erase(it++);
  } else {
    ++it;
  }
}
```

To check if a key is present:
```cpp
if (set.contains(key_value)) {
}
```

## Use absl::Span instead of std::vector as return argument

absl::Span is the equivalent of string_view for a string but for vectors. Like
string_view, it does not have ownership, and the size in the span can decrease
(not increase) without changing the original vector with "subspan". Faster and
more functional, no reason to return "const std::vector<Foo> &", instead return
"absl::Span<Foo>".

```cpp
#include "absl/types/span.h"

absl::Span<Sub_node>    get_sub_nodes() const {
  I(sub_nodes.size()>=1);
  return absl::MakeSpan(sub_nodes).subspan(1); // Skip first element from vector
};
```

## Pass by reference and use "const" when possible

```cpp
void print(const Sub_node& g); //or

void edit(Sub_node& g);
```

Note that older code still uses pointers, this is no longer allowed.

## Avoid dynamic allocation as much as possible

The idea is to RARELY directly allocate pointer allocation

Use:

```cpp
foo = Sweet_potato(3, 7)
```

instead of

```cpp
foo = new Sweet_potato(3, 7)
```

## Do not use "new"/"delete" keywords. Use smart pointers if needed (VERY VERY rare)


Use:
```cpp
foo = std::make_unique<Sweet_potato>(3,7);
```

instead of

```cpp
foo = new Sweet_potato(3, 7)
```


## Use fmt::print to print messages for debugging

```cpp
fmt::print("This is a debug message, name = {}, id = {}\n",g->get_name(), idx);
```

## Use accessors consistently

* get_XX(): gets "const XX &" from object without side effects (assert if it does not exist)
    * operator(Y) is an alias for get_XX(Y)
* ref_XX(): gets "XX * " (nullptr if it does not exist)
* find_XX(): similar to get_XX but, if it does not exist return invalid object (is_invalid())
* setup_XX(): gets XX from object, if it does not exists, it creates it
* create_XX(): clears previous XX from object, and creates a new and returns it
* set_XX(): sets XX to object, it creates if it does not exist. Similar to
  create, but does not return reference.

If a variable is const, it can be exposed directly without get/set accessors

foo = x.const_var;  // No need to have x.get_const_var()

## Use bitarray class to have a compact bitvector marker

```cpp
bitarray visited(g->max_size());
```

## Use iassert extensively / be meaningful whenever possible in assertions

This usually means use meaningful variable names and conditions that are easy to understand.
If the meaning is not clear from the assertion, use a comment in the same line.
This way, when the assertion is triggered it is easy to identify the problem.

```cpp
I(n_edges > 0); //at least one edge needed to perform this function
```

We use the https://github.com/masc-ucsc/iassert package. Go to the iassert for more details on the advantages
and how to allow it to use GDB with assertions.

## Develop in debug mode and benchmark in release mode

Extra checks should be only in debug. Debug and release must execute the same,
only checks (not behavior change) allowed in debug mode.

Benchmark in release. It is 2x-10x faster.

## Use compact if/else brackets

Use clang-format as configured to catch style errors. LGraph clang-format is
based on google format, but it adds several alignment directives and wider
terminal.

```
   cd XXXX
   clang-format -i *pp
```

```cpp
std::vector<LGraph *> Inou_yaml::generate() {

  if (opack.graph_name != "") {
     // ...
  } else {
     // ..
  }
```

## Decide how to use attributes

Attributes are parameters or information that an be per Node, Node_pin or Edge. In LGraph, attributes are
persistent. This means that they are kept across execution runs in the LGraph database (E.g: in lgdb).


For persistent attributes, the structures to use are defined in core/annotate.hpp. Any new attribute
must be added to "annotate.hpp" to preserve persistence and to make sure that they are cleared when needed.


Many times it is important to have information per node, but that it is not persistent across runs. For example,
when building a LGraph from Yosys, there is a need to remember pointers from yosys to LGraph. This by definition can not be persistent because pointers change across runs. For this case, there are several options.


### The Non-Persistent Annotations


If the data structure needs to keep most of the node/pins in the Lgraph, use the compact_class notation:
```cpp
absl::flat_hash_map<SomeData, Node_pin::Compact_class> s2pin;
absl::flat_hash_map<SomeData, Node::Compact_class>     s2node;

SomeData d1;
Lgraph *lg; // LGraph owning the node
s2pin[d1]  = node.get_driver_pin().get_compact_class(); // Example of use getting a pint
s2node[d1] = node.get_compact_class();
auto name = s2pin[d1].get_node(lg).get_name();   // Pick previously set driver name
```

Another example:

```cpp
absl::flat_hash_map<Node_pin::Compact, RTLIL::Wire *>  input_map;

input_map[pin.get_compact()] = wire;

auto *wire = input_map[pin.get_compact()];

for(const auto &[key, value]:input_map) {
  Node_pin pin(lg, key); // Key is a ::Compact, not a Node_pin
  auto name  = pin.get_name();
  // ... Some use here
}
```

If the data structure just holds a small subset of the graph, you can keep the
metadata, and use Node/Node_pin directly. E.g:

```cpp
absl::flat_hash_map<SomeData, Node_pin> s2pin;
absl::flat_hash_map<SomeData, Node>     s2node;

SomeData d1;
s2pin[d1]  = node.get_driver_pin(); // Example of use getting a pint
s2node[d1] = node;
auto name = s2pin[d1].get_name();   // Pick previously set driver name
```

In this case, it is fine to use the full Node, Node_pin, or Edge. This has some
pointers inside, but it is OK because it is not persistent.


## Avoid code duplication

The rule is that if the same code appears in 3 places, it should be refactored

Tool to detect duplication
```
    find . -name '*.?pp' | grep -v test >list.txt
    duplo -ml 12 -pt 90 list.txt report.txt
```

