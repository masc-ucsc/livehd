
#include "slang_context.hpp"

extern int slang_main(int argc, char** argv, Slang_context& tree);  // in slang_driver.cpp

int main(int argc, char** argv) {
  Slang_context tree;
  return slang_main(argc, argv, tree);
}
