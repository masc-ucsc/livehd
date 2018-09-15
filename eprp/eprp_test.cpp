
#include "gtest/gtest.h"

#include "eprp.hpp"

static bool is_equal_called = false;

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

    Eprp_var var2 = var; // Make copy, var is const
    var2.add("test1_foo","field1");
    EXPECT_STREQ(var2.get("test1_foo").c_str(), "field1");

    return var2;
  }

  static Eprp_var is_equal(const Eprp_var &var2) {
    EXPECT_STREQ(var2.get("test1_foo").c_str(), "field1");

    EXPECT_EQ(var2.get("lgdb"), var2.get("check1"));

    EXPECT_EQ(var2.get("graph_name"), var2.get("check2"));

    EXPECT_STRNE(var2.get("lgdb").c_str(), "");

    fmt::print("var2.get = {}", var2.get("nofield2"));

    EXPECT_STREQ(var2.get("nofield").c_str(), "");

    is_equal_called = true;

    return var2;
  }
};

class test2{
public:
  static Eprp_var bar(const Eprp_var &var) {
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
    var2.add("test2_bar", "field2");
    var2.add(new LGraph());

    return var2;
  }
};

class EPrpTest : public ::testing::Test {
protected:
  Eprp eprp;
  virtual void SetUp() override {
    eprp.register_method("test1.xyz.generate", &test1::foo);
    eprp.register_method("test1.fff.test",     &test1::is_equal);
  }
};

TEST_F(EPrpTest, SimpleReadlinePipe) {
  is_equal_called = false;
  const char *buffer =" test1.xyz.generate lgdb:./lgdb graph_name:chacha |> test1.fff.test     check2:chacha    check1:./lgdb   ";

  eprp.parse("inline", buffer, strlen(buffer));

  EXPECT_TRUE(is_equal_called);
}
