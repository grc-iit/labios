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

#ifndef LABIOS_MAIN_IO_CLIENT_H
#define LABIOS_MAIN_IO_CLIENT_H

#include <labios/common/data_structures.h>

class io_client {
protected:
  int worker_index;

public:
  io_client(int worker_index) : worker_index(worker_index) {}
  virtual int write(write_task task) = 0;
  virtual int read(read_task task) = 0;
  virtual int delete_file(delete_task task) = 0;
  virtual int flush_file(flush_task task) = 0;
};
#endif // LABIOS_MAIN_IO_CLIENT_H
