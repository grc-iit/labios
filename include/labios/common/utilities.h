/*
 * Copyright (C) 2019  SCS Lab <scs-help@cs.iit.edu>, Hariharan
 * Devarajan <hdevarajan@hawk.iit.edu>, Anthony Kougkas
 * <akougkas@iit.edu>, Xian-He Sun <sun@iit.edu>
 *
 * This file is part of Labios
 *
 * Labios is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
//
// Created by hariharan on 2/23/18.
//

#ifndef LABIOS_MAIN_UTILITY_H
#define LABIOS_MAIN_UTILITY_H

#include <cstring>
#include <getopt.h>
#include <labios/common/config_manager.h>
#include <string>
#include <vector>

static std::vector<std::string> string_split(std::string value,
                                             std::string delimiter = ",") {
  char *token = strtok(const_cast<char *>(value.c_str()), delimiter.c_str());
  std::vector<std::string> splits = std::vector<std::string>();
  while (token != NULL) {
    splits.push_back(token);
    token = strtok(NULL, delimiter.c_str());
  }
  return splits;
}
static int parse_opts(int argc, char *argv[]) {
  char *argv_cp[argc];
  for (int i = 0; i < argc; ++i) {
    argv_cp[i] = new char[strlen(argv[i]) + 1];
    strcpy(argv_cp[i], argv[i]);
  }

  auto conf = ConfigManager::get_instance();
  int flags, opt;
  int nsecs, tfnd;

  nsecs = 0;
  tfnd = 0;
  flags = 0;
  while ((opt = getopt(argc, argv_cp, "a:b:c:d:")) != -1) {
    switch (opt) {
    case 'a': {
      conf->NATS_URL_CLIENT = std::string(optarg);
      break;
    }
    case 'b': {
      conf->NATS_URL_SERVER = std::string(optarg);
      break;
    }
    case 'c': {
      conf->MEMCACHED_URL_CLIENT = std::string(optarg);
      break;
    }
    case 'd': {
      conf->MEMCACHED_URL_SERVER = std::string(optarg);
      break;
    }
    default: {
      break;
    }
    }
  }
  for (int i = 0; i < argc; ++i) {
    delete (argv_cp[i]);
  }
  return 0;
}

#endif // LABIOS_MAIN_UTILITY_H
