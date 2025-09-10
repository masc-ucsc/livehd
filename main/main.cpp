//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <format>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "eprp.hpp"
#include "iassert.hpp"
#include "lgraph.hpp"
#include "main_api.hpp"
#include "meta_api.hpp"
#include "perf_tracing.hpp"
#include "replxx.hxx"

using Replxx = replxx::Replxx;

void help(std::string_view cmd, std::string_view txt) { std::print("{:20s} {}\n", cmd, txt); }
void help_labels(std::string_view cmd, std::string_view txt, bool required) {
  if (required) {
    std::print("  {:20s} {} (required)\n", cmd, txt);
  } else {
    std::print("  {:20s} {} (optional)\n", cmd, txt);
  }
}

// prototypes
Replxx::completions_t hook_completion(std::string const& context, int index, std::vector<std::string> const& user_data);
Replxx::hints_t hook_hint(std::string const& context, int index, Replxx::Color& color, std::vector<std::string> const& user_data);
void            hook_color(std::string const& str, Replxx::colors_t& colors,
                           std::vector<std::pair<std::string, Replxx::Color>> const& user_data);

Replxx::completions_t hook_shared(std::string const& context, int index, std::vector<std::string> const& user_data) {
  auto*                 examples = &(user_data);
  Replxx::completions_t completions;

  int last_cmd_start = 0;
  int last_cmd_end   = context.size();

  int  last_label_start = context.size();
  bool last_label_found = false;
  bool last_label_done  = false;

  for (int i = context.size(); i >= 0; i--) {
    if (context[i] == ' ') {
      last_label_done = true;
      continue;
    }
    if (context[i] == '>') {
      last_cmd_start = i + 1;
      break;
    }
    if (context[i] == ':') {
      last_cmd_end = i;
      if (!last_label_found && !last_label_done) {
        last_label_found = true;
      } else {
        last_label_done = true;
      }
    }
    if (last_label_found && !last_label_done) {
      last_label_start = i;
    }
  }
  while (context[last_cmd_start] == ' ') {
    last_cmd_start++;
  }

  std::vector<std::string> fields;

  std::string prefix{context.substr(context.size() - index)};

  std::string last_cmd;

  std::string prefix_add = "";

  if (last_label_found && last_label_done) {
    std::string label         = context.substr(last_label_start, context.size());
    std::string full_filename = "";
    auto        pos           = label.find_last_of(':');
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
    // std::cerr << "[" << context << "][" << prefix << "]" << context.size() << ":" << index << "label[" << label << "]" <<
    // std::endl;

    std::string path = ".";
    auto        pos2 = full_filename.find_last_of('/');
    std::string filename;
    if (pos2 != std::string::npos) {
      path     = full_filename.substr(0, pos2);
      filename = full_filename.substr(pos2 + 1);
      prefix_add += path + "/";
      prefix = filename;
    } else {
      filename = full_filename;
    }

    bool label_files  = strcasecmp(label.c_str(), "files") == 0;
    bool label_output = strcasecmp(label.c_str(), "output") == 0;
    bool label_path   = strcasecmp(label.c_str(), "path") == 0;
    bool label_odir   = strcasecmp(label.c_str(), "odir") == 0;
    bool label_name   = strcasecmp(label.c_str(), "name") == 0;

    if (label_name) {
      std::vector<std::string> name_files;
      auto*                    library = Graph_library::instance("lgdb/");

      library->each_sub([&name_files, path](Lg_type_id id, std::string_view name) {
        (void)id;
        name_files.emplace_back(name);
      });
      fields   = name_files;
      examples = &fields;
    } else if (label_files || label_output || label_path || label_odir) {
      // std::print("label[{}] full_filename[{}] path[{}] filename[{}] prefix[{}] add[{}]\n", label, full_filename, path, filename,
      // prefix, prefix_add);
      DIR* dirp = opendir(path.c_str());
      if (dirp) {
        std::vector<std::string> sort_files;
        struct dirent*           dp;

        while ((dp = readdir(dirp)) != NULL) {
          if (dp->d_type != DT_DIR && (label_path || label_odir)) {
            continue;
          }
          // std::print("preadding {}\n",dp->d_name);

          if (strncasecmp(dp->d_name, filename.c_str(), filename.size()) == 0 || filename.empty()) {
            // std::print("adding {}\n",dp->d_name);
            struct stat sb;

            std::string dir_name{path + "/" + dp->d_name};
            if (stat(dir_name.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
              sort_files.emplace_back(std::string{dp->d_name} + "/");
            } else {
              sort_files.emplace_back(dp->d_name);
            }
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
    auto        pos = cmd.find(' ');
    if (pos != std::string::npos) {
      cmd = cmd.substr(0, pos);
    }
    // std::print("cmd[{}]\n", cmd);
    Main_api::get_labels(cmd, [&fields](std::string_view label, std::string_view txt, bool required) {
      (void)required;
      (void)txt;
      fields.emplace_back(absl::StrCat(label, ":"));
    });
    if (!fields.empty()) {
      examples = &fields;
    }
    prefix = cmd;
  }

  int last_match_end   = context.size();
  int last_match_start = last_match_end;
  for (int i = last_match_end - 1; i >= 0; --i) {
    if (!std::isalnum(context[i]) && context[i] != '.' && context[i] != '_') {
      break;
    }
    last_match_start = i;
  }
  std::string match = context.substr(last_match_start, last_match_end);

  // std::print("match[{}]\n", match);
  for (auto const& e : *examples) {
    // std::print("checking {} vs {}\n",e, match);
    if (strncasecmp(match.c_str(), e.c_str(), match.size()) == 0) {
      // std::print("match {} match:{}\n", e, match);
      completions.emplace_back(e.c_str());
    }
  }

  if (context.size() != static_cast<unsigned>(index)) {
    std::string fprefix{context.substr(context.size() - index)};
    auto        to_chop = prefix.size() - fprefix.size();
    for (auto& completion : completions) {
      const std::string comp = completion.text();
      if (comp.back() == ':') {
        continue;
      }
      // std::print("fprefix[{}] completion[{}]\n", fprefix, comp);
      if (comp.size() > to_chop && to_chop > 0) {
        completion = Replxx::Completion(comp.substr(to_chop));
      }
    }
  }

  return completions;
}

Replxx::completions_t hook_completion(std::string const& context, int index, std::vector<std::string> const& user_data) {
  return hook_shared(context, index, user_data);
}

// context is the string passed on command line
// index is the length of the last "chunk" of the string since last non-alphanumeric character
// E.g: foo       has size:3 index:3
//      foo.b     has size:5 index:1
Replxx::hints_t hook_hint(std::string const& context, int index, Replxx::Color& color, std::vector<std::string> const& user_data) {
  Replxx::hints_t hints;

  // only show hint if prefix is at least 'n' chars long
  // or if prefix begins with a specific character
  std::string prefix{context.substr(context.size() - index)};

  if (prefix.size() >= 2 || (!prefix.empty() && prefix.at(0) == '!')) {
    auto opts = hook_shared(context, index, user_data);

    for (auto const& e : opts) {
      hints.emplace_back(e.text());
    }
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

void hook_color(std::string const& context, Replxx::colors_t& colors,
                std::vector<std::pair<std::string, Replxx::Color>> const& regex_color) {
  for (auto const& e : regex_color) {
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

constexpr unsigned long major_version = 0;
constexpr unsigned long minor_version = 0;

void dummy_call_to_preserve_methods_useful_for_debugging() { std::cout << "dummy_called\n"; }

int main(int argc, char** argv) {
  // TRACE_EVENT("pass", "main");

  I_setup();

  bool option_quiet = false;

  std::string cmd;

  int c;
  int option_index = 0;

  struct option longopts[] = {{"version", no_argument, nullptr, 'v'},
                              {"quiet", no_argument, nullptr, 0},
                              {"command", required_argument, nullptr, 'c'},
                              {0, 0, 0, 0}};

  while ((c = getopt_long(argc, argv, "qvc:", longopts, &option_index)) != -1) {
    switch (c) {
      case 'q': option_quiet = true; break;
      case 'v': std::print("lgshell, version {}.{}", major_version, minor_version); return 0;
      case 'c':
        if (cmd.empty()) {
          cmd.append(optarg);
        } else {
          absl::StrAppend(&cmd, " ", optarg);
        }
        break;
      case '?': break;
      default:;
    }
  }

  for (int i = std::max(optind, 1); i < argc; ++i) {
    if (cmd.empty()) {
      cmd.append(argv[i]);
    } else {
      absl::StrAppend(&cmd, " ", argv[i]);
    }
  }

  Main_api::init();

  if (!cmd.empty()) {
    std::print("livehd cmd {}\n", cmd);
    for (const auto line : absl::StrSplit(cmd, '\n')) {
      Main_api::parse_inline(line);
    }
    return 0;
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
  Replxx rx(std::cin, std::cout, fileno(stdin), fileno(stdout), fileno(stderr));
  rx.install_window_change_handler();

  // words to be completed
  std::vector<std::string> examples{
      "help",
      "history",
      "quit",
      "exit",
      "clear",
      "prompt ",
  };

  // init all the livehd libraries used
  Main_api::get_commands([&examples](std::string_view _cmd, std::string_view help_msg) {
    (void)help_msg;
    examples.emplace_back(_cmd);
  });

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
    history_file.append("/.cache/livehd/history.txt");

    if (access(history_file.c_str(), F_OK) == -1) {
      std::cout << "Setting history file to $HOME/.cache/livehd/history.txt\n";
      std::string livehd_path(env_home);
      livehd_path.append("/.cache");
      if (access(livehd_path.c_str(), F_OK) == -1) {
        int ok = mkdir(livehd_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ok < 0) {
          std::cerr << "error: could not create " << livehd_path << " directory for history.txt\n";
          return -3;
        }
      }
      livehd_path.append("/livehd");
      if (access(livehd_path.c_str(), F_OK) == -1) {
        int ok = mkdir(livehd_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ok < 0) {
          std::cerr << "error: could not create " << livehd_path << " directory for history.txt\n";
          return -3;
        }
      }
      int ok = creat(history_file.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (ok < 0) {
        std::cerr << "error: could not create " << history_file << "\n";
        return -3;
      }
    }

    rx.history_load(history_file);
  }

  rx.set_max_history_size(8192);
  rx.set_max_hint_rows(6);
  rx.set_highlighter_callback(std::bind(&hook_color, std::placeholders::_1, std::placeholders::_2, cref(regex_color)));

  rx.set_completion_callback(std::bind(&hook_completion, std::placeholders::_1, std::placeholders::_2, cref(examples)));
  rx.set_hint_callback(std::bind(&hook_hint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, cref(examples)));

  if (!option_quiet) {
    std::cout << "Welcome to livehd!\n"
              << "Press 'tab' to view autocompletions\n"
              << "Type 'help' for help\n"
              << "Type 'quit' or 'exit' to exit\n\n";
  }

  // set the repl prompt
  std::string prompt{"\x1b[1;32mlivehd\x1b[0m> "};

  // main repl loop
  for (;;) {
    try {
      // display the prompt and retrieve input from the user
      char const* cinput{nullptr};

      do {
        cinput = rx.input(prompt);
      } while ((cinput == nullptr) && (errno == EAGAIN));

      if (cinput == nullptr) {
        std::print("errno:{}\n", errno);
        break;
      }
      if (cinput[0] == 0) {
        continue;  // Empty line
      }

      std::string input{cinput};

      if (input.compare(0, 4, "quit") == 0 || input.compare(0, 4, "exit") == 0 || input.compare(0, 1, "q") == 0
          || input.compare(0, 1, "x") == 0) {
        rx.history_add(input);
        break;

      } else if (input.compare(0, 4, "help") == 0) {
        auto pos = input.find(' ');
        while (input[pos + 1] == ' ') {
          pos++;
        }

        if (pos == std::string::npos) {
          help("help [str]", "this output, or for a specific command");
          help("quit", "exit livehd");
          help("exit", "exit livehd");
          help("clear", "clear the screen");
          help("history", "display the current history");
          help("prompt <str>", "change the current prompt");
          help("cool_mode", "become the fonz");  // https://en.wikipedia.org/wiki/Fonzie

          Main_api::get_commands(help);
        } else {
          std::string cmd2 = input.substr(pos + 1);
          auto        pos2 = cmd2.find(' ');
          if (pos2 != std::string::npos) {
            cmd2 = cmd2.substr(0, pos2);
          }

          help(cmd2, Main_api::get_command_help(cmd2));
          Main_api::get_labels(cmd2, help_labels);
        }

        rx.history_add(input);
        continue;

      } else if (input.compare(0, 6, "prompt") == 0) {
        // set the repl prompt text
        auto pos = input.find(' ');
        if (pos == std::string::npos) {
          std::cout << "Error: 'prompt' missing argument\n";
        } else {
          prompt = input.substr(pos + 1) + " ";
        }

        rx.history_add(input);
        continue;

      } else if (input.compare(0, 7, "history") == 0) {
        // display the current history
        Replxx::HistoryScan hs(rx.history_scan());
        for (int i(0); hs.next(); ++i) {
          std::cout << std::setw(4) << i << ": " << hs.get().text() << "\n";
        }

        rx.history_add(input);
        continue;

      } else if (input.compare(0, 5, "clear") == 0) {
        // clear the screen
        rx.clear_screen();

        rx.history_add(input);
        continue;

      } else if (input.compare(0, 9, "cool_mode") == 0) {
        for (auto& e : regex_color) {
          e = std::pair(e.first, cl::LIGHTGRAY);
        }

        rx.set_highlighter_callback(std::bind(&hook_color, std::placeholders::_1, std::placeholders::_2, cref(regex_color)));
        prompt = "\x1b[1;37mayyyyy\x1b[0m> ";

      } else {
        // default action
        std::cout << input << "\n";

        // add command to history file before executing it, since the command being executed
        // could throw an exception
        rx.history_add(input);
        Main_api::parse_inline(input);
        // Graph_library::sync_all();

        continue;
      }
    } catch (const std::runtime_error& re) {
      err_tracker::logger("ERROR: {}", re.what());
      std::print("ERROR: {}", re.what());
#ifndef NDEBUG
      std::cout << " (std::runtime_error)";
#endif
      std::cout << "\ncommand aborted...\n";
    } catch (const std::exception& ex) {
      err_tracker::logger("ERROR: {}", ex.what());
      std::print("ERROR: {}", ex.what());
#ifndef NDEBUG
      std::cout << " (std::exception)";
#endif
      std::cout << "\ncommand aborted...\n";
    } catch (...) {
#ifndef NDEBUG
      std::cout << " (unknown exception class)";
#endif
      std::cout << "command aborted...\n";
    }
  }

  if (!option_quiet) {
    std::cerr << "See you soon!\n";
  }

  if (history) {
    rx.history_save(history_file);
  }

  Graph_library::sync_all();

  if (Main_api::has_errors()) {
    return 1;
  }

  return 0;
}
