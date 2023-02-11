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
 * Created by hariharan on 5/9/18.
 * Updated by akougkas on 6/26/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_TASK_SCHEDULER_SERVICE_H
#define LABIOS_MAIN_TASK_SCHEDULER_SERVICE_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <labios/common/config_manager.h>
#include <labios/common/enumerations.h>
#include <labios/common/external_clients/nats_impl.h>
#include <labios/common/threadPool.h>
#include <labios/common/timer.h>
#include <memory>
#include <zconf.h>
/******************************************************************************
 *Class
 ******************************************************************************/
class task_scheduler {
private:
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
  static std::shared_ptr<task_scheduler> instance;
  static service service_i;
  threadPool scheduling_threads;
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  explicit task_scheduler(service service_i)
      : kill(false), scheduling_threads(
                         ConfigManager::get_instance()->TS_NUM_WORKER_THREADS) {
    scheduling_threads.init();
  }
  /******************************************************************************
   *Interface
   ******************************************************************************/
  static void schedule_tasks(std::vector<task *> &tasks);

public:
  int kill;
  inline static std::shared_ptr<task_scheduler> getInstance(service service) {
    return instance == nullptr ? instance = std::shared_ptr<task_scheduler>(
                                     new task_scheduler(service))
                               : instance;
  }
  int run();
  /******************************************************************************
   *Destructor
   ******************************************************************************/
  virtual ~task_scheduler() { scheduling_threads.shutdown(); }
};

#endif // LABIOS_MAIN_TASK_SCHEDULER_SERVICE_H
