# Select between inou and pass

WARNING: this document is out dated

* **inou**: reads external (non-LGraph) files to create an LGraph, or exports files from an LGraph (ex: [inou/json](../inou/json)).
* **pass**: optimizes or regenerates a LGraph, or generates a new set of LGraphs from a given LGraph (such as dead code elimination).

Use one of the sample passes as starting point ([inou/rand](../inou/json) or [pass/sample](../pass/sample)) and
 make sure to use the Options_base and inherit from Inou or Pass

## Create a pass

* Create pass/<my\_pass> directory

In the file pass/<my\_pass>/my\_pass.hpp:

```cpp
//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "pass.hpp"
#include "lgraph.hpp"

class <My_pass> : public Pass {
  private:
    void do_work(const LGraph& g);

  public:
  <My_pass>() : Pass("my_pass") {
  }

  void setup() final;

  static void pass(Eprp_var &var);
};

```

Finally, in the pass/<my\_pass>/BUILD

```cpp
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "<my_pass>",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
    ]
)
```

* Register the newly created command in the lgshell interface
* Add a dependency in main/BUILD

```
#....
            "//cops/live:cops_live",

            #add dependencies to new passes here
            "//pass/<my_pass>:<my_pass>",
    ],
#....

```

  * Add the hook for the setup on main/main\_api.cpp

```
//....
void setup_cops_live();

// add new setup function prototypes here
void setup_pass_<my_pass>();

void Main_api::init() {
//....

  // call the new setup function here
  setup_pass_<my_pass>();

  // do not touch anything beyond this point
//....
```

## Pass Parameters and Common variables

 One of the main goals is to have a uniform set of passes in lgshell. lgshell should use this common
variable names when possible

```bash
    name:foo        lgraph name
    path:lgdb       lgraph database path (lgdb)
    files:foo,var   comma separated list of files used for INPUT
    odir:.          output directory to generate files like verilog/pyrope...
```

To add parameters to your pass, simply add parameters to the Eprp\_method declaration:

In the pass\_<my_pass>.cpp file:

```
  Eprp_method <my_other_pass>("pass.<my_other_pass>", "some useful description", &<My_pass>::pass2);
  <my_other_pass>.add_label_required("name","lgraph name");
  <my_other_pass>.add_label_optional("odir","output location","<some default location in case none is provided>");
  <my_other_pass>.add_label_optional("path","lgdb path","lgdb");
  register_pass(<my_other_pass>);
  
  ...
  
  void <My_pass>::pass(Eprp_var &var) {
    ...
    if (var.has_label("label name")) {
      fmt::print("entered label name: {}\n", var.get("label name"));
    }
    ...
  }
```

