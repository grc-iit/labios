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
// Created by hdevarajan on 5/10/18.
//

#ifndef LABIOS_MAIN_POSIXCLIENT_H
#define LABIOS_MAIN_POSIXCLIENT_H

#include "io_client.h"
#include "labios/common/config_manager.h"
#include <chrono>
#include <labios/common/data_structures.h>

using namespace std::chrono;

class posix_client : public io_client {
  std::string dir;

public:
  posix_client(int worker_index) : io_client(worker_index) {
    dir = ConfigManager::get_instance()->WORKER_PATH + "/" +
          std::to_string(worker_index) + "/";
  }
  int write(write_task task) override;
  int read(read_task task) override;
  int delete_file(delete_task task) override;
  int flush_file(flush_task task) override;
};

#endif // LABIOS_MAIN_POSIXCLIENT_H
