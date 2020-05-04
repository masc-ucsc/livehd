
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#include <regex>
#include <string>

#include <gmock/gmock.h>
#include "gtest/gtest.h"

#include "tmt_test.hpp"

using testing::HasSubstr;

class MainTest : public ::testing::Test {
protected:
  int   master;
  pid_t child;
  void  SetUp() override {
    struct winsize win = {
        80, 24,   // row, col
        480, 192  // pixes, unused
    };

    child = forkpty(&master, NULL, NULL, &win);
    EXPECT_NE(child, -1);
    if (child == -1) exit(-3);

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
      EXPECT_TRUE(false);  // Should never reach here
    }
  }

  void drain_stdin() {
    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    char buffer[200] = {
        0,
    };
    int sz = 1;
    while (sz > 0) {
      sz = read(master, buffer, 200);
    }
    fcntl(master, F_SETFL, flags);  // Switch back to blocking
  }

  std::string read_line() {
    std::string line;
    char        buffer;
    while(1) {
      bool skipping_warning = false;
      while (1) {
        int sz = read(master, &buffer, 1);
        if (sz != 1) break;

        if (line == ":0:0 warning:") {
          skipping_warning = true;
        }
        if ((buffer == '#') && line.size()) break;

        if ((buffer == '\n' || buffer == '\r' || buffer == ' ') && line.empty()) continue;

        if ((buffer == '\n' || buffer == '\r') && line.size()) break;

        line += buffer;
      }

      if (!skipping_warning)
        break;
      line.clear(); // let's try again
    }

    {
      std::string line2;
      TMT *vt = tmt_open(24, 80, nullptr, NULL, NULL);
      tmt_write(vt, line.c_str(), line.size());

      const TMTSCREEN *s = tmt_screen(vt);
      for (size_t r = 0; r < s->nline; r++) {
        if (s->lines[r]->dirty) {
          for (size_t c = 0; c < s->ncol; c++) {
            char ch = s->lines[r]->chars[c].c;

            // Do not add multiple spaces
            if (line2.empty() || !(line2.back() == ' ' && ch == ' '))
              line2.push_back(ch);
          }
        }
      }

      tmt_close(vt);
      //std::cerr << "x.xline:" << line2 << std::endl;

      return line2;
    }

#if 0
    // sed "s,\x1B\[[0-9;]*[a-zA-Z],,g"
    std::cerr << "0.xline:" << line << std::endl;
    //std::regex sed("(\\x1B\\[[0-9;]*[a-zA-Z])");
    std::regex sed2("(\\x1B)");
    auto line2 = std::regex_replace(line, sed2, " ");
    std::cerr << "1.xline:" << line2 << std::endl;

    std::regex sed("(\\x1B\\[[0-9;]*[a-zA-Z])");
    line = std::regex_replace(line, sed, " ");
    std::cerr << "2.xline:" << line << std::endl;
    return line;
#endif
  }
};

TEST_F(MainTest, Comments) {
  drain_stdin();
  std::string cmd = "// COMMENT#\n";  // # is a marker for the stupid space lines

  auto sz = write(master, cmd.c_str(), cmd.size());
  EXPECT_EQ(sz, cmd.size());

  std::string l0 = read_line();  // > // COMMENT
  std::string l1 = read_line();  // // COMMENT

  EXPECT_THAT(l0, HasSubstr("COMMENT"));  // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("// COMMENT"));
}

#if 0
// Not handling comments in replx auto completion
TEST_F(MainTest, MultiComments) {
  drain_stdin();
  // std::string subcmd = "/* ERROR */ files path:. /* COMMENT */ match:\"xxx$\" |> dump // more #";
  std::string subcmd = "/*asdasd */ fil\t path:. /*zzz*/ match:\"xxx$\" |> dump // more #";
  std::string cmd    = subcmd + "\n";

  auto sz = write(master, cmd.c_str(), cmd.size());
  EXPECT_EQ(sz, cmd.size());

  std::string l0 = read_line();        // typed line
  std::string l1 = read_line();        // cut&paste command echo
  read_line();                         // files:
  read_line();                         // files:
  std::string l4 = read_line_plain();  // files:
  std::string l5 = read_line_plain();  // match:xxx$
  std::string l6 = read_line_plain();  // dump
  std::string l7 = read_line_plain();  // dump

  EXPECT_THAT(l0, HasSubstr("dump"));  // It has escape characters, just match a word
  EXPECT_THAT(l1, HasSubstr("dump"));
  EXPECT_THAT(l4, HasSubstr("lgraph.dump labels:"));
  EXPECT_THAT(l5, AnyOf(HasSubstr("match:xxx$"), HasSubstr("files:")));
  EXPECT_THAT(l6, AnyOf(HasSubstr("match:xxx$"), HasSubstr("files:")));
  EXPECT_THAT(l7, HasSubstr("lgraph.dump lgraphs:"));
}
#endif

TEST_F(MainTest, Autocomplete) {
  drain_stdin();
  std::string cmd = "fil\t#\n";  // # is a marker for the stupid space lines

  auto sz = write(master, cmd.c_str(), cmd.size());
  EXPECT_EQ(sz, cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line();

  EXPECT_THAT(l0, HasSubstr("fil"));  // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("files"));
}

TEST_F(MainTest, LabelsComplete) {

  drain_stdin();
  std::string cmd = "files pa\t\n";

  write(master,cmd.c_str(),cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line();
  std::string l2 = read_line();
#if 0
  std::cout << "labels:" << l0 << std::endl; // typed
  std::cout << "labels:" << l1 << std::endl; // shell
  std::cout << "labels:" << l2 << std::endl; // echo
#endif

  EXPECT_THAT(l1, HasSubstr("path:"));
  EXPECT_THAT(l2, HasSubstr("path:"));
}

TEST_F(MainTest, Help) {
  drain_stdin();
  std::string cmd = "he\t#\n";  // # is a marker for the stupid space lines

  auto sz = write(master, cmd.c_str(), cmd.size());
  EXPECT_EQ(sz, cmd.size());

  std::string l0 = read_line();
  std::string l1 = read_line();

  EXPECT_THAT(l0, HasSubstr("he"));  // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("help"));
}

TEST_F(MainTest, HelpPass) {
  drain_stdin();
  std::string cmd = "help inou.graphviz.from #\n ";  // # is a marker for the stupid space lines

  auto sz = write(master, cmd.c_str(), cmd.size());
  EXPECT_EQ(sz, cmd.size());

  std::string l0 = read_line(); // typed
  std::string l1 = read_line(); // echo
  std::string l2 = read_line(); // xtra space after return
  std::string l3 = read_line(); // 1st response
  std::string l4 = read_line(); // 2nd response
  std::string l5 = read_line(); // 3rd response

  EXPECT_THAT(l0, HasSubstr("help")); // Typed
  EXPECT_THAT(l1, HasSubstr("help")); // echo
  EXPECT_THAT(l3, HasSubstr("dot format")); // explanation

  EXPECT_THAT(l4, HasSubstr("optional")); // first arg explained
}

TEST_F(MainTest, Quit) {
  drain_stdin();
  std::string cmd = "qui\t#\n";  // # is a marker for the stupid space lines

  auto sz = write(master, cmd.c_str(), cmd.size());
  EXPECT_EQ(sz, cmd.size());

  std::string l0 = read_line(); // type
  std::string l1 = read_line(); // echo
  std::string l2 = read_line(); // 1st response

  EXPECT_THAT(l0, HasSubstr("qui"));  // It has escape colors, just match word
  EXPECT_THAT(l1, HasSubstr("quit"));  // It has escape colors, just match word
}

