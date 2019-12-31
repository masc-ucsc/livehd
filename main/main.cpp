//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "eprp.hpp"
#include "iassert.hpp"
#include "lgraph.hpp"

#include "replxx.hxx"
using Replxx = replxx::Replxx;

#include "main_api.hpp"

void help(const std::string& cmd, const std::string& txt) { fmt::print("{:20s} {}\n", cmd, txt); }

void help_labels(const std::string& cmd, const std::string& txt, bool required) {
  if (required)
    fmt::print("  {:20s} {} (required)\n", cmd, txt);
  else
    fmt::print("  {:20s} {} (optional)\n", cmd, txt);
}

// prototypes
Replxx::completions_t hook_completion(std::string const& context, int index, void* user_data);
Replxx::hints_t       hook_hint(std::string const& context, int index, Replxx::Color& color, void* user_data);
void                  hook_color(std::string const& str, Replxx::colors_t& colors, void* user_data);

Replxx::completions_t hook_shared(std::string const& context, int index, void* user_data, bool add_all) {
  auto*                 examples = static_cast<std::vector<std::string>*>(user_data);
  Replxx::completions_t completions;

  int  last_cmd_start = context.size();
  int  last_cmd_end   = context.size();
  bool skipping_word  = true;

  int  last_label_start = context.size();
  bool last_label_found = false;
  bool last_label_done  = false;

  for (int i = context.size(); i >= 0; i--) {
    if (context[i] == ' ') {
      skipping_word   = false;
      last_label_done = true;
      continue;
    }
    if (context[i] == '>') break;
    if (context[i] == ':') {
      last_cmd_start = i;
      last_cmd_end   = i;
      skipping_word  = true;
      if (!last_label_found && !last_label_done) {
        last_label_found = true;
      } else {
        last_label_done = true;
      }
    }
    if (last_label_found && !last_label_done) {
      last_label_start = i;
    }
    if (!skipping_word) {
      last_cmd_start = i;
    }
  }

  std::vector<std::string> fields;

  std::string prefix{context.substr(index)};

  std::string prefix_add = "";

  if (last_label_found && last_label_done) {
    std::string label         = context.substr(last_label_start, context.size());
    std::string full_filename = "";
    auto        pos           = label.find_last_of(":");
    if (pos != std::string::npos) {
      auto pos2 = label.find_last_of(',');
      if (pos2 == std::string::npos) {
        pos2 = pos;
      }
      full_filename = label.substr(pos2 + 1);
      prefix_add    = label.substr(0, pos2 + 1);
      prefix        = full_filename;  // Overwrite beginning of the match
      label         = label.substr(0, pos);
    }
    bool label_files  = strcasecmp(label.c_str(), "files") == 0;
    bool label_output = strcasecmp(label.c_str(), "output") == 0;
    bool label_path   = strcasecmp(label.c_str(), "path") == 0;
    bool label_odir   = strcasecmp(label.c_str(), "odir") == 0;
    if (label_files || label_output || label_path || label_odir) {
      std::string path = ".";
      auto        pos2  = full_filename.find_last_of('/');
      std::string filename;
      if (pos2 != std::string::npos) {
        path     = full_filename.substr(0, pos2);
        filename = full_filename.substr(pos2 + 1);
        prefix_add += path + "/";
        prefix = filename;
      } else {
        filename = full_filename;
      }
      // fmt::print("label[{}] full_filename[{}] path[{}] filename[{}] prefix[{}] add[{}]\n", label, full_filename, path, filename,
      // prefix, prefix_add);
      DIR* dirp = opendir(path.c_str());
      if (dirp) {
        std::vector<std::string> sort_files;
        struct dirent*           dp;
        while ((dp = readdir(dirp)) != NULL) {
          if (dp->d_type != DT_DIR && (label_path || label_odir)) continue;
          // fmt::print("preadding {}\n",dp->d_name);
          if (strncasecmp(dp->d_name, filename.c_str(), filename.size()) == 0 || filename.empty()) {
            // fmt::print("adding {}\n",dp->d_name);
            sort_files.push_back(dp->d_name);
          }
        }
        closedir(dirp);
        if (!sort_files.empty()) {
          std::sort(sort_files.begin(), sort_files.end());
          fields = sort_files;
        }
      }
      examples = &fields;
    }
  } else if (last_cmd_start < last_cmd_end) {
    std::string cmd = context.substr(last_cmd_start, last_cmd_end);
    auto        pos = cmd.find(" ");
    if (pos != std::string::npos) {
      cmd = cmd.substr(0, pos);
    }
    // fmt::print("cmd[{}]\n", cmd);
    Main_api::get_labels(
        cmd, [&fields](const std::string& label, const std::string txt, bool required) { (void)txt; fields.push_back(label + ":"); });
    if (!fields.empty()) examples = &fields;
  }

  for (auto const& e : *examples) {
    // fmt::print("checking {} vs {}\n",e, prefix);
    if (strncasecmp(prefix.c_str(), e.c_str(), prefix.size()) == 0) {
      // fmt::print("match {}\n",e);
      if (add_all) {
        auto pos = prefix_add.find_last_of('/');
        if (pos != std::string::npos) {
          prefix_add = prefix_add.substr(pos + 1);
        }
        completions.emplace_back((prefix_add + e).c_str());
      } else
        completions.emplace_back(e.c_str());
    }
  }

#if 1
  if (!add_all && completions.size() == 1 && !prefix_add.empty()) {
    // find last / as completions seem to work upto last char
    auto pos = prefix_add.find_last_of('/');
    if (pos != std::string::npos) {
      prefix_add = prefix_add.substr(pos + 1);
    }
    completions[0] = prefix_add + completions[0];
  }
#endif

  return completions;
}

