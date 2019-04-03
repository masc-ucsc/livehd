
# General coding style for LGraph

These are the coding style rules for LGraph. Each rule can be broken, but it
should be VERY rare, and a small comment should be placed explaining why.

## comments

Code should be the comments, try to keep comments concise. They should explain
the WHY not the HOW. The code is the HOW.

Labels used in comments:

// FIXME: Known bug/issue but no time to fix it at the moment

// TODO: Code improvement that will improve perf/quality/??? but no time at the moment

// WARNING: message for some "strange" "weird" code that if changes has effects
// (bug). Usually, this is a "not nice" code that must be kept for some reason.

// NOTE: Any comment that you want to remember something about (not critical)

// STYLE: why you broke a style rule (pointers, iterator...)

## c++17

Use std::string_view instead of "const char &ast" or "const std::string &" in the APIs. DO NOT use string_view in maps/sets/storage.

Avoid pointers, use std::unique_ptr with RAII

## No camelCase. Use underscores to separate words:

```cpp
foo_bar = Foo_bar(3);
```

## Exceptions

Unexpected non-recoverable behavior should raise an exception. They are captured at the top level to notify user and move to next task.

## No tabs, indentation is 2 spaces

Make sure to configure your editor to use 2 spaces

You can configure your text editor to do this automatically

## Include order

First do C includes (try to avoid when possible), then an empty line with C++
includes, then an empty line followed with lgraph related includes. E.g:

#include <sys/types.h>
#include <dirent.h>

#include <iostream>
#include <set>

#include "graph_library.hpp"
#include "lgedgeiter.hpp"

## Keep column widths short

- Less than 120 characters if at all possible (meaning not compromising
  readability)

You can configure your text editor to do this automatically

## Avoid trailing spaces

You can configure your text editor to highlight them.
 https://github.com/ntpeters/vim-better-whitespace

## Classes start with upper case, but just first word.

```cpp
class Sweet_potato {
```

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


## Use "auto" and "const auto" when possible.

```cpp
for(auto idx:g->unordered()) {
  for(const auto &c:g->out_edges(idx)) {
```

## Strings must be passed as std::string_view

```cpp
void print(std::string_view message)
```

## Use abseil library for String operations like StrCat, StrSplit, EndsWith

```cpp
#include "absl/strings/substitute.h"

auto file = absl::StrCat("file","/",extension);

if (absl::EndsWidth(file,".prp")) {
  // Your code here
}
```
The reason is to have a more efficient. Using + for string concats have mallocs
and traversal overheads.

## Do not use std::unordered_set, use flat_hash_map or flat_hash_set from abseil


```cpp
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

absl::flat_hash_map<Index_ID, RTLIL::Wire *>   my_example;
```

## Pass by reference and use "const" when possible

```cpp
void print(const LGraph& g); //or

void edit(LGraph& g);
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

## Use Pass:: report errors/warnings/info

```cpp
Pass::error("inou_yaml: can only have a yaml_input or a graph_name, not both");
Pass::warn("inou_yaml.to_lg: output:{} input:{} graph:{}", output, input, graph_name);
```

## Use accessors consistently

* get_XX(): gets XX from object without side effects, it should return always
  (assert if it does not exist)
* find_XX(): gets XX from object without side effects, if it does not exist,
  returns null or false, or ??
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

```cpp
std::vector<LGraph *> Inou_yaml::generate() {

  if (opack.graph_name != "") {
     // ...
  } else {
     // ..
  }
```

## Check if a string starts with a substring

Use the STL rfind to handle the common case of finding a sub-string. Even for when checking if a string starts with a given substring.

```cpp
  // If string starts with a given substring
  std::string str1 = "start333";
  std::string str2 = "xstart333";
  I(str1.rfind("start") == 0);
  I(str2.rfind("start") != 0);

  // For single character checks
  I(str1[0] == 's');
  I(str2[1] == 's');

  // If the substring exists
  I(str1.rfind("start") != std::string::npos);
  I(str2.rfind("start") != std::string::npos);
  I(str2.rfind("potato") == std::string::npos);
```

