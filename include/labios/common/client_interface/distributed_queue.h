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
#ifndef LABIOS_MAIN_DISTRIBUTEDQUEUE_H
#define LABIOS_MAIN_DISTRIBUTEDQUEUE_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <labios/common/data_structures.h>
#include <labios/common/enumerations.h>
#include <labios/common/exceptions.h>
#include <labios/common/external_clients/serialization_manager.h>
#include <memory>
#include <nats.h>
/******************************************************************************
 *Class
 ******************************************************************************/
class distributed_queue {
protected:
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
  service service_i;
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  explicit distributed_queue(service service) : service_i(service) {}

public:
  /******************************************************************************
   *Interface
   ******************************************************************************/
  virtual int publish_task(task *task_t) {
    throw NotImplementedException("publish_task");
  }
  virtual task *subscribe_task_with_timeout(int &status) {
    throw NotImplementedException("subscribe_task_with_timeout");
  }

  virtual task *subscribe_task(int &status) {
    throw NotImplementedException("subscribe_task");
  }

  virtual int get_queue_size() {
    throw NotImplementedException("get_queue_size");
  }
  virtual int get_queue_count() {
    throw NotImplementedException("get_queue_count");
  }
  virtual int get_queue_count_limit() {
    throw NotImplementedException("get_queue_count");
  }
  /******************************************************************************
   *Destructor
   ******************************************************************************/
  virtual ~distributed_queue() {}
};

#endif // LABIOS_MAIN_DISTRIBUTEDQUEUE_H
