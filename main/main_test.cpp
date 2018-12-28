#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <pty.h>
#include <fcntl.h>

#include <string>
#include <regex>

#include "gtest/gtest.h"
#include <gmock/gmock.h>

using testing::HasSubstr;

class MainTest : public ::testing::Test {
protected:
  int master;
  pid_t child;
  void SetUp() override {
    struct winsize win = {
        80, 24, // row, col
        480, 192 // pixes, unused
    };

    child = forkpty(&master, NULL, NULL, &win);
    EXPECT_NE(child, -1);
    if (child == -1)
      exit(-3);

    if (child == 0) {
      const char *lgshell = "main/lgshell";
      if (access(lgshell, X_OK) == -1) {
        lgshell = "bazel-bin/main/lgshell";
        if (access(lgshell, X_OK) == -1) {
          lgshell = "lgshell";
          EXPECT_TRUE(access(lgshell, X_OK) != -1);
        }
      }
      execlp(lgshell, lgshell, "-q", 0);
      EXPECT_TRUE(false); // Should never reach here
    }

  }

  void drain_stdin() {
    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    char buffer[200] = {0,};
    int sz = 1;
    while(sz>0) {
      sz = read(master,buffer,200);
    }
    fcntl(master, F_SETFL, flags); // Switch back to blocking
  }

  std::string read_line_plain() {
    std::string line;
    char buffer;
    while(1) {
      //std::cout << "y\n";
      int sz = read(master,&buffer,1);
      if (sz != 1)
        break;

      if (buffer == '\x1B' ) {
        line = ""; // We are still in a escaped text
        //read_line(); // consume until the end again
        continue;
      }
      //std::cout << "xx[" << buffer <<  "]\n";

      if ((buffer == '\n' || buffer == '\r' || buffer == ' ') && line.empty())
        continue;

      if ((buffer == '\n' || buffer == '\r') && line.size())
        break;
      else
        line += buffer;
    }
    //std::cerr << "pline:" << line << std::endl;
    return line;
  }

  std::string read_line() {
    std::string line;
    char buffer;
    while(1) {
      int sz = read(master,&buffer,1);
      if (sz != 1)
        break;

      if ((buffer == '#') && line.size())
        break;
      line += buffer;
    }
#if 1
    //sed "s,\x1B\[[0-9;]*[a-zA-Z],,g"
    std::regex sed("(\\x1B\\[[0-9;]*[a-zA-Z])");
    line = std::regex_replace (line,sed,"");
#endif
    //std::cerr << "xline:" << line << std::endl;
    return line;
  }
};

TEST_F(MainTest, Comments) {

  drain_stdin();
  std::string cmd = "// COMMENT#\n"; // # is a marker for the stupid espace lines

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line(); // > // COMMENT
  std::string l1 = read_line(); // // COMMENT

  EXPECT_THAT(l0, HasSubstr("COMMENT")); // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("// COMMENT"));
}

TEST_F(MainTest, MultiComments) {

  drain_stdin();
  //std::string subcmd = "/* ERROR */ files path:. /* COMMENT */ match:\"xxx$\" |> dump // more #";
  std::string subcmd = "/*asdasd */ fil\t path:. /*zzz*/ match:\"xxx$\" |> dump // more #";
  std::string cmd = subcmd + "\n";

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line(); // typed line
  std::string l1 = read_line(); // cut&paste command echo
  read_line(); // files:
  read_line(); // files:
  std::string l4 = read_line_plain(); // files:
  std::string l5 = read_line_plain(); // match:xxx$
  std::string l6 = read_line_plain(); // dump
  std::string l7 = read_line_plain(); // dump

  EXPECT_THAT(l0, HasSubstr("dump")); // It has escape characters, just match a word
  EXPECT_THAT(l1, HasSubstr("dump"));
  EXPECT_THAT(l4, HasSubstr("lgraph.dump labels:"));
  EXPECT_THAT(l5, HasSubstr("match:xxx$"));
  EXPECT_THAT(l6, HasSubstr("files:"));
  EXPECT_THAT(l7, HasSubstr("lgraph.dump lgraphs:"));
}

TEST_F(MainTest, Autocomplete) {

  drain_stdin();
  std::string cmd = "fil\t#\n"; // # is a marker for the stupid espace lines

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line();

  EXPECT_THAT(l0, HasSubstr("fil")); // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("files"));
}

#if 0
TEST_F(MainTest, LabelsComplete) {

  drain_stdin();
  std::string cmd = "files pa\t:nothing#\n";

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line();
  std::cout << "labels:" << l0 << std::endl;
  std::cout << "labels:" << l1 << std::endl;

  EXPECT_THAT(l0, HasSubstr("path:"));
  EXPECT_THAT(l0, HasSubstr("nothing"));
}
#endif

TEST_F(MainTest, Help) {

  drain_stdin();
  std::string cmd = "he\t#\n"; // # is a marker for the stupid espace lines

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line();

  EXPECT_THAT(l0, HasSubstr("he")); // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("help"));
}

TEST_F(MainTest, HelpPass) {

  drain_stdin();
  std::string cmd = "help inou.graphviz #\n "; // # is a marker for the stupid espace lines

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line();
  read_line();
  read_line();
  read_line_plain();
  std::string l4 = read_line_plain();
  std::string l5 = read_line_plain();

  EXPECT_THAT(l0, HasSubstr("help"));
  EXPECT_THAT(l4, HasSubstr("dot format"));
  EXPECT_THAT(l5, HasSubstr("optional"));
}

TEST_F(MainTest, Quit) {

  drain_stdin();
  std::string cmd = "qui\t#\n"; // # is a marker for the stupid espace lines

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line_plain();

  EXPECT_THAT(l0, HasSubstr("qui")); // It has escape colors, just match word
  if (!l1.empty() && l1[0] != '[') {
    EXPECT_THAT(l1, HasSubstr("unset HOME")); // sanbox unsets HOME variable
  }
}

#if 0
int main(int argc, char** argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
