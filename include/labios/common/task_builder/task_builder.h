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
/*******************************************************************************
 * Created by hariharan on 2/23/18.
 * Updated by akougkas on 6/26/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_TASK_HANDLER_H
#define LABIOS_MAIN_TASK_HANDLER_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <chrono>
#include <labios/common/client_interface/distributed_queue.h>
#include <labios/common/data_structures.h>
#include <labios/common/enumerations.h>
#include <memory>
/******************************************************************************
 *Class
 ******************************************************************************/
class task_builder {
private:
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
  static std::shared_ptr<task_builder> instance;
  service service_i;
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  explicit task_builder(service service) : service_i(service) {}

public:
  /******************************************************************************
   *Interface
   ******************************************************************************/
  inline static std::shared_ptr<task_builder> getInstance(service service) {
    return instance == nullptr
               ? instance =
                     std::make_shared<task_builder>(task_builder(service))
               : instance;
  }
  std::vector<write_task *> build_write_task(write_task task, std::string data);
  std::vector<read_task> build_read_task(read_task task);
  /******************************************************************************
   *Destructor
   ******************************************************************************/
  virtual ~task_builder() {}
};

#endif // LABIOS_MAIN_TASK_HANDLER_H
