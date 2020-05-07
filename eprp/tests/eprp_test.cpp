
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
    auto files = var.get("nofiles");

    std::vector<std::string> svector = absl::StrSplit(files,',');

    for (const auto& v : svector) {
      fmt::print(" {}",v);
    }
    fmt::print("\n");

    EXPECT_EQ(svector.size(),4);

    EXPECT_EQ(svector[0], "g3xx");
    EXPECT_EQ(svector[1], "./f1/f1.v");
    EXPECT_EQ(svector[2], "xotato/../bar.prp");
    EXPECT_EQ(svector[3], "potato/bar.v");
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
    EXPECT_EQ(var.get("test1_foo"), "field1");
  }

  static void is_equal(Eprp_var &var) {
    EXPECT_EQ(var.get("test1_foo"), "field1");

    EXPECT_EQ(var.get("lgdb"), var.get("check1"));

    EXPECT_NE(var.get("lgdb"), "");

    fmt::print("var.get = {}\n", var.get("nofield2"));

    EXPECT_EQ(var.get("nofield"), "");

    is_equal_called = true;
  }
  static void pass(Eprp_var &var) {
    (void)var;
    fmt::print("pass called\n");
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

class Eprp_test : public ::testing::Test {
public:
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
    m2.add_label_optional("lgdb","lgraph directory","lgdb");
    m2.add_label_optional("check1","check1 super duper attribute","lgdb");
    m2.add_label_required("check2","check2 super duper attribute");

    Eprp_method m3("test1.pass", "pass value through", &test1::pass);
    m3.add_label_required("check1","check1 super duper attribute");
    m3.add_label_required("check2","check2 super duper attribute");

    eprp.register_method(m1);
    eprp.register_method(m2);
    eprp.register_method(m3);
  }
};

class Eprp_files : public ::testing::Test {
public:
protected:
  Eprp eprp;
  void SetUp() override {
    Eprp_method m1("test1.files2", "Generate a random test/method call to foo", &test1::files2);
    m1.add_label_required("nofiles","list of files");

    eprp.register_method(m1);
  }
};

TEST_F(Eprp_files, ParseFiles) {
  const char *buffer =" test1.files2 match:\"nothing\" nofiles:g3xx,./f1/f1.v,xotato/../bar.prp,potato/bar.v";

  eprp.parse_inline(buffer);
}

TEST_F(Eprp_test, SimpleReadlinePipe) {
  is_equal_called = false;
  const char *buffer =" test1.xyz.generate lgdb:./lgdb graph_name:chacha |> test1.fff.test check2:jeje    lgdb:potato   check1:potato   ";

  eprp.parse_inline(buffer);

  EXPECT_TRUE(is_equal_called);
  is_equal_called=false;

  buffer =" test1.pass test1_foo:field1 check2:chacha  check1:lgdb |> #a";
  eprp.parse_inline(buffer);

  buffer ="#a |> test1.fff.test check2:not_used";
  eprp.parse_inline(buffer);
  EXPECT_TRUE(is_equal_called);
}

