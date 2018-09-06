
#include "eprp.hpp"


class test1{
public:
  static Eprp_var foo(const Eprp_var &var) {
    fmt::print("test1.foo");
    for (const auto& v : var.dict) {
      fmt::print(" {}:{}",v.first,v.second);
    }
    fmt::print(" ::");
    for (const auto& v : var.lgs) {
      fmt::print(" {}",v->get_id());
    }
    fmt::print("\n");

    Eprp_var var2;
    var2.add("test1_foo","field1");

    return var2;
  }
};
class test2{
public:
  static Eprp_var bar(const Eprp_var &var) {
    fmt::print("test2.foo");
    for (const auto& v : var.dict) {
      fmt::print(" {}:{}",v.first,v.second);
    }
    fmt::print(" ::");
    for (const auto& v : var.lgs) {
      fmt::print(" {}",v->get_id());
    }
    fmt::print("\n");

    Eprp_var var2;
    var2.add("test2_bar", "field2");
    var2.add(new LGraph());

    return var2;
  }
};

int main() {

  Eprp eprp;

  eprp.register_method("inou.rand.generate", &test1::foo);
  eprp.register_method("pass.dfg", &test2::bar);

  eprp.readline(" inou.rand.generate lgdb:./lgdb graph_name:\"chacha\"");
  eprp.readline("inou.rand.generate a:\"b\" \"potato\" | a");
}
