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
/******************************************************************************
 *include files
 ******************************************************************************/
#include "task_scheduler.h"
#include <algorithm>
#include <iomanip>
#include <labios/common/data_structures.h>
#include <labios/common/external_clients/memcached_impl.h>
#include <labios/labios_system.h>

std::shared_ptr<task_scheduler> task_scheduler::instance = nullptr;
service task_scheduler::service_i = service(TASK_SCHEDULER);
/******************************************************************************
 *Interface
 ******************************************************************************/
int task_scheduler::run() {
  auto queue = labios_system::getInstance(service_i)->get_client_queue(
      CLIENT_TASK_SUBJECT);
  auto task_list = std::vector<task *>();
  Timer t = Timer();
  t.startTime();
  int status;

  while (!kill) {
    status = -1;
    auto task_i = queue->subscribe_task_with_timeout(status);
    if (status != -1 && task_i != nullptr) {
      task_list.push_back(task_i);
    }
    auto time_elapsed = t.stopTime();
    if (!task_list.empty() && (task_list.size() >= MAX_NUM_TASKS_IN_QUEUE ||
                               time_elapsed >= MAX_SCHEDULE_TIMER)) {
      // scheduling_threads.submit(std::bind(schedule_tasks, task_list));
      schedule_tasks(task_list);
      t.startTime();
      task_list.clear();
    }
  }
  return 0;
}

void task_scheduler::schedule_tasks(std::vector<task *> &tasks) {
#ifdef TIMERTS
  Timer t = Timer();
  t.resumeTime();
#endif
  auto solver_i = labios_system::getInstance(service_i)->solver_i;
  solver_input input(tasks, static_cast<int>(tasks.size()));
  solver_output output = solver_i->solve(input);

  for (auto element : output.worker_task_map) {
    auto queue =
        labios_system::getInstance(service_i)->get_worker_queue(element.first);
    for (auto task : element.second) {

      switch (task->t_type) {
      case task_type::WRITE_TASK: {
        auto *wt = reinterpret_cast<write_task *>(task);
#ifdef DEBUG
        std::cout << "threadID:" << std::this_thread::get_id() << "\tOperation"
                  << static_cast<std::underlying_type<task_type>::type>(
                         task->t_type)
                  << "\tDataspaceID#" << wt->destination.filename << "\tTask#"
                  << task->task_id << "\tWorker#" << element.first << "\n";
#endif
        queue->publish_task(wt);
        break;
      }
      case task_type::READ_TASK: {
        auto *rt = reinterpret_cast<read_task *>(task);
#ifdef DEBUG
        std::cout << "threadID:" << std::this_thread::get_id() << "\tOperation"
                  << static_cast<std::underlying_type<task_type>::type>(
                         task->t_type)
                  << "\tTask#" << task->task_id << "\tWorker#" << element.first
                  << "\n";
#endif
        queue->publish_task(rt);
        break;
      }
      }
    }
  }
  for (auto task : tasks) {
    delete task;
  }
  delete input.task_size;
  delete output.solution;
#ifdef TIMERTS
  std::stringstream stream;
  stream << "task_scheduler::schedule_tasks()," << std::fixed
         << std::setprecision(10) << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
}
