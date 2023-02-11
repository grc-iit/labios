//
// Created by lukemartinlogan on 12/4/21.
//

#ifndef SCS_PATH_PARSER_H
#define SCS_PATH_PARSER_H

#include <cstdlib>
#include <list>
#include <regex>
#include <string>

namespace scs {

static std::string path_parser(std::string path) {
  std::smatch env_names;
  std::regex expr("\\$\\{[^\\}]+\\}");
  if (!std::regex_search(path, env_names, expr)) {
    return path;
  }
  for (auto &env_name_re : env_names) {
    std::string to_replace = std::string(env_name_re);
    std::string env_name = to_replace.substr(2, to_replace.size() - 3);
    std::string env_val = env_name;
    try {
      char *ret = getenv(env_name.c_str());
      if (ret) {
        env_val = ret;
      } else {
        continue;
      }
    } catch (...) {
    }
    std::regex replace_expr("\\$\\{" + env_name + "\\}");
    path = std::regex_replace(path, replace_expr, env_val);
  }
  return path;
}

} // namespace scs

#endif // SCS_PATH_PARSER_H
