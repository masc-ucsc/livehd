# Select between inou and pass

  * inou: reads from external (non-LGraph) to create an LGraph, or exports external from a LGraph.
Examples, inou/json

  * pass: optimizes or regenerates a LGraph, or generated a new set of LGraphs from a given LGraph.
E.g: dead-code-elimination

Use one of the sample passes as starting point (inou/rand or pass/dce) and
 make sure to use the Options_base and inherit from Inou or Pass

## Create a pass

  * Create pass/<my\_pass> directory

In the file pass/<my\_pass>/my\_pass.hpp:

```cpp
//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef <MYPASS>_HPP_
#define <MYPASS>_HPP_

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

#endif
```

In the file pass/<my\_pass>/my\_pass.cpp:

```cpp
//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "<my_pass>.hpp"

void setup_pass_<my_pass>() {
  <My_pass> p;
  p.setup();
}

void <My_pass>::setup() {
  Eprp_method m1("pass.<my_pass>", "<my_pass> is an example pass, this is an example help text", &<My_pass>::pass);
  register_pass(m1);
}

void <My_pass>::pass(Eprp_var &var) {
  <My_pass> pass;

  for(auto &l:var.lgs) {
    pass.do_work(l);
  }
}

void <My_pass>::trans(LGraph &g) {

  for(auto idx : g.fast()) {
    if(g.is_graph_output(idx)) {
      fmt::print("found graph output {}\n",g.get_graph_output_name(idx));
    }
  }
}
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
```