Replxx::completions_t hook_completion(std::string const& context, int index, void* user_data) {
  return hook_shared(context, index, user_data, false);
}

Replxx::hints_t hook_hint(std::string const& context, int index, Replxx::Color& color, void* user_data) {
  Replxx::hints_t hints;

  // only show hint if prefix is at least 'n' chars long
  // or if prefix begins with a specific character
  std::string prefix{context.substr(index)};
  if (prefix.size() >= 2 || (!prefix.empty() && prefix.at(0) == '!')) {
    auto opts = hook_shared(context, index, user_data, true);
#if 1
    for (auto const& e : opts) {
      // fmt::print("prefix[{}] e[{}]\n",prefix,e);
      // if (strncasecmp(e.c_str(), prefix.c_str(), prefix.size()) == 0 ) {
      if (e.size()>prefix.size())
        hints.emplace_back(e.substr(prefix.size()).c_str());
      //}
    }
#else
    auto* examples = static_cast<std::vector<std::string>*>(user_data);
    for (auto const& e : *examples) {
      if (e.compare(0, prefix.size(), prefix) == 0) {
        hints.emplace_back(e.substr(prefix.size()).c_str());
      }
    }
#endif
  }

  // set hint color to green if single match found
  if (hints.size() == 1) {
    color = Replxx::Color::GREEN;
  }

  return hints;
}

int real_len(std::string const& s) {
  int     len(0);
  uint8_t m4(128 + 64 + 32 + 16);
  uint8_t m3(128 + 64 + 32);
  uint8_t m2(128 + 64);
  for (int i(0); i < static_cast<int>(s.length()); ++i, ++len) {
    uint8_t c(s[i]);
    if ((c & m4) == m4) {
      i += 3;
    } else if ((c & m3) == m3) {
      i += 2;
    } else if ((c & m2) == m2) {
      i += 1;
    }
  }
  return (len);
}

void hook_color(std::string const& context, Replxx::colors_t& colors, void* user_data) {
  auto* regex_color = static_cast<std::vector<std::pair<std::string, Replxx::Color>>*>(user_data);

  // highlight matching regex sequences
  for (auto const& e : *regex_color) {
    size_t      pos{0};
    std::string str = context;
    std::smatch match;

    while (std::regex_search(str, match, std::regex(e.first))) {
      std::string c{match[0]};
      pos += real_len(match.prefix());
      int len(real_len(c));

      for (int i = 0; i < len; ++i) {
        colors.at(pos + i) = e.second;
      }

      pos += len;
      str = match.suffix();
    }
  }
}

