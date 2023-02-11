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
#include <iomanip>
#include <labios/common/external_clients/nats_impl.h>
#include <labios/common/timer.h>

/******************************************************************************
 *Interface
 ******************************************************************************/
int NatsImpl::publish_task(task *task_t) {
#ifdef TIMERNATS
  Timer t = Timer();
  t.resumeTime();
#endif
  auto msg = serialization_manager().serialize_task(task_t);
  natsConnection_PublishString(nc, subject.c_str(), msg.c_str());
#ifdef TIMERNATS
  std::stringstream stream;
  stream << "NatsImpl::publish_task(" +
                std::to_string(
                    static_cast<std::underlying_type<task_type>::type>(
                        task_t->t_type)) +
                "),"
         << std::fixed << std::setprecision(10) << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
  return 0;
}

task *NatsImpl::subscribe_task_with_timeout(int &status) {
#ifdef TIMERNATS
  Timer t = Timer();
  t.resumeTime();
#endif
  natsMsg *msg = nullptr;
  natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER_MS);
  if (msg == nullptr)
    return nullptr;
  task *task = serialization_manager().deserialize_task(natsMsg_GetData(msg));
  status = 0;
#ifdef TIMERNATS
  std::stringstream stream;
  stream << "NatsImpl::subscribe_task_with_timeout(" +
                std::to_string(
                    static_cast<std::underlying_type<task_type>::type>(
                        task->t_type)) +
                "),"
         << std::fixed << std::setprecision(10) << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
  return task;
}

task *NatsImpl::subscribe_task(int &status) {
#ifdef TIMERNATS
  Timer t = Timer();
  t.resumeTime();
#endif
  natsMsg *msg = nullptr;
  natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER_MS_MAX);
  if (msg == nullptr)
    return nullptr;
  task *task = serialization_manager().deserialize_task(natsMsg_GetData(msg));
  status = 0;
#ifdef TIMERNATS
  std::stringstream stream;
  stream << "NatsImpl::subscribe_task(" +
                std::to_string(
                    static_cast<std::underlying_type<task_type>::type>(
                        task->t_type)) +
                "),"
         << std::fixed << std::setprecision(10) << t.pauseTime() << "\n";
  std::cout << stream.str();
#endif
  return task;
}

int NatsImpl::get_queue_size() {
  int size_of_queue;
  natsSubscription_GetPending(sub, nullptr, &size_of_queue);
  return size_of_queue;
}

int NatsImpl::get_queue_count() {
  int *count_of_queue = new int();
  natsSubscription_GetStats(sub, count_of_queue, NULL, NULL, NULL, NULL, NULL);
  int queue_count = *count_of_queue;
  delete (count_of_queue);
  return queue_count;
}

int NatsImpl::get_queue_count_limit() { return INT_MAX; }
