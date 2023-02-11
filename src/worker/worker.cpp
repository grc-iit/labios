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
#include "worker.h"
#include <iomanip>
#include <labios/common/return_codes.h>
#include <sys/stat.h>
std::shared_ptr<worker> worker::instance = nullptr;
/******************************************************************************
 *Interface
 ******************************************************************************/
int worker::run() {
  if (!setup_working_dir() == SUCCESS) {
    throw std::runtime_error("worker::setup_working_dir() failed!");
  }
  if (update_score(false) != SUCCESS) {
    throw std::runtime_error("worker::update_score() failed!");
  }
  if (update_capacity() != SUCCESS) {
    throw std::runtime_error("worker::update_capacity() failed!");
  }
  size_t task_count = 0;
  Timer t = Timer();
  t.startTime();

  do {
    int status = -1;
    task *task_i = queue->subscribe_task(status);

    if (status != -1 && task_i != nullptr) {
      task_count++;
      switch (task_i->t_type) {
      case task_type::WRITE_TASK: {
        auto *wt = reinterpret_cast<write_task *>(task_i);
#ifdef DEBUG
        std::cout << "Task#" << task_i->task_id << "\tOperation: WRITE"
                  << "\tOffset:" << wt->source.offset
                  << "\tSize:" << wt->destination.size << "\n";
#endif
        client->write(*wt);
        break;
      }
      case task_type::READ_TASK: {
        auto *rt = reinterpret_cast<read_task *>(task_i);
#ifdef DEBUG
        std::cout << "Task#" << task_i->task_id << "\tOperation: READ"
                  << "\tOffset:" << rt->source.offset
                  << "\tSize:" << rt->source.size
                  << "\tFilename:" << rt->source.filename << "\n";
#endif
        client->read(*rt);
        break;
      }
      case task_type::FLUSH_TASK: {
        auto *ft = reinterpret_cast<flush_task *>(task_i);
        client->flush_file(*ft);
        break;
      }
      case task_type::DELETE_TASK: {
        auto *dt = reinterpret_cast<delete_task *>(task_i);
        client->delete_file(*dt);
        break;
      }
      default:
        std::cerr << "Task #" << task_i->task_id << " type unknown\n";
        throw std::runtime_error("worker::run() failed!");
      }
    }
    if (t.stopTime() > WORKER_INTERVAL || task_count >= MAX_WORKER_TASK_COUNT) {
      if (update_score(false) != SUCCESS) {
        std::cerr << "worker::update_score() failed!\n";
        // throw std::runtime_error("worker::update_score() failed!");
      }
      if (update_capacity() != SUCCESS) {
        std::cerr << "worker::update_capacity() failed!\n";
        // throw std::runtime_error("worker::update_capacity() failed!");
      }
      t.startTime();
      task_count = 0;
    }
  } while (!kill);

  return 0;
}

int worker::update_score(bool before_sleeping = false) {
  int worker_score = calculate_worker_score(before_sleeping);
  // std::cout<<"worker score: "<<worker_score<<std::endl;
  if (worker_score > 0) {
    if (map->put(table::WORKER_SCORE, std::to_string(worker_index),
                 std::to_string(worker_score),
                 std::to_string(-1)) != MEMCACHED_SUCCESS)
      return WORKER__UPDATE_SCORE_FAILED;
  }
  return SUCCESS;
}

int worker::calculate_worker_score(bool before_sleeping = false) {
  float load = (float)queue->get_queue_count() / INT_MAX;
  float capacity = get_remaining_capacity() / get_total_capacity();
  float isAlive = before_sleeping ? 0 : 1;
  float energy = ((float)WORKER_ENERGY) / 5;
  float speed = ((float)WORKER_SPEED) / 5;

  float score = POLICY_WEIGHT[0] * load + POLICY_WEIGHT[1] * capacity +
                POLICY_WEIGHT[2] * isAlive + POLICY_WEIGHT[3] * energy +
                POLICY_WEIGHT[4] * speed;

  int worker_score = -1;
  if (score >= 0 && score < .20) {
    worker_score =
        static_cast<int>(5 * score * 100 + 100); // worker:
                                                 //  relatively full, busy,
                                                 //  slow, high-energy
  } else if (score >= .20 && score < .40) {
    worker_score = static_cast<int>(4 * score * 100 + 100);
  } else if (score >= .40 && score < .60) {
    worker_score = static_cast<int>(3 * score * 100 + 100);
  } else if (score >= .60 && score < .80) {
    worker_score = static_cast<int>(2 * score * 100 + 100);
  } else if (score >= .80 && score <= 1) {
    worker_score =
        static_cast<int>(1 * score * 100 +
                         100); // worker:
                               //  relatively empty, not busy, speedy, efficient
  }
  return worker_score;
}

int worker::update_capacity() {
  auto remaining_cap = get_remaining_capacity();
  if (map->put(table::WORKER_CAPACITY, std::to_string(worker_index),
               std::to_string(remaining_cap),
               std::to_string(-1)) == MEMCACHED_SUCCESS) {
    // std::cout<<"worker capacity:
    // "<<std::setprecision(6)<<remaining_cap<<"\n";
    return SUCCESS;
  } else
    return WORKER__UPDATE_CAPACITY_FAILED;
}

int worker::setup_working_dir() {
  auto config_manager = ConfigManager::get_instance();
  std::string cmd = "mkdir -p " + config_manager->WORKER_PATH + "/" +
                    std::to_string(worker_index);
  std::system(cmd.c_str());

  cmd = "rm -rf " + config_manager->WORKER_PATH + "/" +
        std::to_string(worker_index) + "/*";
  std::system(cmd.c_str());
  struct stat info;
  std::string path =
      config_manager->WORKER_PATH + "/" + std::to_string(worker_index);
  if (stat(path.c_str(), &info) != 0) {
    std::cerr << "cannot access " << path.c_str() << "\n";
    return WORKER__SETTING_DIR_FAILED;
  } else
    return SUCCESS;
}

int64_t worker::get_total_capacity() { return WORKER_CAPACITY_MAX; }

int64_t worker::get_current_capacity() {
  auto config_manager = ConfigManager::get_instance();
  std::string cmd = "du -s " + config_manager->WORKER_PATH + "/" +
                    std::to_string(worker_index) + "/" + " | awk {'print$1'}";

  std::array<char, 128> buffer = std::array<char, 128>();
  std::string result;
  std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe)
    throw std::runtime_error("popen() failed!");
  while (!feof(pipe.get())) {
    if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
      result += buffer.data();
  }
  return std::stoll(result) * KB;
}

float worker::get_remaining_capacity() {
  if (get_total_capacity() <= get_current_capacity())
    return 0;
  return (float)(get_total_capacity() - get_current_capacity());
}
