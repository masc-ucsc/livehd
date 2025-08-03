
#include "slang_tree.hpp"

extern int slang_main(int argc, char** argv, Slang_tree& tree);  // in slang_driver.cpp

int main(int argc, char** argv) {
  Slang_tree tree;
  return slang_main(argc, argv, tree);
}