int main(int argc, char** argv) {
  I_setup();

  bool option_quiet = false;

  std::string cmd;

  for(int i=1;i<argc;++i) {
    if (argv[i][0] == '-') {
      if (strcasecmp(argv[1], "-q") == 0) option_quiet = true;
    } else {
      if (cmd.empty())
        cmd.append(argv[i]);
      else
        absl::StrAppend(&cmd, " ", argv[i]);
    }
  }

  Main_api::init();

  if (!cmd.empty()) {
    fmt::print("lgraph cmd {}\n", cmd);
    Main_api::parse_inline(cmd);
    exit(0);
  }

  using cl = Replxx::Color;
  std::vector<std::pair<std::string, cl>> regex_color{
      // pipe operator
      {"\\|\\>", cl::BRIGHTCYAN},

      // method keywords
      {"color_blue", cl::CYAN},

      // commands
      {"help", cl::BRIGHTMAGENTA},
      {"history", cl::BRIGHTMAGENTA},
      {"quit", cl::BRIGHTMAGENTA},
      {"exit", cl::BRIGHTMAGENTA},
      {"clear", cl::BRIGHTMAGENTA},
      {"prompt", cl::BRIGHTMAGENTA},

      // numbers
      {"[\\-|+]{0,1}[0-9]+", cl::CYAN},                     // integers
      {"[\\-|+]{0,1}[0-9]*\\.[0-9]+", cl::CYAN},            // decimals
      {"[\\-|+]{0,1}[0-9]+e[\\-|+]{0,1}[0-9]+", cl::CYAN},  // scientific notation

      // registers
      {"\\@\\w+", cl::MAGENTA},

      // labels
      {"\\w+\\:", cl::YELLOW},

  };

  // init the repl
  Replxx rx;
  rx.install_window_change_handler();

  // words to be completed
  std::vector<std::string> examples{
      "help", "history", "quit", "exit", "clear", "prompt ",
  };

  // init all the lgraph libraries used
  Main_api::get_commands([&examples](const std::string& cmd, const std::string& help_msg) { examples.push_back(cmd); });

  const char* env_home = std::getenv("HOME");
  bool        history  = true;
  if (env_home == 0) {
    Pass::warn("unset HOME directory, not loading history file");
    history = false;
  }
  // the path to the history file
  std::string history_file;

  if (history) {
    history_file = std::string(env_home);
    history_file.append("/.config/lgraph/history.txt");

    if (access(history_file.c_str(), F_OK) == -1) {
      std::cout << "Setting history file to $HOME/.config/lgraph/history.txt\n";
      std::string lgraph_path(env_home);
      lgraph_path.append("/.config");
      if (access(lgraph_path.c_str(), F_OK) == -1) {
        int ok = mkdir(lgraph_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ok < 0) {
          std::cerr << "error: could not create " << lgraph_path << " directory for history.txt\n";
          exit(-3);
        }
      }
      lgraph_path.append("/lgraph");
      if (access(lgraph_path.c_str(), F_OK) == -1) {
        int ok = mkdir(lgraph_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ok < 0) {
          std::cerr << "error: could not create " << lgraph_path << " directory for history.txt\n";
          exit(-3);
        }
      }
      int ok = creat(history_file.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (ok < 0) {
        std::cerr << "error: could not create " << history_file << "\n";
        exit(-3);
      }
    }

    rx.history_load(history_file);
  }

  rx.set_max_history_size(8192);
  // rx.set_max_line_size(32768);
  rx.set_max_hint_rows(6);

  rx.set_highlighter_callback(hook_color, static_cast<void*>(&regex_color));

  rx.set_completion_callback(hook_completion, static_cast<void*>(&examples));
  rx.set_hint_callback(hook_hint, static_cast<void*>(&examples));

  if (!option_quiet) {
    std::cout << "Welcome to lgraph\n"
              << "Press 'tab' to view autocompletions\n"
              << "Type 'help' for help\n"
              << "Type 'quit' or 'exit' to exit\n\n";
  }

  // set the repl prompt
  std::string prompt{"\x1b[1;32mlgraph\x1b[0m> "};

  // main repl loop
  for (;;) {
    // display the prompt and retrieve input from the user
    char const* cinput{nullptr};

    do {
      cinput = rx.input(prompt);
    } while ((cinput == nullptr) && (errno == EAGAIN));

    if (cinput == nullptr) {
      break;
    }
    if (cinput[0] == 0) {
      continue;  // Empty line
    }

    std::string input{cinput};

    if (input.compare(0, 4, "quit") == 0 || input.compare(0, 4, "exit") == 0 || input.compare(0, 1, "q") == 0 ||
        input.compare(0, 1, "x") == 0) {
      rx.history_add(input);
      break;

    } else if (input.compare(0, 4, "help") == 0) {
      auto pos = input.find(" ");
      while (input[pos + 1] == ' ') pos++;

      if (pos == std::string::npos) {
        help("help [str]", "this output, or for a specific command");
        help("quit", "exit lgraph");
        help("exit", "exit lgraph");
        help("clear", "clear the screen");
        help("history", "display the current history");
        help("prompt <str>", "change the current prompt");

        Main_api::get_commands(help);
      } else {
        std::string cmd  = input.substr(pos + 1);
        auto        pos2 = cmd.find(" ");
        if (pos2 != std::string::npos) cmd = cmd.substr(0, pos2);

        help(cmd, Main_api::get_command_help(cmd));
        Main_api::get_labels(cmd, help_labels);
      }

      rx.history_add(input);
      continue;

    } else if (input.compare(0, 6, "prompt") == 0) {
      // set the repl prompt text
      auto pos = input.find(" ");
      if (pos == std::string::npos) {
        std::cout << "Error: 'prompt' missing argument\n";
      } else {
        prompt = input.substr(pos + 1) + " ";
      }

      rx.history_add(input);
      continue;

    } else if (input.compare(0, 7, "history") == 0) {
      // display the current history
      for (size_t i = 0, sz = rx.history_size(); i < sz; ++i) {
        std::cout << std::setw(4) << i << ": " << rx.history_line(i) << "\n";
      }

      rx.history_add(input);
      continue;

    } else if (input.compare(0, 5, "clear") == 0) {
      // clear the screen
      rx.clear_screen();

      rx.history_add(input);
      continue;

    } else {
      // default action
      std::cout << input << "\n";

      Main_api::parse_inline(input);
      Graph_library::sync_all();

      rx.history_add(input);
      continue;
    }
  }

  if (!option_quiet) std::cerr << "See you soon\n";

  if (history) rx.history_save(history_file);

  Graph_library::sync_all();

  if (Main_api::has_errors()) return 1;

  return 0;
}
