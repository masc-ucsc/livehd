#include "prp_lnast.hpp"
#include <chrono>

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s file\n", argv[0]);
    exit(1);
  }
  Prp_lnast converter;

  auto start = std::chrono::system_clock::now();
  converter.parse_file(argv[1]);
  auto lnast = converter.prp_ast_to_lnast("test");
  auto end = std::chrono::system_clock::now();

  auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Elapsed time: " << elapsed_time.count() << " ms\n";

  fmt::print("AST to LNAST output:\n\n");

  std::string rule_name;
  for(const auto &it:lnast->depth_preorder(lnast->get_root())){
    auto node = lnast->get_data(it);
    std::string indent{"  "};
    for(int i=0;i<it.level;++i)
      indent += "  ";

    fmt::print("{} {} {:>20} : {}\n", it.level, indent, converter.Lnast_type_to_string(node.type), node.token.text);
  }

  return 0;
}
