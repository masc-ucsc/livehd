
#include "fmt/format.h"

#include "gtest/gtest.h"

// Fake lgraph class for testing
class LGraph {
public:
  int id;

  LGraph() {
    static int conta=0;
    id = conta++;
  };

  int get_id() const { return id; };
};

#include "eprp.hpp"
#include "eprp_utils.hpp"

static bool is_equal_called = false;

class test1{
public:
  static void files2(Eprp_var &var) {
    const std::string files = var.get("files");

    std::vector<std::string> svector = Eprp_utils::parse_files(files,"test1.files2");

    for (const auto& v : svector) {
      fmt::print(" {}",v);
    }
    fmt::print("\n");

    EXPECT_EQ(svector.size(),4);

    EXPECT_STREQ(svector[0].c_str(), "g3xx");
    EXPECT_STREQ(svector[1].c_str(), "./f1/f1.v");
    EXPECT_STREQ(svector[2].c_str(), "xotato/../bar.prp");
    EXPECT_STREQ(svector[3].c_str(), "potato/bar.v");
  }

  static void foo(Eprp_var &var) {
    fmt::print("test1.foo");
    for (const auto& v : var.dict) {
      fmt::print(" {}:{}",v.first,v.second);
    }
    fmt::print(" ::");
    for (const auto& v : var.lgs) {
      fmt::print(" {}",v->get_id());
    }
    fmt::print("\n");

    var.add("test1_foo","field1");
    EXPECT_STREQ(var.get("test1_foo").c_str(), "field1");
  }

  static void is_equal(Eprp_var &var) {
    EXPECT_STREQ(var.get("test1_foo").c_str(), "field1");

    EXPECT_EQ(var.get("lgdb"), var.get("check1"));

    EXPECT_STREQ(var.get("graph_name").c_str(), "chacha");

    EXPECT_STRNE(var.get("lgdb").c_str(), "");

    fmt::print("var.get = {}\n", var.get("nofield2"));

    EXPECT_STREQ(var.get("nofield").c_str(), "");

    is_equal_called = true;
  }
  static void pass(Eprp_var &var) {
  }
};

class test2{
public:
  static void bar(Eprp_var &var) {
    fmt::print("test1.foo");
    for (const auto& v : var.dict) {
      fmt::print(" {}:{}",v.first,v.second);
    }
    fmt::print(" ::");
    for (const auto& v : var.lgs) {
      fmt::print(" {}",v->get_id());
    }
    fmt::print("\n");

    var.add("test2_bar", "field2");
    var.add(new LGraph());
  }
};

class EPrpTest : public ::testing::Test {
protected:
  Eprp eprp;
  void SetUp() override {
    Eprp_method m1("test1.xyz.generate", "Generate a random test/method call to foo", &test1::foo);
    m1.add_label_required("lgdb","lgraph directory");
    m1.add_label_optional("graph_name","another super duper attribute");

    EXPECT_STREQ(m1.get_label_help("lgdb").c_str(),"lgraph directory");
    EXPECT_TRUE(m1.has_label("graph_name"));
    EXPECT_FALSE(m1.has_label("graph_name_not_there"));

    Eprp_var var;
    std::string txt;
    EXPECT_TRUE(m1.check_labels(var, txt));
    EXPECT_FALSE(txt.empty());

    var.add("lgdb","potato");
    txt.clear();
    EXPECT_FALSE(m1.check_labels(var, txt));
    EXPECT_TRUE(txt.empty());

    Eprp_method m2("test1.fff.test","fff::is_equal call", &test1::is_equal);
    m2.add_label_required("lgdb","lgraph directory");
    m2.add_label_required("check1","check1 super duper attribute");
    // Missing check2 attribute on purpose to check warning generation
    //
    Eprp_method m3("test1.pass", "pass value through", &test1::pass);

    eprp.register_method(m1);
    eprp.register_method(m2);
    eprp.register_method(m3);
  }
};

class EPrpFiles : public ::testing::Test {
protected:
  Eprp eprp;
  void SetUp() override {
    Eprp_method m1("test1.files2", "Generate a random test/method call to foo", &test1::files2);
    m1.add_label_required("files","list of files");

    eprp.register_method(m1);
  }
};

TEST_F(EPrpFiles, ParseFiles) {
  const char *buffer =" test1.files2 match:\"nothing\" files:g3xx,./f1/f1.v,xotato/../bar.prp,,potato/bar.v";

  eprp.parse("parsefiles", buffer, strlen(buffer));
}

TEST_F(EPrpTest, SimpleReadlinePipe) {
  is_equal_called = false;
  const char *buffer =" test1.xyz.generate lgdb:./lgdb graph_name:chacha |> test1.fff.test     check2:chacha    check1:./lgdb   ";

  eprp.parse("inline", buffer, strlen(buffer));

  EXPECT_TRUE(is_equal_called);
  is_equal_called=false;

  buffer =" test1.pass graph_name:chacha check2:chacha  check1:./lgdb  lgdb:./lgdb test1_foo:field1 |> @a";
  eprp.parse("inline", buffer, strlen(buffer));

  buffer ="@a |> test1.fff.test";
  eprp.parse("inline", buffer, strlen(buffer));
  EXPECT_TRUE(is_equal_called);

}
